#include "cy86_codegen.h"

#include "source_reader.h"

namespace
{

// Where the program image is loaded.  The ELF headers sit in the first page of
// the same segment, so the image starts one page in, which also gives the
// widest alignment a literal can ask for.
const unsigned long long kImageAddress = 0x401000;

// The line a body of code is moved to when the statement before it placed
// data.  The image is one writable and executable segment, so the instructions
// after a `data` statement would otherwise be fetched from the line the data is
// stored into, and x86 answers a store into a line it is fetching instructions
// from with a machine clear: a loop whose counter shares a line with its own
// body runs about fifty times slower than the same loop whose counter does not.
//
// The image address is a multiple of this, so aligning the image offset aligns
// the address.
const std::size_t kCodeAlignment = 64;

// The `jmp rel32` that carries a label across that gap, and its length.  A
// label's address is what the program computes distances from - the length of a
// string is the label after it minus the label on it - so the label stays where
// the data left it and this jump stands there in its place, rather than the
// code being aligned by moving the label off the end of the data.
const unsigned char kJumpRel32 = 0xE9;
const std::size_t kJumpRel32Size = 5;

// The address of a label no statement carries.  No statement can sit there, so
// one table answers both where a label is and whether it is anywhere.
const unsigned long long kUnplaced = ~0ULL;

// The x86 registers backing the CY86 ones, in the order `Cy86Register`
// numbers them.
const int kRegisterMap[] =
{
	x86::RSP, x86::RBP, x86::R12, x86::R13, x86::R14, x86::R15
};

// The scratch registers.  None of them backs a CY86 register, so an
// instruction may use them freely between reading its operands and writing its
// result.
const int kResult = x86::RAX;
const int kRight = x86::RCX;
const int kAddress = x86::R11;

// The red zone spill slots, as displacements from `rsp`.  Each is sixteen
// bytes, which is enough for the widest value the x87 forms move.
const long long kSlotA = -16;
const long long kSlotB = -32;

// The registers a Linux system call takes its arguments in, in order.
const int kArgumentRegisters[] =
{
	x86::RDI, x86::RSI, x86::RDX, x86::R10, x86::R8, x86::R9
};

int x86_register(Cy86Register reg)
{
	return kRegisterMap[reg];
}

// The bytes of `text`, little-endian, as a 64-bit value.
unsigned long long little_endian(const std::string& text)
{
	const std::size_t size = text.size() < 8 ? text.size() : 8;
	unsigned long long value = 0;
	for (std::size_t index = size; index-- > 0; )
	{
		value = (value << 8) | static_cast<unsigned char>(text[index]);
	}
	return value;
}

// The alignment a statement needs before it: the alignment of the type a
// literal has, or the width a data instruction places.  Everything else is
// code, which needs none.
std::size_t alignment_of(const Cy86Statement& statement)
{
	if (statement.is_literal)
	{
		return statement.align;
	}
	if (statement.opcode->family == CY_DATA)
	{
		return statement.opcode->width / 8;
	}
	return 1;
}

// Whether a statement places bytes the program reads and writes rather than
// bytes it executes.  A literal and a `data` instruction are the two forms
// that do; `alignment_of` above draws the same line.
bool places_data(const Cy86Statement& statement)
{
	return statement.is_literal || statement.opcode->family == CY_DATA;
}

// The x87 memory forms, as the escape byte and the ModRM digit that name them
// at each width.
struct FloatForm
{
	unsigned escape;
	int digit;
};

FloatForm float_load(int width)
{
	FloatForm form;
	form.escape = width == 32 ? 0xD9 : (width == 64 ? 0xDD : 0xDB);
	form.digit = width == 80 ? 5 : 0;
	return form;
}

FloatForm float_store(int width)
{
	FloatForm form;
	form.escape = width == 32 ? 0xD9 : (width == 64 ? 0xDD : 0xDB);
	form.digit = width == 80 ? 7 : 3;
	return form;
}

// The integer forms of the same.  Every conversion goes through the widest
// one: a narrower FISTP answers an out-of-range value with the integer
// indefinite instead of with its low bits, and a value that wide is exactly
// the case the assignment leaves undefined.
const FloatForm kIntegerLoad = { 0xDF, 5 };
const FloatForm kIntegerStore = { 0xDF, 7 };

}

