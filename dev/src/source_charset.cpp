#include "source_charset.h"

namespace
{

struct CodePointRange
{
	int first;
	int last;
};

// See C++ standard 2.11 Identifiers and Appendix/Annex E.1
const CodePointRange kAnnexE1Allowed[] =
{
	{0xA8,0xA8},
	{0xAA,0xAA},
	{0xAD,0xAD},
	{0xAF,0xAF},
	{0xB2,0xB5},
	{0xB7,0xBA},
	{0xBC,0xBE},
	{0xC0,0xD6},
	{0xD8,0xF6},
	{0xF8,0xFF},
	{0x100,0x167F},
	{0x1681,0x180D},
	{0x180F,0x1FFF},
	{0x200B,0x200D},
	{0x202A,0x202E},
	{0x203F,0x2040},
	{0x2054,0x2054},
	{0x2060,0x206F},
	{0x2070,0x218F},
	{0x2460,0x24FF},
	{0x2776,0x2793},
	{0x2C00,0x2DFF},
	{0x2E80,0x2FFF},
	{0x3004,0x3007},
	{0x3021,0x302F},
	{0x3031,0x303F},
	{0x3040,0xD7FF},
	{0xF900,0xFD3D},
	{0xFD40,0xFDCF},
	{0xFDF0,0xFE44},
	{0xFE47,0xFFFD},
	{0x10000,0x1FFFD},
	{0x20000,0x2FFFD},
	{0x30000,0x3FFFD},
	{0x40000,0x4FFFD},
	{0x50000,0x5FFFD},
	{0x60000,0x6FFFD},
	{0x70000,0x7FFFD},
	{0x80000,0x8FFFD},
	{0x90000,0x9FFFD},
	{0xA0000,0xAFFFD},
	{0xB0000,0xBFFFD},
	{0xC0000,0xCFFFD},
	{0xD0000,0xDFFFD},
	{0xE0000,0xEFFFD}
};

// See C++ standard 2.11 Identifiers and Appendix/Annex E.2
const CodePointRange kAnnexE2DisallowedInitially[] =
{
	{0x300,0x36F},
	{0x1DC0,0x1DFF},
	{0x20D0,0x20FF},
	{0xFE20,0xFE2F}
};

template <std::size_t N>
bool in_ranges(const CodePointRange (&ranges)[N], int code_point)
{
	std::size_t low = 0;
	std::size_t high = N;
	while (low < high)
	{
		const std::size_t middle = low + (high - low) / 2;
		if (code_point < ranges[middle].first)
		{
			high = middle;
		}
		else if (code_point > ranges[middle].last)
		{
			low = middle + 1;
		}
		else
		{
			return true;
		}
	}
	return false;
}

} // namespace

Utf8Decoded decode_utf8_sequence(const char* bytes, std::size_t size)
{
	Utf8Decoded result = { 0, 0 };
	if (size == 0)
	{
		return result;
	}

	const unsigned char lead = static_cast<unsigned char>(bytes[0]);
	if (lead < 0x80)
	{
		result.code_point = static_cast<int>(lead);
		result.length = 1;
		return result;
	}

	std::size_t length = 0;
	int value = 0;
	if ((lead & 0xE0) == 0xC0)
	{
		length = 2;
		value = lead & 0x1F;
	}
	else if ((lead & 0xF0) == 0xE0)
	{
		length = 3;
		value = lead & 0x0F;
	}
	else if ((lead & 0xF8) == 0xF0)
	{
		length = 4;
		value = lead & 0x07;
	}
	else
	{
		return result;
	}

	if (size < length)
	{
		return result;
	}
	for (std::size_t index = 1; index < length; ++index)
	{
		const unsigned char unit = static_cast<unsigned char>(bytes[index]);
		if ((unit & 0xC0) != 0x80)
		{
			return result;
		}
		value = (value << 6) | (unit & 0x3F);
	}

	// Reject non shortest form encodings so that each code point has exactly
	// one spelling in the source file.
	static const int kSmallestForLength[5] = { 0, 0, 0x80, 0x800, 0x10000 };
	if (value < kSmallestForLength[length] || !is_valid_code_point(value))
	{
		return result;
	}

	result.code_point = value;
	result.length = length;
	return result;
}

void append_utf8_sequence(std::string& out, int code_point)
{
	const unsigned int value = static_cast<unsigned int>(code_point);
	if (value < 0x80)
	{
		out.push_back(static_cast<char>(value));
		return;
	}
	if (value < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (value >> 6)));
		out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
		return;
	}
	if (value < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (value >> 12)));
		out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
		return;
	}
	out.push_back(static_cast<char>(0xF0 | (value >> 18)));
	out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
	out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
	out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
}

bool is_annex_e1(int code_point)
{
	return in_ranges(kAnnexE1Allowed, code_point);
}

bool is_annex_e2(int code_point)
{
	return in_ranges(kAnnexE2DisallowedInitially, code_point);
}
