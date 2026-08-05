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
	NameId name;               // TT_IDENTIFIER: the spelling
	unsigned long long value;  // TT_LITERAL
};

// Runs translation phases 1 to 7 over `path` and appends its tokens, ending
// with `ST_EOF`.  Throws when the file cannot be read or is not a valid
// sequence of C++ tokens.
void build_sema_tokens(SourceFileTable& files, const PreprocessorOptions& options,
                       const std::string& path, NameTable& names,
                       std::vector<SemaToken>& out);