Cy86Codegen::Cy86Codegen(const Cy86Program& program, NameTable& names)
	: program_(&program)
	, names_(&names)
	, assembler_(image_)
	, entry_(kImageAddress)
	, entry_field_(0)
{
}

unsigned long long Cy86Codegen::base() const
{
	return kImageAddress;
}

void Cy86Codegen::run()
{
	// Parsing settled which names label a statement, so the table their
	// addresses go into is built once at the length it will need.
	addresses_.assign(program_->label_limit, kUnplaced);

	emit_prologue();

	// Where the program runs from when no `start` labels a statement: the
	// first statement, or the implicit success an empty program ends at.
	unsigned long long first = base() + image_.size();
	if (program_->statements.empty())
	{
		emit_exit();
	}

	// Only a move between two statements is a transition.  The prologue is code
	// and runs once, so nothing is gained by moving the program's first
	// statement off its line, and 2.14's alignment is measured from where that
	// statement lands: the first data statement stays where the prologue left
	// it, and the first run therefore begins at the offset it always did.
	bool data_before = program_->statements.empty()
		|| places_data(program_->statements[0]);

	for (std::size_t index = 0; index < program_->statements.size(); ++index)
	{
		const Cy86Statement& statement = program_->statements[index];
		const bool data_here = places_data(statement);
		// Data after code moves whole onto its own line: nothing computes a
		// distance forward from a label on an instruction, so the run may carry
		// its labels with it, and starting it aligned leaves every alignment
		// inside it the one the statement asked for.
		if (data_here && !data_before)
		{
			pad_to(kCodeAlignment);
		}
		pad_to(alignment_of(statement));
		const unsigned long long here = base() + image_.size();
		if (index == 0)
		{
			first = here;
		}
		for (std::size_t label = 0; label < statement.labels.size(); ++label)
		{
			addresses_[statement.labels[label]] = here;
		}
		// The labels are placed, so the body may move off the data's line now
		// without any of them moving with it.
		if (!data_here && data_before)
		{
			jump_to_code_alignment();
		}
		data_before = data_here;
		emit_statement(statement);
	}

	// 3.6.1 has no counterpart here: the program starts at `start` if there is
	// one, and otherwise at whatever comes first.
	const NameId start = names_->intern("start");
	entry_ = start < addresses_.size() && addresses_[start] != kUnplaced
		? addresses_[start] : first;
	resolve();
}

void Cy86Codegen::emit_prologue()
{
	// The CY86 registers start defined, and `bp` starts where `sp` does.  The
	// entry address is not known yet, so its field is patched once it is.
	for (int reg = x86::R12; reg <= x86::R15; ++reg)
	{
		assembler_.arithmetic(64, 0x32, reg, x86::RM::reg(reg));
	}
	assembler_.store(64, x86::RM::reg(x86::RBP), x86::RSP);
	entry_field_ = assembler_.load_immediate(64, kResult, 0);
	assembler_.instruction(0, 0xFF, 1, 4, false, x86::RM::reg(kResult));
}

void Cy86Codegen::emit_exit()
{
	// An empty token sequence is a program, and the only thing it can sensibly
	// do is succeed.
	assembler_.load_immediate(64, kResult, 60);
	assembler_.load_immediate(64, x86::RDI, 0);
	assembler_.byte(0x0F);
	assembler_.byte(0x05);
}

void Cy86Codegen::pad_to(std::size_t alignment)
{
	if (alignment < 2)
	{
		return;
	}
	while ((image_.size() % alignment) != 0)
	{
		assembler_.byte(0);
	}
}

