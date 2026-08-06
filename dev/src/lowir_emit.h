#pragma once

#include <string>
#include <vector>

// `cppgm++ --emit-lowir`: runs translation phases 1 to 7 over each source file,
// parses each translation unit with the PA10 parser, resolves it with the
// PA11/PA12 semantic layer, lowers the resolved procedural program into PA13
// LowIR, and writes that LowIR text to `outfile`.  Throws when a file cannot be
// read or written, is not a translation unit, or is a program this milestone
// gives no lowering to.
void emit_lowir(const std::string& outfile,
                const std::vector<std::string>& inputs);
