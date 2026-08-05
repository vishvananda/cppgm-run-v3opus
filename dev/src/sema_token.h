#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "name_table.h"
#include "parse_token.h"

struct PreprocessorOptions;
class SourceFileTable;

// A phase 7 token as a semantic analysis reads it.
//
// The PA6 recognizer needed no more of a token than its terminal, because a
// grammar with mock name lookup could decide everything from the letters of a
// spelling.  From PA7 on the letters are a name that a scope has to be asked
// about, and a literal is a value the type of an array depends on, so the
// stream carries both: the spelling as its `NameTable` identifier, and the
// value of an integral literal already converted.
struct SemaToken
{
	std::uint16_t type;        // ETokenType, TT_IDENTIFIER, TT_LITERAL or ST_EOF
	bool integral;             // TT_LITERAL: `value` is the literal's value
	NameId name;               // TT_IDENTIFIER: the spelling; TT_LITERAL: literal
	unsigned long long value;  // TT_LITERAL
};

// A literal token as an expression reads it: 2.14 its type and its object
// representation.
//
// PA7 needed no more of a literal than the value of an integral one, because
// the only place its grammar admits a literal is an array bound.  From PA8 a
// literal is an expression that can initialize an object of any type, so the
// bytes 2.14 gives it are what the image writes and what 8.5.2 copies into a
// character array.
struct LiteralValue
{
	LiteralValue()
		: type(kFundamentalTypeCount)
		, array(false)
		, elements(0)
	{}

	// `kFundamentalTypeCount` for a user-defined literal, which `pa8.gram`
	// gives no meaning to.
	EFundamentalType type;
	bool array;                // a string literal, whose type is an array
	std::uint32_t elements;    // array: elements, including the terminating null
	std::string data;          // the object representation, little-endian
};

// Runs translation phases 1 to 7 over `path` and appends its tokens, ending
// with `ST_EOF`.  Throws when the file cannot be read or is not a valid
// sequence of C++ tokens.
//
// When `literals` is given, every `TT_LITERAL` token appends its value to it
// and carries the index in its `name` field.  A tool that never reads a
// literal's value passes nothing and pays nothing for it.
void build_sema_tokens(SourceFileTable& files, const PreprocessorOptions& options,
                       const std::string& path, NameTable& names,
                       std::vector<SemaToken>& out,
                       std::vector<LiteralValue>* literals = nullptr);
