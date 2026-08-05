#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cy86_model.h"
#include "x86_asm.h"

// CY86 lowered to an x86-64 program image.
//
// The image is built in one forward pass.  That is possible because no
// encoding chosen here changes size when a label value changes: a label always
// rides in a `mov r64, imm64` or in a data field of a fixed width, so an
// instruction's length is known before any address is.  The pass therefore
// fixes every label address as it goes and a second pass adds the addresses
// into the fields a relocation list recorded, which is linear in the number of
// references rather than in the size of the program.
//
// Registers follow the assignment's suggestion: the CY86 registers are backed
// by rsp, rbp and r12 to r15, which no instruction emitted here and no system
// call clobbers.  Everything else is scratch: rax carries the first value read
// and the result, rcx the second (so a shift count is already in cl), rdx the
// high half of a multiply or divide, and r11 an address being computed.  The
// red zone holds the two spill slots the x87 forms need.
class Cy86Codegen
{
public:
	Cy86Codegen(const Cy86Program& program, NameTable& names);

	// Throws `SourceError` when the program is well formed as a token sequence
	// but not as a program: a mistyped operand, or a label with no statement.
	void run();

	const std::string& image() const { return image_; }
	// Where the image loads, which is also where it is entered: the prologue
	// that gives the CY86 registers their initial values opens it and jumps to
	// whichever statement the program starts at.
	unsigned long long base() const;

private:
	// A field of the image holding a constant that a label address is still to
	// be added into.
	struct Relocation
	{
		std::size_t offset;
		std::size_t size;
		NameId label;
	};

	// Layout.
	void emit_prologue();
	void emit_exit();
	void emit_statement(const Cy86Statement& statement);
	void pad_to(std::size_t alignment);
	void resolve();
	unsigned long long address_of(NameId label) const;
	void relocate(std::size_t offset, std::size_t size, NameId label);

	// Operands.
	void check_operands(const Cy86Statement& statement);
	// The bytes an operand contributes to a field of `size`, which for a label
	// reference is the constant part its address is still to be added to.
	std::string immediate_bytes(const Cy86Operand& operand, std::size_t size) const;
	// A constant into a register, recording the field when a label address has
	// still to be added into it.  Every constant an instruction reads goes
	// through here, so a reference is recorded exactly where one is emitted.
	void emit_immediate(int reg, const Cy86Operand& operand, int width);
	void emit_immediate80(const Cy86Operand& operand, int low, int high);
	void emit_address(int reg, const Cy86Operand& operand);
	void emit_load(int reg, const Cy86Operand& operand, int width);
	void emit_store(const Cy86Operand& operand, int reg, int width);

	// The integer and control transfer forms.
	void emit_data(const Cy86Statement& statement);
	void emit_move80(const Cy86Statement& statement);
	void emit_jumpif(const Cy86Statement& statement);
	void emit_shift(const Cy86Statement& statement);
	void emit_multiply(const Cy86Statement& statement);
	void emit_divide(const Cy86Statement& statement);
	void emit_compare(const Cy86Statement& statement);
	void emit_syscall(const Cy86Statement& statement);
	void emit_indirect(const Cy86Statement& statement, int digit);

	// The x87 forms.
	void emit_float_push(const Cy86Operand& operand, int width);
	void emit_float_pop(const Cy86Operand& operand, int width);
	void emit_spill(const Cy86Operand& operand, int width, long long slot);
	void emit_float_alu(const Cy86Statement& statement);
	void emit_float_compare(const Cy86Statement& statement);
	void emit_int_to_float80(const Cy86Statement& statement);
	void emit_float80_to_int(const Cy86Statement& statement);
	void emit_two_to_63(long long slot);

	const Cy86Program* program_;
	NameTable* names_;

	std::string image_;
	x86::Assembler assembler_;
	std::vector<Relocation> relocations_;
	// Where each label sits, indexed by `NameId` so a reference costs a load,
	// and long enough for every label the parser found.
	std::vector<unsigned long long> addresses_;
	// The statement the program starts at, and the prologue field holding it.
	unsigned long long entry_;
	std::size_t entry_field_;
};
