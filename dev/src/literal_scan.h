#pragma once

#include <cstddef>
#include <string>

#include "post_token.h"
#include "source_charset.h"

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

// Decodes the source character at `pos`, which is every element of a
// raw-string body and every element of any other body that is not an escape
// sequence.  Phase 1 has already rejected a source file that is not valid
// UTF-8; a byte that decodes to nothing stands for itself so that this cannot
// run off the end of a body whatever it was handed.
inline std::size_t decode_source_character(const std::string& text, std::size_t pos,
                                           LiteralElement& element)
{
	element.numeric_escape = false;
	const Utf8Decoded decoded = decode_utf8(text.data() + pos, text.size() - pos);
	if (decoded.length == 0)
	{
		element.value = static_cast<unsigned char>(text[pos]);
		return pos + 1;
	}
	element.value = static_cast<unsigned long long>(decoded.code_point);
	return pos + decoded.length;
}

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

// 2.14.5p5: the code units one element of a literal body comes to in the
// ordinary execution character set, which the course defines as UTF-8.  A
// numeric escape names one code unit and every other element names a code
// point, so the two are appended differently - and a string literal's body and
// a character literal's are encoded by this one rule.  False where a numeric
// escape holds no code unit.
bool append_ordinary_units(const LiteralElement& element, std::string& units);

// 2.14.3p1: which reading of a character-literal a layer asks for.  What a
// c-char that no ordinary code unit holds is worth, and whether more than one
// c-char is a literal at all, are two answers to one question, so they are one
// fact of the reader rather than of the spelling: PA2's dump is course defined
// to hold exactly one code point and to refuse a run of them, while the
// language gives a `char` holding the code unit and an `int` holding the run.
// The tools that print a token and 16.1's controlling expression ask for the
// first, and the compiler that evaluates a literal asks for the second.
enum class CharacterLiterals
{
	CourseSubset,
	Language
};

// Analyses a character-literal or user-defined-character-literal.
void scan_character_literal(const std::string& spelling, PostToken& token,
                            CharacterLiterals characters);