void Cy86Codegen::jump_to_code_alignment()
{
	// The jump has to fit before the line it jumps to, so the target is the
	// first aligned offset the whole instruction is behind.
	const std::size_t after = image_.size() + kJumpRel32Size;
	std::size_t target = after;
	while ((target % kCodeAlignment) != 0)
	{
		++target;
	}

	const std::size_t displacement = target - after;
	assembler_.byte(kJumpRel32);
	for (std::size_t index = 0; index < 4; ++index)
	{
		assembler_.byte(static_cast<unsigned char>((displacement >> (8 * index)) & 0xFF));
	}
	while (image_.size() < target)
	{
		assembler_.byte(0);
	}
}

void Cy86Codegen::relocate(std::size_t offset, std::size_t size, NameId label)
{
	Relocation relocation;
	relocation.offset = offset;
	relocation.size = size < 8 ? size : 8;
	relocation.label = label;
	relocations_.push_back(relocation);
}

unsigned long long Cy86Codegen::address_of(NameId label) const
{
	if (label >= addresses_.size() || addresses_[label] == kUnplaced)
	{
		throw SourceError(" `" + names_->text(label) + "` labels no statement");
	}
	return addresses_[label];
}

void Cy86Codegen::resolve()
{
	for (std::size_t index = 0; index < 8; ++index)
	{
		image_[entry_field_ + index] = static_cast<char>((entry_ >> (8 * index)) & 0xFF);
	}

	// A field already holds the constant part of its value, so resolving one
	// is reading it back, adding the address, and writing it down again.
	for (std::size_t index = 0; index < relocations_.size(); ++index)
	{
		const Relocation& relocation = relocations_[index];
		unsigned long long value = address_of(relocation.label);
		for (std::size_t byte = relocation.size; byte-- > 0; )
		{
			value += static_cast<unsigned long long>(
				static_cast<unsigned char>(image_[relocation.offset + byte])) << (8 * byte);
		}
		for (std::size_t byte = 0; byte < relocation.size; ++byte)
		{
			image_[relocation.offset + byte] = static_cast<char>((value >> (8 * byte)) & 0xFF);
		}
	}
}

void Cy86Codegen::check_operands(const Cy86Statement& statement)
{
	const Cy86OpcodeInfo& info = *statement.opcode;
	for (std::size_t index = 0; index < info.operands.size(); ++index)
	{
		const Cy86OperandSpec& spec = info.operands[index];
		const Cy86Operand& operand = statement.operands[index];
		if (spec.written && operand.kind == Cy86Operand::kImmediate)
		{
			throw SourceError(" an operand written to is not an immediate");
		}
		if (spec.immediate_only && operand.kind != Cy86Operand::kImmediate)
		{
			throw SourceError(" `" + info.name + "` takes an immediate");
		}
		if (operand.kind == Cy86Operand::kRegister && operand.width != spec.width)
		{
			throw SourceError(" a register does not match the operand width");
		}
	}
}

std::string Cy86Codegen::immediate_bytes(const Cy86Operand& operand, std::size_t size) const
{
	if (!operand.has_label)
	{
		return literal_field_bytes(operand, size);
	}
	// A label reference starts as the constant part of its value; the address
	// is added into the field once every statement has one.
	std::string bytes;
	unsigned long long value = operand.addend;
	for (std::size_t index = 0; index < size; ++index)
	{
		bytes.push_back(static_cast<char>(value & 0xFF));
		value >>= 8;
	}
	return bytes;
}

void Cy86Codegen::emit_immediate(int reg, const Cy86Operand& operand, int width)
{
	const std::size_t size = width / 8;
	const unsigned long long value = operand.has_label
		? operand.addend
		: literal_field_value(operand, size);
	const std::size_t field = assembler_.load_immediate(width, reg, value);
	if (operand.has_label)
	{
		relocate(field, size, operand.label);
	}
}

void Cy86Codegen::emit_address(int reg, const Cy86Operand& operand)
{
	// Without a base register an address is a constant like any other, read at
	// the 64 bits the assignment gives every address.
	if (!operand.has_base)
	{
		emit_immediate(reg, operand, 64);
		return;
	}

	const int base = x86_register(operand.reg);
	const long long disp = static_cast<long long>(operand.addend);
	if (disp == 0)
	{
		assembler_.load(64, reg, x86::RM::reg(base));
	}
	else if (disp >= -2147483648LL && disp <= 2147483647LL)
	{
		assembler_.instruction(64, 0x8D, 1, reg, true, x86::RM::mem(base, disp));
	}
	else
	{
		assembler_.load_immediate(64, reg, operand.addend);
		assembler_.arithmetic(64, 0x02, reg, x86::RM::reg(base));
	}
}

