#include "cy86_model.h"

#include <cstring>

#include "x86_asm.h"

namespace
{

// The operand constraints of the assignment, verbatim from the handout's
// machine readable list, each followed by how the opcode lowers.  Keeping the
// descriptors as the text they are written in means the table is checkable
// against the handout by reading it, and costs one parse of 170 short strings
// once per run.
struct Cy86OpcodeRow
{
	const char* descriptor;
	Cy86Family family;
	unsigned aux;
};

const Cy86OpcodeRow kOpcodes[] =
{
	{ "data8 rI8",                                CY_DATA,              0 },
	{ "data16 rI16",                              CY_DATA,              0 },
	{ "data32 rI32",                              CY_DATA,              0 },
	{ "data64 rI64",                              CY_DATA,              0 },
	{ "move8 w8 r8",                              CY_MOVE,              0 },
	{ "move16 w16 r16",                           CY_MOVE,              0 },
	{ "move32 w32 r32",                           CY_MOVE,              0 },
	{ "move64 w64 r64",                           CY_MOVE,              0 },
	{ "move80 w80 r80",                           CY_MOVE80,            0 },
	{ "jump ar64",                                CY_JUMP,              0 },
	{ "jumpif br8 ar64",                          CY_JUMPIF,            0 },
	{ "call ar64",                                CY_CALL,              0 },
	{ "ret",                                      CY_RET,               0 },
	{ "not8 w8 r8",                               CY_NOT,               0 },
	{ "not16 w16 r16",                            CY_NOT,               0 },
	{ "not32 w32 r32",                            CY_NOT,               0 },
	{ "not64 w64 r64",                            CY_NOT,               0 },
	{ "and8 w8 r8 r8",                            CY_ALU,               0x22 },
	{ "and16 w16 r16 r16",                        CY_ALU,               0x22 },
	{ "and32 w32 r32 r32",                        CY_ALU,               0x22 },
	{ "and64 w64 r64 r64",                        CY_ALU,               0x22 },
	{ "or8 w8 r8 r8",                             CY_ALU,               0x0A },
	{ "or16 w16 r16 r16",                         CY_ALU,               0x0A },
	{ "or32 w32 r32 r32",                         CY_ALU,               0x0A },
	{ "or64 w64 r64 r64",                         CY_ALU,               0x0A },
	{ "xor8 w8 r8 r8",                            CY_ALU,               0x32 },
	{ "xor16 w16 r16 r16",                        CY_ALU,               0x32 },
	{ "xor32 w32 r32 r32",                        CY_ALU,               0x32 },
	{ "xor64 w64 r64 r64",                        CY_ALU,               0x32 },
	{ "lshift8 iw8 ir8 ur8",                      CY_SHIFT,             4 },
	{ "lshift16 iw16 ir16 ur8",                   CY_SHIFT,             4 },
	{ "lshift32 iw32 ir32 ur8",                   CY_SHIFT,             4 },
	{ "lshift64 iw64 ir64 ur8",                   CY_SHIFT,             4 },
	{ "srshift8 sw8 sr8 ur8",                     CY_SHIFT,             7 },
	{ "srshift16 sw16 sr16 ur8",                  CY_SHIFT,             7 },
	{ "srshift32 sw32 sr32 ur8",                  CY_SHIFT,             7 },
	{ "srshift64 sw64 sr64 ur8",                  CY_SHIFT,             7 },
	{ "urshift8 uw8 ur8 ur8",                     CY_SHIFT,             5 },
	{ "urshift16 uw16 ur16 ur8",                  CY_SHIFT,             5 },
	{ "urshift32 uw32 ur32 ur8",                  CY_SHIFT,             5 },
	{ "urshift64 uw64 ur64 ur8",                  CY_SHIFT,             5 },
	{ "s8convf80 fw80 sr8",                       CY_INT_TO_FLOAT80,    0 },
	{ "s16convf80 fw80 sr16",                     CY_INT_TO_FLOAT80,    0 },
	{ "s32convf80 fw80 sr32",                     CY_INT_TO_FLOAT80,    0 },
	{ "s64convf80 fw80 sr64",                     CY_INT_TO_FLOAT80,    0 },
	{ "u8convf80 fw80 ur8",                       CY_INT_TO_FLOAT80,    0 },
	{ "u16convf80 fw80 ur16",                     CY_INT_TO_FLOAT80,    0 },
	{ "u32convf80 fw80 ur32",                     CY_INT_TO_FLOAT80,    0 },
	{ "u64convf80 fw80 ur64",                     CY_INT_TO_FLOAT80,    0 },
	{ "f32convf80 fw80 fr32",                     CY_FLOAT_TO_FLOAT80,  0 },
	{ "f64convf80 fw80 fr64",                     CY_FLOAT_TO_FLOAT80,  0 },
	{ "f80convs8 sw8 fr80",                       CY_FLOAT80_TO_INT,    0 },
	{ "f80convs16 sw16 fr80",                     CY_FLOAT80_TO_INT,    0 },
	{ "f80convs32 sw32 fr80",                     CY_FLOAT80_TO_INT,    0 },
	{ "f80convs64 sw64 fr80",                     CY_FLOAT80_TO_INT,    0 },
	{ "f80convu8 uw8 fr80",                       CY_FLOAT80_TO_INT,    0 },
	{ "f80convu16 uw16 fr80",                     CY_FLOAT80_TO_INT,    0 },
	{ "f80convu32 uw32 fr80",                     CY_FLOAT80_TO_INT,    0 },
	{ "f80convu64 uw64 fr80",                     CY_FLOAT80_TO_INT,    0 },
	{ "f80convf32 fw32 fr80",                     CY_FLOAT80_TO_FLOAT,  0 },
	{ "f80convf64 fw64 fr80",                     CY_FLOAT80_TO_FLOAT,  0 },
	{ "iadd8 iw8 ir8 ir8",                        CY_ALU,               0x02 },
	{ "iadd16 iw16 ir16 ir16",                    CY_ALU,               0x02 },
	{ "iadd32 iw32 ir32 ir32",                    CY_ALU,               0x02 },
	{ "iadd64 iw64 ir64 ir64",                    CY_ALU,               0x02 },
	{ "fadd32 fw32 fr32 fr32",                    CY_FLOAT_ALU,         0xC1 },
	{ "fadd64 fw64 fr64 fr64",                    CY_FLOAT_ALU,         0xC1 },
	{ "fadd80 fw80 fr80 fr80",                    CY_FLOAT_ALU,         0xC1 },
	{ "isub8 iw8 ir8 ir8",                        CY_ALU,               0x2A },
	{ "isub16 iw16 ir16 ir16",                    CY_ALU,               0x2A },
	{ "isub32 iw32 ir32 ir32",                    CY_ALU,               0x2A },
	{ "isub64 iw64 ir64 ir64",                    CY_ALU,               0x2A },
	{ "fsub32 fw32 fr32 fr32",                    CY_FLOAT_ALU,         0xE9 },
	{ "fsub64 fw64 fr64 fr64",                    CY_FLOAT_ALU,         0xE9 },
	{ "fsub80 fw80 fr80 fr80",                    CY_FLOAT_ALU,         0xE9 },
	{ "smul8 sw8 sr8 sr8",                        CY_MUL,               0 },
	{ "smul16 sw16 sr16 sr16",                    CY_MUL,               0 },
	{ "smul32 sw32 sr32 sr32",                    CY_MUL,               0 },
	{ "smul64 sw64 sr64 sr64",                    CY_MUL,               0 },
	{ "umul8 uw8 ur8 ur8",                        CY_MUL,               0 },
	{ "umul16 uw16 ur16 ur16",                    CY_MUL,               0 },
	{ "umul32 uw32 ur32 ur32",                    CY_MUL,               0 },
	{ "umul64 uw64 ur64 ur64",                    CY_MUL,               0 },
	{ "fmul32 fw32 fr32 fr32",                    CY_FLOAT_ALU,         0xC9 },
	{ "fmul64 fw64 fr64 fr64",                    CY_FLOAT_ALU,         0xC9 },
	{ "fmul80 fw80 fr80 fr80",                    CY_FLOAT_ALU,         0xC9 },
	{ "sdiv8 sw8 sr8 sr8",                        CY_DIV,               0 },
	{ "sdiv16 sw16 sr16 sr16",                    CY_DIV,               0 },
	{ "sdiv32 sw32 sr32 sr32",                    CY_DIV,               0 },
	{ "sdiv64 sw64 sr64 sr64",                    CY_DIV,               0 },
	{ "udiv8 uw8 ur8 ur8",                        CY_DIV,               1 },
	{ "udiv16 uw16 ur16 ur16",                    CY_DIV,               1 },
	{ "udiv32 uw32 ur32 ur32",                    CY_DIV,               1 },
	{ "udiv64 uw64 ur64 ur64",                    CY_DIV,               1 },
	{ "fdiv32 fw32 fr32 fr32",                    CY_FLOAT_ALU,         0xF9 },
	{ "fdiv64 fw64 fr64 fr64",                    CY_FLOAT_ALU,         0xF9 },
	{ "fdiv80 fw80 fr80 fr80",                    CY_FLOAT_ALU,         0xF9 },
	{ "smod8 sw8 sr8 sr8",                        CY_DIV,               2 },
	{ "smod16 sw16 sr16 sr16",                    CY_DIV,               2 },
	{ "smod32 sw32 sr32 sr32",                    CY_DIV,               2 },
	{ "smod64 sw64 sr64 sr64",                    CY_DIV,               2 },
	{ "umod8 uw8 ur8 ur8",                        CY_DIV,               3 },
	{ "umod16 uw16 ur16 ur16",                    CY_DIV,               3 },
	{ "umod32 uw32 ur32 ur32",                    CY_DIV,               3 },
	{ "umod64 uw64 ur64 ur64",                    CY_DIV,               3 },
	{ "ieq8 wb8 ir8 ir8",                         CY_COMPARE,           x86::CC_E },
	{ "ieq16 wb8 ir16 ir16",                      CY_COMPARE,           x86::CC_E },
	{ "ieq32 wb8 ir32 ir32",                      CY_COMPARE,           x86::CC_E },
	{ "ieq64 wb8 ir64 ir64",                      CY_COMPARE,           x86::CC_E },
	{ "feq32 wb8 fr32 fr32",                      CY_FLOAT_COMPARE,     x86::CC_E },
	{ "feq64 wb8 fr64 fr64",                      CY_FLOAT_COMPARE,     x86::CC_E },
	{ "feq80 wb8 fr80 fr80",                      CY_FLOAT_COMPARE,     x86::CC_E },
	{ "ine8 wb8 ir8 ir8",                         CY_COMPARE,           x86::CC_NE },
	{ "ine16 wb8 ir16 ir16",                      CY_COMPARE,           x86::CC_NE },
	{ "ine32 wb8 ir32 ir32",                      CY_COMPARE,           x86::CC_NE },
	{ "ine64 wb8 ir64 ir64",                      CY_COMPARE,           x86::CC_NE },
	{ "fne32 wb8 fr32 fr32",                      CY_FLOAT_COMPARE,     x86::CC_NE },
	{ "fne64 wb8 fr64 fr64",                      CY_FLOAT_COMPARE,     x86::CC_NE },
	{ "fne80 wb8 fr80 fr80",                      CY_FLOAT_COMPARE,     x86::CC_NE },
	{ "slt8 wb8 sr8 sr8",                         CY_COMPARE,           x86::CC_L },
	{ "slt16 wb8 sr16 sr16",                      CY_COMPARE,           x86::CC_L },
	{ "slt32 wb8 sr32 sr32",                      CY_COMPARE,           x86::CC_L },
	{ "slt64 wb8 sr64 sr64",                      CY_COMPARE,           x86::CC_L },
	{ "ult8 wb8 ur8 ur8",                         CY_COMPARE,           x86::CC_B },
	{ "ult16 wb8 ur16 ur16",                      CY_COMPARE,           x86::CC_B },
	{ "ult32 wb8 ur32 ur32",                      CY_COMPARE,           x86::CC_B },
	{ "ult64 wb8 ur64 ur64",                      CY_COMPARE,           x86::CC_B },
	{ "flt32 wb8 fr32 fr32",                      CY_FLOAT_COMPARE,     x86::CC_B },
	{ "flt64 wb8 fr64 fr64",                      CY_FLOAT_COMPARE,     x86::CC_B },
	{ "flt80 wb8 fr80 fr80",                      CY_FLOAT_COMPARE,     x86::CC_B },
	{ "sgt8 wb8 sr8 sr8",                         CY_COMPARE,           x86::CC_G },
	{ "sgt16 wb8 sr16 sr16",                      CY_COMPARE,           x86::CC_G },
	{ "sgt32 wb8 sr32 sr32",                      CY_COMPARE,           x86::CC_G },
	{ "sgt64 wb8 sr64 sr64",                      CY_COMPARE,           x86::CC_G },
	{ "ugt8 wb8 ur8 ur8",                         CY_COMPARE,           x86::CC_A },
	{ "ugt16 wb8 ur16 ur16",                      CY_COMPARE,           x86::CC_A },
	{ "ugt32 wb8 ur32 ur32",                      CY_COMPARE,           x86::CC_A },
	{ "ugt64 wb8 ur64 ur64",                      CY_COMPARE,           x86::CC_A },
	{ "fgt32 wb8 fr32 fr32",                      CY_FLOAT_COMPARE,     x86::CC_A },
	{ "fgt64 wb8 fr64 fr64",                      CY_FLOAT_COMPARE,     x86::CC_A },
	{ "fgt80 wb8 fr80 fr80",                      CY_FLOAT_COMPARE,     x86::CC_A },
	{ "sle8 wb8 sr8 sr8",                         CY_COMPARE,           x86::CC_LE },
	{ "sle16 wb8 sr16 sr16",                      CY_COMPARE,           x86::CC_LE },
	{ "sle32 wb8 sr32 sr32",                      CY_COMPARE,           x86::CC_LE },
	{ "sle64 wb8 sr64 sr64",                      CY_COMPARE,           x86::CC_LE },
	{ "ule8 wb8 ur8 ur8",                         CY_COMPARE,           x86::CC_BE },
	{ "ule16 wb8 ur16 ur16",                      CY_COMPARE,           x86::CC_BE },
	{ "ule32 wb8 ur32 ur32",                      CY_COMPARE,           x86::CC_BE },
	{ "ule64 wb8 ur64 ur64",                      CY_COMPARE,           x86::CC_BE },
	{ "fle32 wb8 fr32 fr32",                      CY_FLOAT_COMPARE,     x86::CC_BE },
	{ "fle64 wb8 fr64 fr64",                      CY_FLOAT_COMPARE,     x86::CC_BE },
	{ "fle80 wb8 fr80 fr80",                      CY_FLOAT_COMPARE,     x86::CC_BE },
	{ "sge8 wb8 sr8 sr8",                         CY_COMPARE,           x86::CC_GE },
	{ "sge16 wb8 sr16 sr16",                      CY_COMPARE,           x86::CC_GE },
	{ "sge32 wb8 sr32 sr32",                      CY_COMPARE,           x86::CC_GE },
	{ "sge64 wb8 sr64 sr64",                      CY_COMPARE,           x86::CC_GE },
	{ "uge8 wb8 ur8 ur8",                         CY_COMPARE,           x86::CC_AE },
	{ "uge16 wb8 ur16 ur16",                      CY_COMPARE,           x86::CC_AE },
	{ "uge32 wb8 ur32 ur32",                      CY_COMPARE,           x86::CC_AE },
	{ "uge64 wb8 ur64 ur64",                      CY_COMPARE,           x86::CC_AE },
	{ "fge32 wb8 fr32 fr32",                      CY_FLOAT_COMPARE,     x86::CC_AE },
	{ "fge64 wb8 fr64 fr64",                      CY_FLOAT_COMPARE,     x86::CC_AE },
	{ "fge80 wb8 fr80 fr80",                      CY_FLOAT_COMPARE,     x86::CC_AE },
	{ "syscall0 w64 r64",                         CY_SYSCALL,           0 },
	{ "syscall1 w64 r64 r64",                     CY_SYSCALL,           1 },
	{ "syscall2 w64 r64 r64 r64",                 CY_SYSCALL,           2 },
	{ "syscall3 w64 r64 r64 r64 r64",             CY_SYSCALL,           3 },
	{ "syscall4 w64 r64 r64 r64 r64 r64",         CY_SYSCALL,           4 },
	{ "syscall5 w64 r64 r64 r64 r64 r64 r64",     CY_SYSCALL,           5 },
	{ "syscall6 w64 r64 r64 r64 r64 r64 r64 r64", CY_SYSCALL,           6 },
};

// One operand descriptor, such as `ir32` or `rI8`.
Cy86OperandSpec parse_operand_spec(const char* text, std::size_t size)
{
	Cy86OperandSpec spec;
	for (std::size_t index = 0; index < size; ++index)
	{
		const char letter = text[index];
		if (letter >= '0' && letter <= '9')
		{
			spec.width = spec.width * 10 + (letter - '0');
			continue;
		}
		switch (letter)
		{
		case 'w': spec.written = true; break;
		case 'I': spec.immediate_only = true; break;
		case 's': spec.sign = CY_SIGN_SIGNED; break;
		case 'u': spec.sign = CY_SIGN_UNSIGNED; break;
		case 'f': spec.sign = CY_SIGN_FLOAT; break;
		default: break;  // `r`, `a` and `b` constrain nothing lowering asks
		}
	}
	return spec;
}

// Splits `descriptor` on its spaces into a name and its operand descriptors.
void parse_descriptor(const char* descriptor, Cy86OpcodeInfo& info)
{
	const char* cursor = descriptor;
	while (*cursor != '\0')
	{
		while (*cursor == ' ')
		{
			++cursor;
		}
		const char* start = cursor;
		while (*cursor != '\0' && *cursor != ' ')
		{
			++cursor;
		}
		if (start == cursor)
		{
			break;
		}
		if (info.name.empty())
		{
			info.name.assign(start, cursor);
		}
		else
		{
			info.operands.push_back(parse_operand_spec(start, cursor - start));
		}
	}

	// The width an operation works in is the width of what it reads.  A
	// comparison writes eight bits whatever it compares, and a data statement
	// only has the operand it places.
	if (info.operands.size() >= 2)
	{
		info.width = info.operands[1].width;
	}
	else if (!info.operands.empty())
	{
		info.width = info.operands[0].width;
	}
}

// The register spellings, in the order `Cy86Register` numbers them.  `sp` and
// `bp` are 64-bit only; the rest have one spelling per width.
const char* const kRegisterLetters = "xyzt";

}

