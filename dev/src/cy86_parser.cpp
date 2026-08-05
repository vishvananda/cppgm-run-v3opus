#include "cy86_parser.h"

#include "source_reader.h"

namespace
{

// The two's complement of `bytes`, or the sign flip of a floating value, in
// place.  2.14 gives a literal its object representation little-endian, so a
// negation is a carry from the low byte up and a floating sign is the high bit
// of the last byte the value occupies.
void negate_bytes(std::string& bytes, EFundamentalType type)
{
	if (fundamental_type_class(type) == FundamentalTypeClass::Floating)
	{
		// `long double` occupies sixteen bytes but only the low ten hold the
		// value, so the sign bit is not in the last byte of the object.
		const std::size_t used = type == FT_LONG_DOUBLE ? 10 : bytes.size();
		if (used != 0 && used <= bytes.size())
		{
			bytes[used - 1] = static_cast<char>(bytes[used - 1] ^ 0x80);
		}
		return;
	}

	unsigned carry = 1;
	for (std::size_t index = 0; index < bytes.size(); ++index)
	{
		const unsigned value = (static_cast<unsigned char>(~bytes[index]) & 0xFF) + carry;
		bytes[index] = static_cast<char>(value & 0xFF);
		carry = value >> 8;
	}
}

// `bytes` read back as a 64-bit value, widened by sign when the type it came
// from is a signed integer and by zero otherwise.
unsigned long long widen(const std::string& bytes, bool is_signed)
{
	const std::size_t size = bytes.size() < 8 ? bytes.size() : 8;
	unsigned long long value = 0;
	for (std::size_t index = size; index-- > 0; )
	{
		value = (value << 8) | static_cast<unsigned char>(bytes[index]);
	}
	const std::size_t bits = size * 8;
	if (bits != 0 && bits < 64 && is_signed && (value >> (bits - 1)) != 0)
	{
		value |= ~((1ULL << bits) - 1);
	}
	return value;
}

}

Cy86Parser::Cy86Parser(const std::vector<SemaToken>& tokens,
                       const std::vector<LiteralValue>& literals,
                       const NameTable& names,
                       const Cy86OpcodeTable& opcodes,
                       Cy86Program& program)
	: tokens_(&tokens)
	, literals_(&literals)
	, names_(&names)
	, opcodes_(&opcodes)
	, program_(&program)
	, cursor_(0)
{
}

const SemaToken& Cy86Parser::peek(std::size_t ahead) const
{
	const std::size_t index = cursor_ + ahead;
	return (*tokens_)[index < tokens_->size() ? index : tokens_->size() - 1];
}

bool Cy86Parser::at(unsigned type, std::size_t ahead) const
{
	return peek(ahead).type == type;
}

void Cy86Parser::expect(unsigned type, const char* what)
{
	if (!at(type))
	{
		throw SourceError(std::string(" expected ") + what);
	}
	++cursor_;
}

const Cy86Parser::NameFacts& Cy86Parser::facts(NameId name)
{
	if (name >= facts_.size())
	{
		facts_.resize(name + 1);
	}
	NameFacts& entry = facts_[name];
	if (!entry.known)
	{
		const std::string& text = names_->text(name);
		entry.is_register = lookup_cy86_register(text, entry.reg, entry.width);
		entry.opcode = entry.is_register ? 0 : opcodes_->find(text);
		entry.known = true;
	}
	return entry;
}

const LiteralValue& Cy86Parser::literal_at(std::size_t index) const
{
	return (*literals_)[index];
}

void Cy86Parser::run()
{
	while (!at(ST_EOF))
	{
		parse_statement();
		expect(OP_SEMICOLON, "`;` after a statement");
	}
}

void Cy86Parser::parse_statement()
{
	program_->statements.push_back(Cy86Statement());
	Cy86Statement& statement = program_->statements.back();
	parse_labels(statement);

	if (at(TT_LITERAL))
	{
		parse_literal_statement(statement, false);
		return;
	}
	if (at(OP_MINUS) && at(TT_LITERAL, 1))
	{
		++cursor_;
		parse_literal_statement(statement, true);
		return;
	}
	if (!at(TT_IDENTIFIER))
	{
		throw SourceError(" a statement is a literal or an opcode");
	}

	const NameFacts& head = facts(peek().name);
	if (head.opcode == 0)
	{
		throw SourceError(" `" + names_->text(peek().name) + "` is not an opcode");
	}
	statement.opcode = head.opcode;
	++cursor_;
	parse_operands(statement);
}

void Cy86Parser::parse_labels(Cy86Statement& statement)
{
	while (at(TT_IDENTIFIER) && at(OP_COLON, 1))
	{
		const NameId name = peek().name;
		if (opcodes_->reserved(names_->text(name)))
		{
			throw SourceError(" a label may not spell an opcode or a register");
		}
		const std::size_t index = program_->statements.size() - 1;
		if (!program_->labels.insert(std::make_pair(name, index)).second)
		{
			throw SourceError(" `" + names_->text(name) + "` labels two statements");
		}
		statement.labels.push_back(name);
		cursor_ += 2;
	}
}