void Cy86Codegen::emit_load(int reg, const Cy86Operand& operand, int width)
{
	if (operand.kind == Cy86Operand::kRegister)
	{
		assembler_.load(width, reg, x86::RM::reg(x86_register(operand.reg)));
		return;
	}
	if (operand.kind == Cy86Operand::kMemory)
	{
		emit_address(kAddress, operand);
		assembler_.load(width, reg, x86::RM::mem(kAddress, 0));
		return;
	}
	emit_immediate(reg, operand, width);
}

void Cy86Codegen::emit_store(const Cy86Operand& operand, int reg, int width)
{
	if (operand.kind == Cy86Operand::kRegister)
	{
		assembler_.store(width, x86::RM::reg(x86_register(operand.reg)), reg);
		return;
	}
	emit_address(kAddress, operand);
	assembler_.store(width, x86::RM::mem(kAddress, 0), reg);
}

void Cy86Codegen::emit_statement(const Cy86Statement& statement)
{
	if (statement.is_literal)
	{
		image_ += statement.data;
		return;
	}

	check_operands(statement);
	const Cy86OpcodeInfo& info = *statement.opcode;
	const std::vector<Cy86Operand>& operands = statement.operands;
	const int width = info.width;

	switch (info.family)
	{
	case CY_DATA:
		emit_data(statement);
		return;

	case CY_MOVE:
		emit_load(kResult, operands[1], width);
		emit_store(operands[0], kResult, width);
		return;

	case CY_MOVE80:
		emit_move80(statement);
		return;

	case CY_JUMP:
		emit_indirect(statement, 4);
		return;

	case CY_CALL:
		emit_indirect(statement, 2);
		return;

	case CY_RET:
		assembler_.byte(0xC3);
		return;

	case CY_JUMPIF:
		emit_jumpif(statement);
		return;

	case CY_NOT:
		emit_load(kResult, operands[1], width);
		assembler_.unary(width, 2, x86::RM::reg(kResult));
		emit_store(operands[0], kResult, width);
		return;

	case CY_ALU:
		emit_load(kResult, operands[1], width);
		emit_load(kRight, operands[2], width);
		assembler_.arithmetic(width, info.aux, kResult, x86::RM::reg(kRight));
		emit_store(operands[0], kResult, width);
		return;

	case CY_SHIFT:
		emit_shift(statement);
		return;

	case CY_MUL:
		emit_multiply(statement);
		return;

	case CY_DIV:
		emit_divide(statement);
		return;

	case CY_COMPARE:
		emit_compare(statement);
		return;

	case CY_SYSCALL:
		emit_syscall(statement);
		return;

	case CY_INT_TO_FLOAT80:
		emit_int_to_float80(statement);
		return;

	case CY_FLOAT_TO_FLOAT80:
		emit_float_push(operands[1], width);
		emit_float_pop(operands[0], 80);
		return;

	case CY_FLOAT80_TO_INT:
		emit_float80_to_int(statement);
		return;

	case CY_FLOAT80_TO_FLOAT:
		emit_float_push(operands[1], 80);
		emit_float_pop(operands[0], info.operands[0].width);
		return;

	case CY_FLOAT_ALU:
		emit_float_alu(statement);
		return;

	case CY_FLOAT_COMPARE:
		emit_float_compare(statement);
		return;
	}
}

void Cy86Codegen::emit_data(const Cy86Statement& statement)
{
	const std::size_t size = statement.opcode->width / 8;
	const Cy86Operand& operand = statement.operands[0];
	const std::size_t field = image_.size();
	image_ += immediate_bytes(operand, size);
	if (operand.has_label)
	{
		relocate(field, size, operand.label);
	}
}

