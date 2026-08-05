// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "ctrl_expr.h"
#include "ctrl_expr_stream.h"
#include "pptoken_lexer.h"

using namespace std;

// mock implementation of IsDefinedIdentifier for PA3
// return true iff first code point is odd
bool PA3Mock_IsDefinedIdentifier(const string& identifier)
{
	if (identifier.empty())
		return false;
	else
		return identifier[0] % 2;
}

// The macro table PA3 evaluates against.  PA5 puts the real one here.
struct PA3MockMacros : CtrlExprMacros
{
	bool is_defined(const string& identifier) const override
	{
		return PA3Mock_IsDefinedIdentifier(identifier);
	}
};

// ctrlexpr: 16.1 Conditional inclusion, on a file of controlling expressions.
//
// Reads a C++ source file from standard input, applies translation phases 1 to
// 3 to delimit its logical lines, and evaluates each non-empty line as the
// controlling expression of an `#if`.  The phases and the evaluation itself
// live in dev/src so that the later assignments drive the same code with a
// real macro table in place of the mock above.

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	try
	{
		std::ios_base::sync_with_stdio(false);

		// Read straight into the buffer the reader takes ownership of, so a
		// large file is never held twice.
		std::string source;
		char chunk[1 << 16];
		while (std::cin.read(chunk, sizeof chunk) || std::cin.gcount() > 0)
		{
			source.append(chunk, static_cast<std::size_t>(std::cin.gcount()));
		}

		CtrlExprResultStream output;
		PPTokenLexer lexer(std::move(source));
		PA3MockMacros macros;
		CtrlExprEvaluator evaluator(macros);

		PPToken token;
		while (lexer.next(token))
		{
			if (token.type == PPTokenType::WhitespaceSequence)
			{
				continue;
			}
			if (token.type != PPTokenType::NewLine &&
			    token.type != PPTokenType::EndOfFile)
			{
				evaluator.add(token);
				continue;
			}

			// A new-line ends a logical line; end of file ends the last one,
			// which phase 2 has usually already terminated.
			if (!evaluator.line_empty())
			{
				CtrlExprValue value;
				if (evaluator.evaluate(value))
				{
					output.emit(value);
				}
				else
				{
					output.emit_error();
				}
			}
			evaluator.begin_line();
		}

		output.emit_eof();
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR:" << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
