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
// about, and a literal is a value a type can depend on, so a token carries an
// index as well: a `NameTable` identifier for an identifier, and the literal
// pool entry for a literal.  Nothing else of a token survives phase 7, which is
// what keeps the stream one word per token however long the file is.
struct SemaToken
{
	std::uint32_t type;  // ETokenType, TT_IDENTIFIER, TT_LITERAL or ST_EOF
	NameId name;         // TT_IDENTIFIER: the spelling; TT_LITERAL: the literal
};

// A literal token as an expression reads it: 2.14 its type and its object
// representation.
//
// One place holds what a literal is worth.  PA7 reads only the value of an
// integral one, because the only place its grammar admits a literal is an array
// bound; from PA8 a literal is an expression that can initialize an object of
// any type, so the bytes 2.14 gives it are what the image writes and what 8.5.2
// copies into a character array.  The value of an integral literal is those
// same bytes read back rather than a second copy of the same fact.
struct LiteralValue
{
	LiteralValue()
		: type(kFundamentalTypeCount)
		, array(false)
		, elements(0)
	{}

	// Whether this is a literal an array bound or a constant expression can
	// take an integer from, which a string literal and a user-defined literal
	// are not.
	bool integral() const
	{
		return !array && type != kFundamentalTypeCount &&
			fundamental_type_is_integral(type);
	}

	// 2.14.2 and 2.14.3: the value, sign extended to 64 bits when its type is
	// signed, which is the form every later conversion expects.
	unsigned long long integer() const;

	// 2.14.4: the value of a floating literal.  The course ABI is the host's,
	// so the bytes 2.14.4 gave it are the host's too, and `long double` keeps
	// the ten significant bytes of the sixteen an object of it occupies.
	long double real() const;

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
// Every `TT_LITERAL` token appends its value to `literals` and carries the
// index in its `name` field.
void build_sema_tokens(SourceFileTable& files, const PreprocessorOptions& options,
                       const std::string& path, NameTable& names,
                       std::vector<SemaToken>& out, std::vector<LiteralValue>& literals);