void Cy86Codegen::emit_indirect(const Cy86Statement& statement, int digit)
{
	emit_load(kResult, statement.operands[0], 64);
	assembler_.instruction(0, 0xFF, 1, digit, false, x86::RM::reg(kResult));
}

void Cy86Codegen::emit_immediate80(const Cy86Operand& operand, int low, int high)
{
	// No form takes an 80-bit immediate, so one is read as the eight bytes an
	// immediate can carry and the two above them.
	const std::string bytes = immediate_bytes(operand, kMaxOperandBytes);
	const std::size_t field = assembler_.load_immediate(64, low, little_endian(bytes));
	if (operand.has_label)
	{
		relocate(field, 8, operand.label);
	}
	assembler_.load_immediate(16, high, little_endian(bytes.substr(8)));
}

void Cy86Codegen::emit_move80(const Cy86Statement& statement)
{
	// Ten bytes move as eight plus two rather than through the x87 stack,
	// which would normalize whatever it loaded.
	const Cy86Operand& source = statement.operands[1];
	if (source.kind == Cy86Operand::kMemory)
	{
		emit_address(kAddress, source);
		assembler_.load(64, kResult, x86::RM::mem(kAddress, 0));
		assembler_.load(16, kRight, x86::RM::mem(kAddress, 8));
	}
	else
	{
		emit_immediate80(source, kResult, kRight);
	}

	emit_address(kAddress, statement.operands[0]);
	assembler_.store(64, x86::RM::mem(kAddress, 0), kResult);
	assembler_.store(16, x86::RM::mem(kAddress, 8), kRight);
}

void Cy86Codegen::emit_jumpif(const Cy86Statement& statement)
{
	emit_load(kResult, statement.operands[0], 8);
	emit_load(kRight, statement.operands[1], 64);
	assembler_.instruction(8, 0x84, 1, kResult, true, x86::RM::reg(kResult));
	// Two bytes of `jmp rcx` follow.
	assembler_.jump_condition(x86::CC_E, 2);
	assembler_.instruction(0, 0xFF, 1, 4, false, x86::RM::reg(kRight));
}

void Cy86Codegen::emit_shift(const Cy86Statement& statement)
{
	const int width = statement.opcode->width;
	emit_load(kResult, statement.operands[1], width);
	emit_load(kRight, statement.operands[2], 8);
	assembler_.shift_by_cl(width, static_cast<int>(statement.opcode->aux),
	                       x86::RM::reg(kResult));
	emit_store(statement.operands[0], kResult, width);
}

void Cy86Codegen::emit_multiply(const Cy86Statement& statement)
{
	const int width = statement.opcode->width;
	emit_load(kResult, statement.operands[1], width);
	emit_load(kRight, statement.operands[2], width);
	// The low half of a product is the same whether its operands are read as
	// signed or unsigned, so one form serves `smul` and `umul` both.
	if (width == 8)
	{
		assembler_.unary(8, 5, x86::RM::reg(kRight));
	}
	else
	{
		assembler_.instruction(width, 0x0FAF, 2, kResult, true, x86::RM::reg(kRight));
	}
	emit_store(statement.operands[0], kResult, width);
}

void Cy86Codegen::emit_divide(const Cy86Statement& statement)
{
	const int width = statement.opcode->width;
	const bool is_unsigned = (statement.opcode->aux & 1) != 0;
	const bool remainder = (statement.opcode->aux & 2) != 0;
	emit_load(kResult, statement.operands[1], width);
	emit_load(kRight, statement.operands[2], width);

	// Every division is done in 64 bits, whatever width it is written in.  A
	// division at its own width traps when the quotient does not fit that
	// width, which the smallest negative value over -1 does not, and CY86
	// arithmetic wraps rather than traps: `iadd8 x8 x8 x8` of 0x80 is 0x00.  So
	// both operands widen the way the opcode reads them, one form divides, and
	// the store takes the low bits back.  Only a 64-bit division can still
	// trap, and only where no wider form exists to avoid it.
	if (width != 64)
	{
		assembler_.extend(64, width, !is_unsigned, kResult, x86::RM::reg(kResult));
		assembler_.extend(64, width, !is_unsigned, kRight, x86::RM::reg(kRight));
	}
	if (is_unsigned)
	{
		assembler_.arithmetic(32, 0x32, x86::RDX, x86::RM::reg(x86::RDX));
	}
	else
	{
		// CQO, which widens the dividend into rdx.
		assembler_.byte(0x48);
		assembler_.byte(0x99);
	}
	assembler_.unary(64, is_unsigned ? 6 : 7, x86::RM::reg(kRight));

	if (remainder)
	{
		assembler_.load(64, kResult, x86::RM::reg(x86::RDX));
	}
	emit_store(statement.operands[0], kResult, width);
}

