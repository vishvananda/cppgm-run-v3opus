// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "DebugPostTokenStream.h"
#include "macro_expander.h"
#include "post_token.h"
#include "posttokenizer.h"
#include "pptoken_lexer.h"

// macro: translation phases 1 to 6 and the tokenization part of phase 7, with
// `#define`, `#undef` and macro replacement in phase 4.
//
// Reads a C++ source file from standard input and describes the resulting
// sequence of tokens on standard output.  The phases live in dev/src, so this
// file only builds the pipeline: phase 3 feeds phase 4, which feeds the phase
// 5 to 7 driver PA2 already uses.

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	try
	{
		std::ios_base::sync_with_stdio(false);

		// Read straight into the buffer the lexer takes ownership of, so a
		// large translation unit is never held twice.
		std::string source;
		char chunk[1 << 16];
		while (std::cin.read(chunk, sizeof chunk) || std::cin.gcount() > 0)
		{
			source.append(chunk, static_cast<std::size_t>(std::cin.gcount()));
		}

		// The output stream is flushed by its destructor, so the tokens
		// produced before an ill-formed one are still reported.
		DebugPostTokenStream output;
		PPTokenLexer lexer(std::move(source));
		MacroExpander expander(&lexer);
		PostTokenizer tokenizer(expander);

		PostToken token;
		while (tokenizer.next(token))
		{
			output.emit(token);
		}

		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR:" << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
