#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "DebugPPTokenStream.h"
#include "IPPTokenStream.h"
#include "pptoken_lexer.h"

// pptoken: translation phases 1, 2 and 3.
//
// Reads a C++ source file from standard input and describes the resulting
// sequence of preprocessing-tokens on standard output.  The phases themselves
// live in dev/src so that the later assignments drive the same lexer.

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	try
	{
		std::ios_base::sync_with_stdio(false);

		// Read straight into the buffer the reader takes ownership of, so a
		// large translation unit is never held twice.
		std::string source;
		char chunk[1 << 16];
		while (std::cin.read(chunk, sizeof chunk) || std::cin.gcount() > 0)
		{
			source.append(chunk, static_cast<std::size_t>(std::cin.gcount()));
		}

		DebugPPTokenStream output;
		PPTokenLexer lexer(std::move(source));

		PPToken token;
		while (lexer.next(token))
		{
			emit_pp_token(output, token);
		}

		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR:" << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