void Cy86Codegen::emit_compare(const Cy86Statement& statement)
{
	const int width = statement.opcode->width;
	emit_load(kResult, statement.operands[1], width);
	emit_load(kRight, statement.operands[2], width);
	assembler_.arithmetic(width, 0x3A, kResult, x86::RM::reg(kRight));
	assembler_.set_condition(static_cast<x86::Condition>(statement.opcode->aux),
	                         x86::RM::reg(kResult));
	emit_store(statement.operands[0], kResult, 8);
}

void Cy86Codegen::emit_syscall(const Cy86Statement& statement)
{
	const std::size_t count = statement.opcode->aux;
	emit_load(kResult, statement.operands[1], 64);
	for (std::size_t index = 0; index < count; ++index)
	{
		emit_load(kArgumentRegisters[index], statement.operands[index + 2], 64);
	}
	assembler_.byte(0x0F);
	assembler_.byte(0x05);
	// The kernel returns in rax and clobbers rcx and r11, none of which backs
	// a CY86 register, so the address of the result can be built after it.
	emit_store(statement.operands[0], kResult, 64);
}

void Cy86Codegen::emit_spill(const Cy86Operand& operand, int width, long long slot)
{
	if (width != 80)
	{
		emit_load(kResult, operand, width);
		assembler_.store(width, x86::RM::mem(x86::RSP, slot), kResult);
		return;
	}
	// Both scratch registers are free here: an x87 form holds what it has
	// already read on the floating stack rather than in one of them.
	emit_immediate80(operand, kResult, kRight);
	assembler_.store(64, x86::RM::mem(x86::RSP, slot), kResult);
	assembler_.store(16, x86::RM::mem(x86::RSP, slot + 8), kRight);
}

void Cy86Codegen::emit_float_push(const Cy86Operand& operand, int width)
{
	const FloatForm form = float_load(width);
	if (operand.kind == Cy86Operand::kMemory)
	{
		emit_address(kAddress, operand);
		assembler_.x87_memory(form.escape, form.digit, x86::RM::mem(kAddress, 0));
		return;
	}
	emit_spill(operand, width, kSlotA);
	assembler_.x87_memory(form.escape, form.digit, x86::RM::mem(x86::RSP, kSlotA));
}

void Cy86Codegen::emit_float_pop(const Cy86Operand& operand, int width)
{
	const FloatForm form = float_store(width);
	if (operand.kind == Cy86Operand::kMemory)
	{
		emit_address(kAddress, operand);
		assembler_.x87_memory(form.escape, form.digit, x86::RM::mem(kAddress, 0));
		return;
	}
	assembler_.x87_memory(form.escape, form.digit, x86::RM::mem(x86::RSP, kSlotA));
	assembler_.load(width, kResult, x86::RM::mem(x86::RSP, kSlotA));
	emit_store(operand, kResult, width);
}

void Cy86Codegen::emit_float_alu(const Cy86Statement& statement)
{
	const int width = statement.opcode->width;
	emit_float_push(statement.operands[1], width);
	emit_float_push(statement.operands[2], width);
	// The DE forms operate on st(1) with st(0) and pop, which leaves the
	// result where the first operand was pushed.
	assembler_.byte(0xDE);
	assembler_.byte(statement.opcode->aux);
	emit_float_pop(statement.operands[0], width);
}

