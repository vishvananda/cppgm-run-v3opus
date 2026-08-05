#include "recognizer.h"

// Statements.
//
// Most alternatives are told apart by their first keyword.  The two that are
// not are the declaration-statement and the expression-statement of 6.8, whose
// shared prefix can be arbitrarily long; the declaration is tried first, as
// 6.8 requires, and the expression is the fallback.

bool Recognizer::parse_statement()
{
	return memoize(kMemoStatement, &Recognizer::parse_statement_body);
}

bool Recognizer::parse_statement_body()
{
	const Mark start = mark();
	parse_attribute_specifier_seq();

	switch (peek())
	{
	case OP_LBRACE:
		return parse_compound_statement() || fail(start);
	case KW_IF:
	case KW_SWITCH:
		return parse_selection_statement() || fail(start);
	case KW_WHILE:
	case KW_DO:
	case KW_FOR:
		return parse_iteration_statement() || fail(start);
	case KW_BREAK:
	case KW_CONTINUE:
	case KW_RETURN:
	case KW_GOTO:
		return parse_jump_statement() || fail(start);
	case KW_TRY:
		return parse_try_block() || fail(start);
	case KW_CASE:
	case KW_DEFAULT:
		return parse_labeled_statement() || fail(start);
	default:
		break;
	}

	if (at(TT_IDENTIFIER) && peek(1) == OP_COLON)
	{
		return parse_labeled_statement() || fail(start);
	}

	reset(start);
	if (parse_block_declaration())
	{
		return true;
	}
	reset(start);
	parse_attribute_specifier_seq();
	return parse_expression_statement() || fail(start);
}

