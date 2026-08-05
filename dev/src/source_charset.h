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

const int kEndOfFile = -1;
const int kMaxCodePoint = 0x10FFFF;

struct Utf8Decoded
{
	int code_point;
	std::size_t length;
};

// Decodes one UTF-8 code unit sequence from the front of [bytes, bytes+size).
// length is zero when the sequence is truncated, is not in shortest form, or
// designates a value that is not a Unicode scalar value.
Utf8Decoded decode_utf8(const char* bytes, std::size_t size);

// Appends the UTF-8 code unit sequence for code_point to out.
void append_utf8(std::string& out, int code_point);

// A Unicode scalar value: in range and outside the surrogate block.
bool is_valid_code_point(int code_point);

// Whitespace other than the line feed, which the token grammar treats as its
// own new-line token rather than as part of a whitespace-sequence.
bool is_basic_whitespace(int code_point);

bool is_digit(int code_point);
bool is_octal_digit(int code_point);
bool is_hex_digit(int code_point);
int hex_digit_value(int code_point);

// identifier-nondigit, restricted by Annex E.2 for the leading code point.
bool is_identifier_start(int code_point);
bool is_identifier_continue(int code_point);
