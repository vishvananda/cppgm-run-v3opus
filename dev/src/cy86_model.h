#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "name_table.h"

// The CY86 mock intermediate language of PA9: what an opcode asks of its
// operands, what an operand is, and what a program is made of.
//
// CY86 is a regular language, so nothing here is a tree.  A program is a
// sequence of statements, a statement is an opcode with its operands or a run
// of literal bytes, and an operand is one of three flat shapes.  The facts a
// later phase asks for - the width and signedness an operand has to have, the
// label a value is relative to, the alignment a datum needs - are decided once
// while parsing, so lowering reads facts rather than deriving them again.

// The CY86 registers.  `sp` and `bp` are the address registers and have no
// narrower aliases; the other four are 64-bit registers whose 32, 16 and 8-bit
// spellings alias their low bits.
enum Cy86Register
{
	CY_SP, CY_BP, CY_X, CY_Y, CY_Z, CY_T, kCy86RegisterCount
};

// True when `text` spells a register, and which one and how wide it is.
bool lookup_cy86_register(const std::string& text, Cy86Register& reg, int& width);

// What an operand descriptor of `cy86-opcode.desc` asks for.  The letters are
// the handout's: `w` written to, `r` read from, `a` an address, `b` a boolean,
// `I` an immediate only, `s`/`u`/`f` the signedness, and a width in bits.
enum Cy86Sign
{
	CY_SIGN_ANY, CY_SIGN_SIGNED, CY_SIGN_UNSIGNED, CY_SIGN_FLOAT
};

struct Cy86OperandSpec
{
	Cy86OperandSpec()
		: written(false), immediate_only(false), sign(CY_SIGN_ANY), width(0)
	{}

	bool written;
	bool immediate_only;
	Cy86Sign sign;
	int width;
};

// How an opcode lowers.  One family stands for every opcode that becomes the
// same x86 shape, so the width and the auxiliary opcode number carry what is
// left of the difference between them.
enum Cy86Family
{
	CY_DATA,          // data8 .. data64
	CY_MOVE,          // move8 .. move64
	CY_MOVE80,
	CY_JUMP, CY_JUMPIF, CY_CALL, CY_RET,
	CY_NOT,
	CY_ALU,           // aux: the 8-bit opcode of the x86 ALU operation
	CY_SHIFT,         // aux: the ModRM digit of the x86 shift group
	CY_MUL,
	CY_DIV,           // aux: bit 0 unsigned, bit 1 remainder
	CY_COMPARE,       // aux: the x86 condition code
	CY_SYSCALL,       // aux: the number of system call arguments
	CY_INT_TO_FLOAT80,
	CY_FLOAT_TO_FLOAT80,
	CY_FLOAT80_TO_INT,
	CY_FLOAT80_TO_FLOAT,
	CY_FLOAT_ALU,     // aux: the second byte of the x87 DE form
	CY_FLOAT_COMPARE  // aux: the x86 condition code
};

struct Cy86OpcodeInfo
{
	std::string name;
	Cy86Family family;
	unsigned aux;
	std::vector<Cy86OperandSpec> operands;
	// The width the operation itself works in, which is the width of the
	// first operand read rather than of the operand written: a comparison
	// writes 8 bits whatever it compares.
	int width;
};

// The operand constraints of the assignment, parsed once and indexed by
// spelling.  One table serves a whole run, so a program of any size costs one
// build of it.
class Cy86OpcodeTable
{
public:
	Cy86OpcodeTable();

	// Null when `name` is not an opcode.
	const Cy86OpcodeInfo* find(const std::string& name) const;

	// Whether `name` is a spelling a label may not take, which the assignment
	// makes the opcode and register spellings.
	bool reserved(const std::string& name) const;

private:
	// The entries keep their addresses, so the index points into them.
	std::vector<Cy86OpcodeInfo> entries_;
	std::unordered_map<std::string, const Cy86OpcodeInfo*> index_;
};

// One operand.  A register names a CY86 register; an immediate and a memory
// address share the same value shape, which is either a label plus a constant
// or the bytes of a literal.
struct Cy86Operand
{
	enum Kind { kRegister, kImmediate, kMemory };

	Cy86Operand()
		: kind(kRegister), reg(CY_SP), width(0), has_base(false)
		, has_label(false), label(kNoName), addend(0), data_signed(false)
	{}

	Kind kind;
	Cy86Register reg;   // kRegister, or the base register of a kMemory address
	int width;          // kRegister: the width the spelling names
	bool has_base;      // kMemory: whether the address has a register base

	bool has_label;
	NameId label;
	unsigned long long addend;  // has_label or has_base: the constant part
	std::string data;           // otherwise: the PA2 bytes of the literal
	bool data_signed;           // whether those bytes widen by sign
};

// One statement: either a run of literal bytes, or an opcode with operands.
struct Cy86Statement
{
	Cy86Statement()
		: is_literal(false), align(1), opcode(0)
	{}

	std::vector<NameId> labels;

	bool is_literal;
	std::string data;      // is_literal: the bytes it places
	std::size_t align;     // is_literal: the alignment 2.14 gives its type

	const Cy86OpcodeInfo* opcode;
	std::vector<Cy86Operand> operands;
};

// A whole CY86 program: the concatenated token sequences of every translation
// unit, parsed.
struct Cy86Program
{
	std::vector<Cy86Statement> statements;
	// Which statement each label names.  A label is a `NameId`, so this
	// answers a reference without a string compare.
	std::unordered_map<NameId, std::size_t> labels;
};
