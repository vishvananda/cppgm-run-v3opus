#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// x86-64 machine code, as an encoder rather than as one function per mnemonic.
//
// Every instruction the backend emits has the same shape: an operand size
// prefix, a REX byte, one to three opcode bytes, and a ModRM pair naming a
// register and a register-or-memory operand.  All of the prefixes and the SIB
// byte follow from the operand width and the register numbers, so one
// `instruction` covers every form and the named helpers below are only the
// opcode numbers with a name attached.
//
// Sizes are the reason this is a separate component.  A caller that has not
// resolved its labels yet still needs to know how long an instruction is, so
// nothing here chooses a shorter encoding when a value happens to be small:
// `mov r64, imm64` is ten bytes whatever the immediate holds.  The one pass
// that emits code therefore also fixes every address.

namespace x86
{

// The general purpose registers, numbered as ModRM and REX number them.
enum Register
{
	RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
	R8, R9, R10, R11, R12, R13, R14, R15
};

// The condition codes, as the low nibble of a `Jcc` or `SETcc` opcode.
enum Condition
{
	CC_O = 0x0, CC_NO = 0x1, CC_B = 0x2, CC_AE = 0x3,
	CC_E = 0x4, CC_NE = 0x5, CC_BE = 0x6, CC_A = 0x7,
	CC_S = 0x8, CC_NS = 0x9, CC_P = 0xA, CC_NP = 0xB,
	CC_L = 0xC, CC_GE = 0xD, CC_LE = 0xE, CC_G = 0xF
};

// One ModRM r/m operand: a register, or the memory at `base + disp`.
struct RM
{
	static RM reg(int number);
	static RM mem(int base, long long disp);

	bool is_register;
	int number;        // the register, or the base register of the address
	long long disp;    // memory only
};

class Assembler
{
public:
	explicit Assembler(std::string& code) : code_(&code) {}

	std::size_t offset() const { return code_->size(); }

	void byte(unsigned value);
	// `value`, little-endian, in `size` bytes.
	void immediate(unsigned long long value, int size);

	// One ModRM instruction.  `width` in bits picks the operand size prefix
	// and REX.W; `opcode` holds its `size` bytes in order from the high byte
	// down.  `reg` is the ModRM.reg field, which for a /digit form is part of
	// the opcode rather than a register, which `reg_is_register` says.
	void instruction(int width, unsigned opcode, int opcode_size,
	                 int reg, bool reg_is_register, const RM& rm);

	// MOV r, r/m and MOV r/m, r.
	void load(int width, int reg, const RM& rm);
	void store(int width, const RM& rm, int reg);

	// MOV r, imm.  Returns the offset of the immediate field, which is what a
	// relocation has to patch.
	std::size_t load_immediate(int width, int reg, unsigned long long value);

	// One of the eight ALU operations in its `r <- r op r/m` direction, named
	// by the opcode of its 8-bit form: ADD 0x02, OR 0x0A, AND 0x22, SUB 0x2A,
	// XOR 0x32, CMP 0x3A.
	void arithmetic(int width, unsigned base_opcode, int reg, const RM& rm);

	// The F6 / F7 group: NOT /2, NEG /3, MUL /4, IMUL /5, DIV /6, IDIV /7.
	void unary(int width, int digit, const RM& rm);

	// The D2 / D3 group, shifting by CL: SHL /4, SHR /5, SAR /7.
	void shift_by_cl(int width, int digit, const RM& rm);

	// MOVZX / MOVSX from `from_width` into a `to_width` register.
	void extend(int to_width, int from_width, bool is_signed, int reg, const RM& rm);

	void set_condition(Condition condition, const RM& rm);
	// Jcc over `displacement` bytes of following code.
	void jump_condition(Condition condition, int displacement);

	// The x87 escapes: `escape` is the D8..DF opcode byte and `digit` the
	// ModRM.reg field its memory forms use.
	void x87_memory(unsigned escape, int digit, const RM& rm);

private:
	std::string* code_;
};

}
