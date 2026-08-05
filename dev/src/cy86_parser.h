#pragma once

#include <string>
#include <vector>

#include "cy86_model.h"
#include "sema_token.h"

// The CY86 parser: the concatenated token sequence of every translation unit,
// matched against the `program` grammar of PA9.
//
// The grammar is regular, so this is one forward scan with a single token of
// lookahead - the only place a decision needs two is telling a label from an
// opcode, which is decided by the colon after it.  Nothing backtracks and
// nothing is read twice.
//
// The parser also settles what an operand is worth: a literal keeps the PA2
// bytes it was given and a label keeps its `NameId` and its constant offset,
// so the code generator never looks at a token again.
class Cy86Parser
{
public:
	Cy86Parser(const std::vector<SemaToken>& tokens,
	           const std::vector<LiteralValue>& literals,
	           const NameTable& names,
	           const Cy86OpcodeTable& opcodes,
	           Cy86Program& program);

	// Throws `SourceError` when the token sequence is not a CY86 program.
	void run();

private:
	// What an identifier spells, decided once per name rather than once per
	// use: a register, an opcode, or a label.  A spelling that is a register
	// or an opcode is one the assignment reserves, so this also answers
	// whether a label may take it - without looking at the letters again.
	struct NameFacts
	{
		NameFacts()
			: opcode(0), is_register(false), reg(CY_SP), width(0)
			, known(false), labels_a_statement(false)
		{}

		bool reserved() const { return is_register || opcode != 0; }

		const Cy86OpcodeInfo* opcode;
		bool is_register;
		Cy86Register reg;
		int width;
		bool known;
		// Whether a statement already carries this name as a label, which the
		// assignment allows only once.
		bool labels_a_statement;
	};

	const SemaToken& peek(std::size_t ahead = 0) const;
	bool at(unsigned type, std::size_t ahead = 0) const;
	void expect(unsigned type, const char* what);
	NameFacts& facts(NameId name);

	void parse_statement();
	void parse_labels(Cy86Statement& statement);
	void parse_literal_statement(Cy86Statement& statement, bool negated);
	void parse_operands(Cy86Statement& statement);
	bool parse_operand(Cy86Operand& operand);
	void parse_parenthesized(Cy86Operand& operand);
	void parse_memory(Cy86Operand& operand);
	// The `± TT_LITERAL` tail the label and register address forms share.
	void parse_offset(Cy86Operand& operand);

	const LiteralValue& literal_at(std::size_t index) const;
	// The value of the literal the cursor is on, into `operand`, negated when
	// asked, which the assignment allows only for an arithmetic type.
	void take_literal_value(Cy86Operand& operand, bool negated);
	// The same literal as a 64-bit offset, which the assignment requires be
	// integral.
	unsigned long long take_offset(bool negated);
	void set_label_reference(Cy86Operand& operand);

	const std::vector<SemaToken>* tokens_;
	const std::vector<LiteralValue>* literals_;
	const NameTable* names_;
	const Cy86OpcodeTable* opcodes_;
	Cy86Program* program_;

	std::size_t cursor_;
	std::vector<NameFacts> facts_;
};
