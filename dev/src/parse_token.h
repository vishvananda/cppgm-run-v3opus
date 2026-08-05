#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "token_model.h"

struct PreprocessorOptions;
class SourceFileTable;

// The terminal vocabulary of the PA6 grammar.
//
// It is the phase 7 token vocabulary of `token_model.h` plus the handful of
// terminals the grammar names that a token type alone cannot express.  The
// enumerators continue where `ETokenType` stops, so one small integer holds
// either kind and the parser compares terminals rather than switching on a
// kind first.
//
// `OP_RSHIFT` never appears: the assignment replaces it with the two
// consecutive terminals `ST_RSHIFT_1` and `ST_RSHIFT_2` so that `T<U<3>>`
// closes two angle bracket pairs (14.2.3) while `a >> b` still shifts.
enum ParseTokenType
{
	TT_IDENTIFIER = kTokenTypeCount,
	TT_LITERAL,
	ST_EOF,
	ST_RSHIFT_1,
	ST_RSHIFT_2,
	kParseTokenTypeCount
};

// The lexical facts about a token that the grammar asks about later.
//
// The first five are PA6 mock name lookup: a name category is decided by the
// letters of the spelling, so it is a property of the token rather than of a
// symbol table, and it is decided once when the stream is built.  The rest are
// the grammar's specialised terminals, each of which is also a plain
// `TT_IDENTIFIER` or `TT_LITERAL`.
enum ParseTokenFlag
{
	kNameClass = 1u << 0,      // contains `C`, so it is a class-name
	kNameTemplate = 1u << 1,   // contains `T`, so it is a template-name
	kNameTypedef = 1u << 2,    // contains `Y`, so it is a typedef-name
	kNameEnum = 1u << 3,       // contains `E`, so it is an enum-name
	kNameNamespace = 1u << 4,  // contains `N`, so it is a namespace-name
	kIsOverride = 1u << 5,     // ST_OVERRIDE
	kIsFinal = 1u << 6,        // ST_FINAL
	kIsEmptyString = 1u << 7,  // ST_EMPTYSTR
	kIsZero = 1u << 8          // ST_ZERO
};

// A token as the parser sees it.  Everything the grammar can ask fits in four
// bytes, so a translation unit's token sequence is a flat array the parser can
// index and backtrack over without touching the tokenizer again.
struct ParseToken
{
	std::uint16_t type;
	std::uint16_t flags;
};

// The name categories of `identifier` under PA6 mock name lookup.
unsigned parse_token_name_flags(const std::string& identifier);

// Runs translation phases 1 to 7 over `path` and appends its tokens, ending
// with `ST_EOF`.  Throws when the file cannot be read or is not a valid
// sequence of C++ tokens, which the tool reports as `BAD`.
void build_parse_tokens(SourceFileTable& files, const PreprocessorOptions& options,
                        const std::string& path, std::vector<ParseToken>& out);
