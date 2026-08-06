#pragma once

#include <string>
#include <vector>

// `cppgm++ --emit-types`: runs translation phases 1 to 7 over each source file,
// parses each translation unit with the PA10 parser, analyses its scopes,
// declarations and types, and writes the semantic dump to `outfile`.  Throws
// when a file cannot be read or written, is not a translation unit, or is a
// program the assignment gives no meaning to.
void emit_types(const std::string& outfile, const std::vector<std::string>& inputs);
