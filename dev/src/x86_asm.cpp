#include "x86_asm.h"

namespace x86
{

namespace
{

// Whether a displacement fits the one-byte form of a ModRM address.
bool fits_signed_byte(long long value)
{
	return value >= -128 && value <= 127;
}

// The REX bits a register number contributes, given where it sits: REX.R for
// the ModRM.reg field, REX.B for the r/m field.
int extension_bit(int number, int bit)
{
	return number >= 8 ? bit : 0;
}

// A byte operand names SPL, BPL, SIL or DIL rather than AH, CH, DH or BH only
// when a REX byte is present, so an 8-bit form that reaches those four has to
// carry one even when it is otherwise empty.
bool needs_byte_prefix(int width, int number)
{
	return width == 8 && number >= 4 && number < 8;
}

}

RM RM::reg(int number)
{
	RM rm;
	rm.is_register = true;
	rm.number = number;
	rm.disp = 0;
	return rm;
}

RM RM::mem(int base, long long disp)
{
	RM rm;
	rm.is_register = false;
	rm.number = base;
	rm.disp = disp;
	return rm;
}

void Assembler::byte(unsigned value)
{
	code_->push_back(static_cast<char>(value & 0xFF));
}

void Assembler::immediate(unsigned long long value, int size)
{
	for (int index = 0; index < size; ++index)
	{
		byte(static_cast<unsigned>(value & 0xFF));
		value >>= 8;
	}
}

void Assembler::instruction(int width, unsigned opcode, int opcode_size,
                            int reg, bool reg_is_register, const RM& rm)
{
	if (width == 16)
	{
		byte(0x66);
	}

	int rex = 0;
	if (width == 64)
	{
		rex |= 0x08;
	}
	rex |= extension_bit(reg, 0x04);
	rex |= extension_bit(rm.number, 0x01);
	const bool byte_reg = reg_is_register && needs_byte_prefix(width, reg);
	const bool byte_rm = rm.is_register && needs_byte_prefix(width, rm.number);
	if (rex != 0 || byte_reg || byte_rm)
	{
		byte(0x40 | rex);
	}

	for (int index = opcode_size; index-- > 0; )
	{
		byte(opcode >> (8 * index));
	}

	const int reg_field = (reg & 7) << 3;
	if (rm.is_register)
	{
		byte(0xC0 | reg_field | (rm.number & 7));
		return;
	}

	// A base of RBP or R13 has no displacement-free form, and a base of RSP or
	// R12 always needs the SIB byte that its r/m encoding stands for.
	const int base = rm.number & 7;
	const int mod = (rm.disp == 0 && base != 5) ? 0 : (fits_signed_byte(rm.disp) ? 1 : 2);
	byte((mod << 6) | reg_field | base);
	if (base == 4)
	{
		byte(0x24);
	}
	if (mod == 1)
	{
		immediate(static_cast<unsigned long long>(rm.disp), 1);
	}
	else if (mod == 2)
	{
		immediate(static_cast<unsigned long long>(rm.disp), 4);
	}
}

void Assembler::load(int width, int reg, const RM& rm)
{
	instruction(width, width == 8 ? 0x8A : 0x8B, 1, reg, true, rm);
}

void Assembler::store(int width, const RM& rm, int reg)
{
	instruction(width, width == 8 ? 0x88 : 0x89, 1, reg, true, rm);
}

std::size_t Assembler::load_immediate(int width, int reg, unsigned long long value)
{
	if (width == 16)
	{
		byte(0x66);
	}
	int rex = width == 64 ? 0x08 : 0;
	rex |= extension_bit(reg, 0x01);
	if (rex != 0 || needs_byte_prefix(width, reg))
	{
		byte(0x40 | rex);
	}
	byte((width == 8 ? 0xB0 : 0xB8) + (reg & 7));

	const std::size_t field = code_->size();
	immediate(value, width / 8);
	return field;
}

void Assembler::arithmetic(int width, unsigned base_opcode, int reg, const RM& rm)
{
	instruction(width, width == 8 ? base_opcode : base_opcode + 1, 1, reg, true, rm);
}

void Assembler::unary(int width, int digit, const RM& rm)
{
	instruction(width, width == 8 ? 0xF6 : 0xF7, 1, digit, false, rm);
}

void Assembler::shift_by_cl(int width, int digit, const RM& rm)
{
	instruction(width, width == 8 ? 0xD2 : 0xD3, 1, digit, false, rm);
}

void Assembler::extend(int to_width, int from_width, bool is_signed, int reg, const RM& rm)
{
	// A 32-bit source has no MOVZX: writing a 32-bit register already clears
	// the upper half, so a plain MOV is the zero extension.
	if (from_width == 32 && !is_signed)
	{
		load(32, reg, rm);
		return;
	}
	if (from_width == 32)
	{
		instruction(to_width, 0x63, 1, reg, true, rm);
		return;
	}
	const unsigned base = is_signed ? 0x0FBE : 0x0FB6;
	instruction(to_width, from_width == 8 ? base : base + 1, 2, reg, true, rm);
}

void Assembler::set_condition(Condition condition, const RM& rm)
{
	instruction(8, 0x0F90 + condition, 2, 0, false, rm);
}

void Assembler::jump_condition(Condition condition, int displacement)
{
	byte(0x70 + condition);
	byte(static_cast<unsigned>(displacement));
}

void Assembler::x87_memory(unsigned escape, int digit, const RM& rm)
{
	instruction(0, escape, 1, digit, false, rm);
}

}
