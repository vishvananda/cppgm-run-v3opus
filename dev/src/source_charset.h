#pragma once

#include <cstddef>
#include <string>

// Course-defined source and execution character set support.
//
// The course defines both character sets as UTF-8, so every translation phase
// works on Unicode code points stored in int.  This module owns the two facts
// that all later phases share: how a code point is transcoded to and from
// UTF-8 code units, and which code point classes the lexical grammar of
// [lex.charset], [lex.name] and [lex.ppnumber] is written against.
//
// Every phase asks these questions once per character, so the classes that
// answer from the code point alone are defined here and only the Annex E range
// search is out of line.

const int kEndOfFile = -1;
const int kMaxCodePoint = 0x10FFFF;

struct Utf8Decoded
{
	int code_point;
	std::size_t length;
};

// A Unicode scalar value: in range and outside the surrogate block.
inline bool is_valid_code_point(int code_point)
{
	if (code_point < 0 || code_point > kMaxCodePoint)
	{
		return false;
	}
	return code_point < 0xD800 || code_point > 0xDFFF;
}

// Decodes one UTF-8 code unit sequence from the front of [bytes, bytes+size).
// length is zero when the sequence is truncated, is not in shortest form, or
// designates a value that is not a Unicode scalar value.  The inline wrapper is
// the single code unit case, which is nearly every character of a source file.
Utf8Decoded decode_utf8_sequence(const char* bytes, std::size_t size);

inline Utf8Decoded decode_utf8(const char* bytes, std::size_t size)
{
	if (size != 0 && static_cast<unsigned char>(bytes[0]) < 0x80)
	{
		const Utf8Decoded result = { static_cast<int>(bytes[0]), 1 };
		return result;
	}
	return decode_utf8_sequence(bytes, size);
}

// Appends the UTF-8 code unit sequence for code_point to out.
void append_utf8_sequence(std::string& out, int code_point);

inline void append_utf8(std::string& out, int code_point)
{
	if (static_cast<unsigned int>(code_point) < 0x80)
	{
		out.push_back(static_cast<char>(code_point));
		return;
	}
	append_utf8_sequence(out, code_point);
}

// Whitespace other than the line feed, which the token grammar treats as its
// own new-line token rather than as part of a whitespace-sequence.
inline bool is_basic_whitespace(int code_point)
{
	switch (code_point)
	{
	case ' ':
	case '\t':
	case '\v':
	case '\f':
	case '\r':
		return true;
	default:
		return false;
	}
}

inline bool is_digit(int code_point)
{
	return code_point >= '0' && code_point <= '9';
}

inline bool is_octal_digit(int code_point)
{
	return code_point >= '0' && code_point <= '7';
}

inline bool is_hex_digit(int code_point)
{
	if (is_digit(code_point))
	{
		return true;
	}
	if (code_point >= 'a' && code_point <= 'f')
	{
		return true;
	}
	return code_point >= 'A' && code_point <= 'F';
}

inline int hex_digit_value(int code_point)
{
	if (is_digit(code_point))
	{
		return code_point - '0';
	}
	if (code_point >= 'a' && code_point <= 'f')
	{
		return code_point - 'a' + 10;
	}
	return code_point - 'A' + 10;
}

inline bool is_nondigit(int code_point)
{
	if (code_point >= 'a' && code_point <= 'z')
	{
		return true;
	}
	if (code_point >= 'A' && code_point <= 'Z')
	{
		return true;
	}
	return code_point == '_';
}

// Annex E.1, and the Annex E.2 subset an identifier may not start with.
bool is_annex_e1(int code_point);
bool is_annex_e2(int code_point);

// identifier-nondigit, restricted by Annex E.2 for the leading code point.
inline bool is_identifier_start(int code_point)
{
	if (code_point < 0x80)
	{
		return is_nondigit(code_point);
	}
	return is_annex_e1(code_point) && !is_annex_e2(code_point);
}

inline bool is_identifier_continue(int code_point)
{
	if (code_point < 0x80)
	{
		return is_nondigit(code_point) || is_digit(code_point);
	}
	return is_annex_e1(code_point);
}
