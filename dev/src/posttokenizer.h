#pragma once

#include <string>

#include "post_token.h"
#include "pptoken_lexer.h"
#include "string_literal.h"

// Translation phases 4 to 6 and the tokenization part of phase 7.
//
// The pull interface mirrors PPTokenLexer so that a later assignment can drive
// tokens the same way phase 3 drives preprocessing-tokens.  Phase 4 is a no-op
// here: PA2 forbids directives, and PA4 replaces the source of
// preprocessing-tokens rather than this class.
//
// Cross-token state is exactly three things: the preprocessing-token that
// ended a string-literal sequence and has not been analysed yet, the open
// sequence itself, and whether the last token emitted was `operator`, which is
// what makes `operator ""sv` a literal-operator-id rather than an ill-formed
// user-defined-literal.
class PostTokenizer
{
public:
	explicit PostTokenizer(std::string source);

	// Produces the next token; returns false once end of file was reported.
	// Throws SourceError when the source file is ill-formed in phases 1 to 3.
	bool next(PostToken& token);

private:
	void convert(PostToken& token);
	void flush_sequence(PostToken& token);

	PPTokenLexer lexer_;
	PPToken current_;
	PPToken held_;
	bool has_held_;
	StringLiteralSequence sequence_;
	std::string deferred_identifier_;
	bool previous_is_operator_;
	bool finished_;
};
