#pragma once

#include <iosfwd>
#include <string>
#include <vector>

struct AstNode;

// Runs translation phases 1 to 7 over each source file, parses each
// translation unit, and writes `write_unit`'s description of it to `outfile`
// between the wrapper lines every dump mode shares.  Throws when a file cannot
// be read or written, or is not a translation unit.
void emit_translation_units(const std::string& outfile,
                            const std::vector<std::string>& inputs,
                            void (*write_unit)(std::ostream& out,
                                               const AstNode& unit));

// `cppgm++ --emit-ast`: the syntax tree dump of each translation unit.
void emit_ast(const std::string& outfile, const std::vector<std::string>& inputs);
