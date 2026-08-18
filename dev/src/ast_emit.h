#pragma once

#include <iosfwd>
#include <string>
#include <vector>

struct AstNode;
class AstArena;

// Runs translation phases 1 to 7 over each source file, parses each
// translation unit, and writes `write_unit`'s description of it to `outfile`
// between the wrapper lines every dump mode shares.  Throws when a file cannot
// be read or written, or is not a translation unit.
//
// 7.1.6.2p1: the arena travels with the tree, because the parse flattens a
// decltype-specifier written before `::` into the name around it and keeps the
// tree it read for the operand there - which is the one thing that spelling
// cannot hold, in every mode that reads such a name.
void emit_translation_units(const std::string& outfile,
                            const std::vector<std::string>& inputs,
                            void (*write_unit)(std::ostream& out,
                                               const AstNode& unit,
                                               AstArena& written));

// `cppgm++ --emit-ast`: the syntax tree dump of each translation unit.
void emit_ast(const std::string& outfile, const std::vector<std::string>& inputs);
