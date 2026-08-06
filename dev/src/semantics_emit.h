#pragma once

#include <string>
#include <vector>

// `cppgm++ --emit-semantics`: runs translation phases 1 to 7 over each source
// file, parses each translation unit with the PA10 parser, resolves its
// declarations, statements and expressions with the PA11 scope and type model,
// and writes the PA12 semantic dump to `outfile`.  Throws when a file cannot be
// read or written, is not a translation unit, or is a program the assignment
// gives no meaning to.
void emit_semantics(const std::string& outfile,
                    const std::vector<std::string>& inputs);