// attribute-specifier* (TT_IDENTIFIER | KW_CASE constant-expression
//                       | KW_DEFAULT) OP_COLON statement
bool Recognizer::parse_labeled_statement()
{
	const Mark start = mark();
	parse_attribute_specifier_seq();
	if (accept(KW_CASE))
	{
		if (!parse_constant_expression())
		{
			return fail(start);
		}
	}
	else if (!accept(KW_DEFAULT) && !accept(TT_IDENTIFIER))
	{
		return fail(start);
	}
	if (!accept(OP_COLON) || !parse_statement())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_compound_statement()
{
	const Mark start = mark();
	if (!open_bracket(OP_LBRACE))
	{
		return false;
	}
	while (!at(OP_RBRACE))
	{
		if (!parse_statement())
		{
			return fail(start);
		}
	}
	++pos_;
	angle_ = start.angle;
	return true;
}

bool Recognizer::parse_expression_statement()
{
	const Mark start = mark();
	parse_expression();
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

// KW_IF OP_LPAREN condition OP_RPAREN statement (KW_ELSE statement)?
// KW_SWITCH OP_LPAREN condition OP_RPAREN statement
bool Recognizer::parse_selection_statement()
{
	const Mark start = mark();
	const bool is_if = at(KW_IF);
	if (!accept(KW_IF) && !accept(KW_SWITCH))
	{
		return false;
	}
	if (!open_bracket(OP_LPAREN) || !parse_condition() || !accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	if (!parse_statement())
	{
		return fail(start);
	}
	if (is_if && at(KW_ELSE))
	{
		++pos_;
		if (!parse_statement())
		{
			return fail(start);
		}
	}
	return true;
}

bool Recognizer::parse_iteration_statement()
{
	const Mark start = mark();
	if (accept(KW_WHILE))
	{
		if (!open_bracket(OP_LPAREN) || !parse_condition() || !accept(OP_RPAREN))
		{
			return fail(start);
		}
		angle_ = start.angle;
		if (!parse_statement())
		{
			return fail(start);
		}
		return true;
	}
	if (accept(KW_DO))
	{
		if (!parse_statement() || !accept(KW_WHILE) || !open_bracket(OP_LPAREN) ||
		    !parse_expression() || !accept(OP_RPAREN))
		{
			return fail(start);
		}
		angle_ = start.angle;
		if (!accept(OP_SEMICOLON))
		{
			return fail(start);
		}
		return true;
	}
	return parse_for_statement();
}

// KW_FOR OP_LPAREN for-init-statement condition? OP_SEMICOLON expression?
//     OP_RPAREN statement
// KW_FOR OP_LPAREN for-range-declaration OP_COLON for-range-initializer
//     OP_RPAREN statement
bool Recognizer::parse_for_statement()
{
	const Mark start = mark();
	if (!accept(KW_FOR) || !open_bracket(OP_LPAREN))
	{
		return fail(start);
	}
	const Mark after_paren = mark();

	// for-init-statement is an expression-statement or a simple-declaration,
	// both of which end in a semicolon.
	if (parse_expression_statement() || parse_simple_declaration())
	{
		parse_condition();
		if (accept(OP_SEMICOLON))
		{
			parse_expression();
			if (accept(OP_RPAREN))
			{
				angle_ = start.angle;
				if (parse_statement())
				{
					return true;
				}
				return fail(start);
			}
		}
	}

	reset(after_paren);
	parse_attribute_specifier_seq();
	if (!parse_decl_specifier_seq() || !parse_declarator() || !accept(OP_COLON))
	{
		return fail(start);
	}
	if (!parse_braced_init_list() && !parse_expression())
	{
		return fail(start);
	}
	if (!accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	if (!parse_statement())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_jump_statement()
{
	const Mark start = mark();
	if (accept(KW_BREAK) || accept(KW_CONTINUE))
	{
		return accept(OP_SEMICOLON) || fail(start);
	}
	if (accept(KW_GOTO))
	{
		if (accept(TT_IDENTIFIER) && accept(OP_SEMICOLON))
		{
			return true;
		}
		return fail(start);
	}
	if (!accept(KW_RETURN))
	{
		return false;
	}
	if (!parse_braced_init_list())
	{
		parse_expression();
	}
	return accept(OP_SEMICOLON) || fail(start);
}

bool Recognizer::parse_condition()
{
	if (parse_condition_declaration())
	{
		return true;
	}
	return parse_expression();
}

// attribute-specifier* decl-specifier-seq declarator
//     (OP_ASS initializer-clause | braced-init-list)
bool Recognizer::parse_condition_declaration()
{
	const Mark start = mark();
	parse_attribute_specifier_seq();
	if (!parse_decl_specifier_seq() || !parse_declarator())
	{
		return fail(start);
	}
	if (at(OP_LBRACE))
	{
		return parse_braced_init_list() || fail(start);
	}
	if (!accept(OP_ASS) || !parse_initializer_clause())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_try_block()
{
	const Mark start = mark();
	if (!accept(KW_TRY))
	{
		return false;
	}
	if (!parse_compound_statement() || !parse_handler_seq())
	{
		return fail(start);
	}
	return true;
}

// handler+, where a handler is
// KW_CATCH OP_LPAREN exception-declaration OP_RPAREN compound-statement
bool Recognizer::parse_handler_seq()
{
	std::size_t count = 0;
	while (at(KW_CATCH))
	{
		const Mark handler = mark();
		++pos_;
		if (!open_bracket(OP_LPAREN) || !parse_exception_declaration() ||
		    !accept(OP_RPAREN))
		{
			reset(handler);
			break;
		}
		angle_ = handler.angle;
		if (!parse_compound_statement())
		{
			reset(handler);
			break;
		}
		++count;
	}
	return count != 0;
}

// attribute-specifier* type-specifier-seq (declarator | abstract-declarator?)
// OP_DOTS
bool Recognizer::parse_exception_declaration()
{
	if (accept(OP_DOTS))
	{
		return true;
	}
	const Mark start = mark();
	parse_attribute_specifier_seq();
	if (!parse_type_specifier_seq())
	{
		return fail(start);
	}
	const Mark after_specifiers = mark();

	for (int alternative = 0; alternative < 3; ++alternative)
	{
		reset(after_specifiers);
		if (alternative == 0 && !parse_declarator())
		{
			continue;
		}
		if (alternative == 1 && !parse_abstract_declarator())
		{
			continue;
		}
		if (at(OP_RPAREN))
		{
			return true;
		}
	}
	return fail(start);
}