void Cy86Parser::parse_literal_statement(Cy86Statement& statement, bool negated)
{
	const LiteralValue& literal = literal_at(peek().name);
	if (literal.type == kFundamentalTypeCount)
	{
		throw SourceError(" CY86 has no user-defined literals");
	}
	if (negated && literal.array)
	{
		throw SourceError(" a string literal has no negation");
	}
	statement.is_literal = true;
	statement.data = literal.data;
	if (negated)
	{
		negate_bytes(statement.data, literal.type);
	}
	// An array is aligned as its elements are, which is what 2.14 gives the
	// literal's type: for a string literal that type is the element type.
	statement.align = fundamental_type_size(literal.type);
	if (statement.align == 0)
	{
		statement.align = 1;
	}
	++cursor_;
}

void Cy86Parser::parse_operands(Cy86Statement& statement)
{
	Cy86Operand operand;
	while (parse_operand(operand))
	{
		statement.operands.push_back(operand);
		operand = Cy86Operand();
	}
	if (statement.operands.size() != statement.opcode->operands.size())
	{
		throw SourceError(" `" + statement.opcode->name + "` takes a different number of operands");
	}
}

bool Cy86Parser::parse_operand(Cy86Operand& operand)
{
	if (at(TT_LITERAL))
	{
		operand.kind = Cy86Operand::kImmediate;
		operand.data = take_literal_bytes(false, operand.data_signed);
		return true;
	}
	if (at(TT_IDENTIFIER))
	{
		const NameFacts& entry = facts(peek().name);
		if (entry.is_register)
		{
			operand.kind = Cy86Operand::kRegister;
			operand.reg = entry.reg;
			operand.width = entry.width;
			++cursor_;
			return true;
		}
		operand.kind = Cy86Operand::kImmediate;
		set_label_reference(operand);
		return true;
	}
	if (at(OP_LPAREN))
	{
		++cursor_;
		operand.kind = Cy86Operand::kImmediate;
		parse_parenthesized(operand);
		expect(OP_RPAREN, "`)` after an immediate");
		return true;
	}
	if (at(OP_LSQUARE))
	{
		++cursor_;
		operand.kind = Cy86Operand::kMemory;
		parse_memory(operand);
		expect(OP_RSQUARE, "`]` after an address");
		return true;
	}
	return false;
}

void Cy86Parser::parse_parenthesized(Cy86Operand& operand)
{
	if (at(OP_MINUS) && at(TT_LITERAL, 1))
	{
		++cursor_;
		operand.data = take_literal_bytes(true, operand.data_signed);
		return;
	}
	if (at(TT_LITERAL))
	{
		operand.data = take_literal_bytes(false, operand.data_signed);
		return;
	}
	set_label_reference(operand);
	parse_offset(operand);
}

void Cy86Parser::parse_memory(Cy86Operand& operand)
{
	if (at(TT_LITERAL))
	{
		operand.data = take_literal_bytes(false, operand.data_signed);
		return;
	}
	if (!at(TT_IDENTIFIER))
	{
		throw SourceError(" an address is a literal, a register or a label");
	}
	const NameFacts& entry = facts(peek().name);
	if (!entry.is_register)
	{
		set_label_reference(operand);
		parse_offset(operand);
		return;
	}
	if (entry.width != 64)
	{
		throw SourceError(" an address is 64 bits wide");
	}
	operand.has_base = true;
	operand.reg = entry.reg;
	operand.width = entry.width;
	++cursor_;
	parse_offset(operand);
}

void Cy86Parser::parse_offset(Cy86Operand& operand)
{
	if (!at(OP_PLUS) && !at(OP_MINUS))
	{
		return;
	}
	const bool negated = at(OP_MINUS);
	++cursor_;
	if (!at(TT_LITERAL))
	{
		throw SourceError(" an offset is a literal");
	}
	operand.addend = take_offset(negated);
}

void Cy86Parser::set_label_reference(Cy86Operand& operand)
{
	if (!at(TT_IDENTIFIER))
	{
		throw SourceError(" an immediate is a literal or a label");
	}
	const NameId name = peek().name;
	if (opcodes_->reserved(names_->text(name)))
	{
		throw SourceError(" `" + names_->text(name) + "` is not a label");
	}
	operand.has_label = true;
	operand.label = name;
	++cursor_;
}

std::string Cy86Parser::take_literal_bytes(bool negated, bool& is_signed)
{
	const LiteralValue& literal = literal_at(peek().name);
	if (literal.type == kFundamentalTypeCount)
	{
		throw SourceError(" CY86 has no user-defined literals");
	}
	// An array is not a signed integral type, so a string literal narrower
	// than its operand widens by zero however its elements are signed.
	is_signed = !literal.array && fundamental_type_is_signed(literal.type);
	std::string bytes = literal.data;
	if (negated)
	{
		if (literal.array)
		{
			throw SourceError(" a string literal has no negation");
		}
		negate_bytes(bytes, literal.type);
	}
	++cursor_;
	return bytes;
}

unsigned long long Cy86Parser::take_offset(bool negated)
{
	const LiteralValue& literal = literal_at(peek().name);
	if (!literal.integral())
	{
		throw SourceError(" an offset is an integer");
	}
	bool is_signed = false;
	const std::string bytes = take_literal_bytes(negated, is_signed);
	return widen(bytes, is_signed);
}