std::string literal_field_bytes(const Cy86Operand& operand, std::size_t size)
{
	const std::size_t kept = operand.data_size < kMaxOperandBytes
		? operand.data_size : kMaxOperandBytes;
	const std::size_t copied = kept < size ? kept : size;
	std::string bytes(reinterpret_cast<const char*>(operand.data), copied);
	if (operand.data_size >= size)
	{
		return bytes;
	}
	// Too short widens: by sign for a signed integer, by zero for a string
	// literal or a floating type, whose bytes an operand narrower than them
	// would have truncated instead.
	const bool negative = operand.data_signed && copied != 0 &&
		(operand.data[copied - 1] & 0x80) != 0;
	bytes.resize(size, negative ? static_cast<char>(0xFF) : 0);
	return bytes;
}

unsigned long long literal_field_value(const Cy86Operand& operand, std::size_t size)
{
	const std::string bytes = literal_field_bytes(operand, size);
	const std::size_t length = bytes.size() < 8 ? bytes.size() : 8;
	unsigned long long value = 0;
	for (std::size_t index = length; index-- > 0; )
	{
		value = (value << 8) | static_cast<unsigned char>(bytes[index]);
	}
	return value;
}

bool lookup_cy86_register(const std::string& text, Cy86Register& reg, int& width)
{
	if (text == "sp" || text == "bp")
	{
		reg = text[0] == 's' ? CY_SP : CY_BP;
		width = 64;
		return true;
	}
	const char* const letter = text.empty() ? 0 : std::strchr(kRegisterLetters, text[0]);
	if (letter == 0 || *letter == '\0')
	{
		return false;
	}
	const std::string suffix = text.substr(1);
	if (suffix == "8")
	{
		width = 8;
	}
	else if (suffix == "16")
	{
		width = 16;
	}
	else if (suffix == "32")
	{
		width = 32;
	}
	else if (suffix == "64")
	{
		width = 64;
	}
	else
	{
		return false;
	}
	reg = static_cast<Cy86Register>(CY_X + (letter - kRegisterLetters));
	return true;
}

Cy86OpcodeTable::Cy86OpcodeTable()
{
	const std::size_t count = sizeof(kOpcodes) / sizeof(kOpcodes[0]);
	entries_.resize(count);
	index_.reserve(count * 2);
	for (std::size_t index = 0; index < count; ++index)
	{
		Cy86OpcodeInfo& info = entries_[index];
		info.family = kOpcodes[index].family;
		info.aux = kOpcodes[index].aux;
		info.width = 0;
		parse_descriptor(kOpcodes[index].descriptor, info);
		index_.insert(std::make_pair(info.name, &info));
	}
}

const Cy86OpcodeInfo* Cy86OpcodeTable::find(const std::string& name) const
{
	const std::unordered_map<std::string, const Cy86OpcodeInfo*>::const_iterator
		found = index_.find(name);
	return found == index_.end() ? 0 : found->second;
}
