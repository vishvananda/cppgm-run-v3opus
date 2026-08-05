#pragma once

#include <cstddef>
#include <string>

#include "post_token.h"

// Analysis of a single literal spelling: 2.14.2 integer-literals, 2.14.4
// floating-literals, 2.14.3 character-literals, and the 2.14.8 user-defined
// forms of each.  String literals are in string_literal.h, because phase 6
// makes a token out of a whole sequence of them rather than out of one
// spelling.

// The encoding a literal is written in.  Ordinary and Utf8 share an element
// type, because the course defines the execution character set as UTF-8.
enum class LiteralEncoding
{
	Ordinary,
	Utf8,
	Char16,
	Char32,
	Wide
};

EFundamentalType literal_element_type(LiteralEncoding encoding);

// Width of one code unit of `encoding`, in bytes.
std::size_t literal_code_unit_size(LiteralEncoding encoding);

// One c-char or s-char.
//
// A numeric escape sequence is kept apart from the characters that denote a
// code point because the two are interpreted differently: a character literal
// reads a numeric escape as a code point, while a string literal reads it as a
// single code unit whose width the encoding of the whole concatenated sequence
// decides.
struct LiteralElement
{
	unsigned long long value;
	bool numeric_escape;
};

// A numeric escape wider than any code unit is saturated to this value, which
// no code unit can hold, so that its own width never has to be tracked.
const unsigned long long kOversizedEscapeValue = 0x100000000ULL;

// Decodes the element at `pos` and returns the position just past it.  The
// spelling comes from phase 3, so every escape sequence in it is well formed.
std::size_t decode_literal_element(const std::string& text, std::size_t pos,
                                   LiteralElement& element);

// True when `text` from `pos` is a ud-suffix.  The course reserves every
// suffix that does not start with an underscore, so `1_ud` is a user-defined
// literal while `1sv` is invalid.
bool is_user_defined_suffix(const std::string& text, std::size_t pos);

// Analyses a pp-number into an integer-literal, a floating-literal or the
// user-defined form of either.  Sets PostTokenKind::Invalid when the spelling
// matches none of those grammars, or when an integer-literal fits no type.
void scan_pp_number(const std::string& spelling, PostToken& token);

// Analyses a character-literal or user-defined-character-literal.
void scan_character_literal(const std::string& spelling, PostToken& token);
