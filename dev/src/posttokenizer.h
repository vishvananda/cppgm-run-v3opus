#pragma once

#include <string>

#include "post_token.h"
#include "pptoken_lexer.h"
#include "string_literal.h"

// Translation phases 5 to 6 and the tokenization part of phase 7.
//
// The preprocessing-tokens come from a PPTokenSource, so PA2 drives phase 3
// straight into this class while PA4 puts phase 4 in between: what reaches
// here is a macro replaced token sequence either way.
//
// Cross-token state is exactly three things: the preprocessing-token that
// ended a string-literal sequence and has not been analysed yet, the open
// sequence itself, and whether the last token emitted was `operator`, which is
// what makes `operator ""sv` a literal-operator-id rather than an ill-formed
// user-defined-literal.
class PostTokenizer
{
public:
	explicit PostTokenizer(PPTokenSource& source);

	// Produces the next token; returns false once end of file was reported.
	// Throws SourceError when the source file is ill-formed in phases 1 to 3.
	bool next(PostToken& token);

private:
	void convert(PostToken& token);
	void flush_sequence(PostToken& token);

	PPTokenSource& source_;
	PPToken current_;
	PPToken held_;
	bool has_held_;
	StringLiteralSequence sequence_;
	std::string deferred_identifier_;
	bool previous_is_operator_;
	bool finished_;
};
