#pragma once

#include <string>
#include <vector>

// `cppgm++ --emit-ast`: runs translation phases 1 to 7 over each source file,
// parses each translation unit and writes the syntax tree dump to `outfile`.
// Throws when a file cannot be read or written, or is not a translation unit.
void emit_ast(const std::string& outfile, const std::vector<std::string>& inputs);