void Cy86Codegen::emit_float_compare(const Cy86Statement& statement)
{
	const int width = statement.opcode->width;
	// Pushed second first, so that st(0) holds the left operand and FCOMIP
	// compares them in the order written.
	emit_float_push(statement.operands[2], width);
	emit_float_push(statement.operands[1], width);
	assembler_.byte(0xDF);
	assembler_.byte(0xF1);
	// FCOMIP pops only its own operand; the other is dropped without touching
	// the flags it just set.
	assembler_.byte(0xDD);
	assembler_.byte(0xD8);
	assembler_.set_condition(static_cast<x86::Condition>(statement.opcode->aux),
	                         x86::RM::reg(kResult));
	emit_store(statement.operands[0], kResult, 8);
}

void Cy86Codegen::emit_two_to_63(long long slot)
{
	// 2^63 as an 80-bit float: the exponent 16383 + 63 and an all but leading
	// zero significand.
	assembler_.load_immediate(64, kRight, 0x8000000000000000ULL);
	assembler_.store(64, x86::RM::mem(x86::RSP, slot), kRight);
	assembler_.load_immediate(16, kRight, 0x403E);
	assembler_.store(16, x86::RM::mem(x86::RSP, slot + 8), kRight);
}

void Cy86Codegen::emit_int_to_float80(const Cy86Statement& statement)
{
	const Cy86OperandSpec& spec = statement.opcode->operands[1];
	const int width = spec.width;
	const bool is_signed = spec.sign == CY_SIGN_SIGNED;
	emit_load(kResult, statement.operands[1], width);
	if (width != 64)
	{
		assembler_.extend(64, width, is_signed, kResult, x86::RM::reg(kResult));
	}

	// FILD reads a signed integer, so an unsigned 64-bit value is biased down
	// by 2^63 first - which is exactly what flipping its high bit does - and
	// the bias is added back as a float.
	const bool biased = !is_signed && width == 64;
	if (biased)
	{
		assembler_.load_immediate(64, kRight, 0x8000000000000000ULL);
		assembler_.arithmetic(64, 0x32, kResult, x86::RM::reg(kRight));
	}
	assembler_.store(64, x86::RM::mem(x86::RSP, kSlotA), kResult);
	assembler_.x87_memory(kIntegerLoad.escape, kIntegerLoad.digit,
	                      x86::RM::mem(x86::RSP, kSlotA));
	if (biased)
	{
		emit_two_to_63(kSlotB);
		const FloatForm load80 = float_load(80);
		assembler_.x87_memory(load80.escape, load80.digit, x86::RM::mem(x86::RSP, kSlotB));
		assembler_.byte(0xDE);
		assembler_.byte(0xC1);
	}
	emit_float_pop(statement.operands[0], 80);
}

void Cy86Codegen::emit_float80_to_int(const Cy86Statement& statement)
{
	const Cy86OperandSpec& spec = statement.opcode->operands[0];
	const int width = spec.width;
	const bool is_signed = spec.sign == CY_SIGN_SIGNED;
	emit_float_push(statement.operands[1], 80);

	// FISTP writes a signed integer, so an unsigned result is taken from the
	// next width up, and a full 64-bit one is biased down by 2^63 and the bias
	// put back as an integer.
	const bool biased = !is_signed && width == 64;
	if (biased)
	{
		emit_two_to_63(kSlotB);
		const FloatForm load80 = float_load(80);
		assembler_.x87_memory(load80.escape, load80.digit, x86::RM::mem(x86::RSP, kSlotB));
		assembler_.byte(0xDE);
		assembler_.byte(0xE9);
	}
	assembler_.x87_memory(kIntegerStore.escape, kIntegerStore.digit,
	                      x86::RM::mem(x86::RSP, kSlotA));
	assembler_.load(width, kResult, x86::RM::mem(x86::RSP, kSlotA));
	if (biased)
	{
		assembler_.load_immediate(64, kRight, 0x8000000000000000ULL);
		assembler_.arithmetic(64, 0x32, kResult, x86::RM::reg(kRight));
	}
	emit_store(statement.operands[0], kResult, width);
}
