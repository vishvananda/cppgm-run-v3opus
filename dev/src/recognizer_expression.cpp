#include "recognizer.h"

// Expressions, lambdas and braced initializers.
//
// The binary operator cascade of the grammar is eleven nonterminals that
// differ only in their operator set and their operand, so it is one function
// parameterised by level rather than eleven near-copies.  Two of those
// operator sets are conditional: while the innermost open bracket pair is
// `<>`, a `>` and an `>>` belong to the pair and cannot be read as operators.

namespace
{

// The binary operator levels, loosest last.  Level 0 takes cast-expression
// operands; every other level takes the level below it.
enum BinaryLevel
{
	kLevelPointerToMember,
	kLevelMultiplicative,
	kLevelAdditive,
	kLevelShift,
	kLevelRelational,
	kLevelEquality,
	kLevelAnd,
	kLevelExclusiveOr,
	kLevelInclusiveOr,
	kLevelLogicalAnd,
	kLevelLogicalOr
};

}

bool Recognizer::parse_primary_expression()
{
	switch (peek())
	{
	case KW_TRUE:
	case KW_FALSE:
	case KW_NULLPTR:
	case KW_THIS:
	case TT_LITERAL:
		++pos_;
		return true;

	case OP_LPAREN:
	{
		const Mark start = mark();
		if (open_bracket(OP_LPAREN) && parse_expression() && accept(OP_RPAREN))
		{
			angle_ = start.angle;
			return true;
		}
		return fail(start);
	}

	case OP_LSQUARE:
		return parse_lambda_expression();

	default:
		return parse_id_expression();
	}
}

bool Recognizer::parse_postfix_expression()
{
	if (!parse_postfix_root())
	{
		return false;
	}
	while (parse_postfix_suffix())
	{
	}
	return true;
}

bool Recognizer::parse_postfix_root()
{
	const Mark start = mark();
	switch (peek())
	{
	case KW_DYNAMIC_CAST:
	case KW_STATIC_CAST:
	case KW_REINTERPET_CAST:
	case KW_CONST_CAST:
	{
		++pos_;
		if (!accept(OP_LT))
		{
			return fail(start);
		}
		angle_ = true;
		if (!parse_type_id() || !parse_close_angle_bracket())
		{
			return fail(start);
		}
		angle_ = start.angle;
		if (!open_bracket(OP_LPAREN) || !parse_expression() || !accept(OP_RPAREN))
		{
			return fail(start);
		}
		angle_ = start.angle;
		return true;
	}

	case KW_TYPEID:
	{
		++pos_;
		if (!open_bracket(OP_LPAREN))
		{
			return fail(start);
		}
		if (!parse_type_id() || !at(OP_RPAREN))
		{
			pos_ = start.pos + 2;
			if (!parse_expression())
			{
				return fail(start);
			}
		}
		if (!accept(OP_RPAREN))
		{
			return fail(start);
		}
		angle_ = start.angle;
		return true;
	}

	default:
		break;
	}

	// A type followed by `(` or `{` is a functional cast; the same type
	// followed by anything else is an id-expression, so the type alternatives
	// are tried first and give the position back when no call follows.
	if (parse_typename_specifier() || parse_simple_type_specifier())
	{
		if (at(OP_LBRACE))
		{
			if (parse_braced_init_list())
			{
				return true;
			}
		}
		else if (open_bracket(OP_LPAREN))
		{
			parse_expression_list();
			if (accept(OP_RPAREN))
			{
				angle_ = start.angle;
				return true;
			}
		}
		reset(start);
	}

	return parse_primary_expression();
}

bool Recognizer::parse_postfix_suffix()
{
	const Mark start = mark();
	switch (peek())
	{
	case OP_INC:
	case OP_DEC:
		++pos_;
		return true;

	case OP_LSQUARE:
	{
		open_bracket(OP_LSQUARE);
		if (!parse_expression() && !parse_braced_init_list())
		{
			return fail(start);
		}
		if (!accept(OP_RSQUARE))
		{
			return fail(start);
		}
		angle_ = start.angle;
		return true;
	}

	case OP_LPAREN:
	{
		open_bracket(OP_LPAREN);
		parse_expression_list();
		if (!accept(OP_RPAREN))
		{
			return fail(start);
		}
		angle_ = start.angle;
		return true;
	}

	case OP_DOT:
	case OP_ARROW:
	{
		++pos_;
		if (parse_pseudo_destructor_name())
		{
			return true;
		}
		accept(KW_TEMPLATE);
		if (parse_id_expression())
		{
			return true;
		}
		return fail(start);
	}

	default:
		return false;
	}
}

bool Recognizer::parse_unary_expression()
{
	const Mark start = mark();
	switch (peek())
	{
	case OP_INC: case OP_DEC: case OP_STAR: case OP_AMP: case OP_PLUS:
	case OP_MINUS: case OP_LNOT: case OP_COMPL:
		++pos_;
		if (parse_cast_expression())
		{
			return true;
		}
		return fail(start);

	case KW_SIZEOF:
	{
		++pos_;
		if (accept(OP_DOTS))
		{
			if (open_bracket(OP_LPAREN) && accept(TT_IDENTIFIER) && accept(OP_RPAREN))
			{
				angle_ = start.angle;
				return true;
			}
			return fail(start);
		}
		const Mark after_sizeof = mark();
		if (open_bracket(OP_LPAREN) && parse_type_id() && accept(OP_RPAREN))
		{
			angle_ = start.angle;
			return true;
		}
		reset(after_sizeof);
		if (parse_unary_expression())
		{
			return true;
		}
		return fail(start);
	}

	case KW_ALIGNOF:
	{
		++pos_;
		if (open_bracket(OP_LPAREN) && parse_type_id() && accept(OP_RPAREN))
		{
			angle_ = start.angle;
			return true;
		}
		return fail(start);
	}

	case KW_NOEXCEPT:
	{
		++pos_;
		if (open_bracket(OP_LPAREN) && parse_expression() && accept(OP_RPAREN))
		{
			angle_ = start.angle;
			return true;
		}
		return fail(start);
	}

	case KW_NEW:
		return parse_new_expression();

	case KW_DELETE:
		return parse_delete_expression();

	case OP_COLON2:
		if (peek(1) == KW_NEW)
		{
			return parse_new_expression();
		}
		if (peek(1) == KW_DELETE)
		{
			return parse_delete_expression();
		}
		break;

	default:
		break;
	}

	return parse_postfix_expression();
}

bool Recognizer::parse_new_expression()
{
	const Mark start = mark();
	accept(OP_COLON2);
	if (!accept(KW_NEW))
	{
		return fail(start);
	}
	const Mark after_new = mark();

	// The placement argument list and a parenthesized type-id look alike, so
	// both readings of a leading `(` are tried, longest first.
	for (int with_placement = 1; with_placement >= 0; --with_placement)
	{
		reset(after_new);
		if (with_placement != 0 && !parse_new_placement())
		{
			continue;
		}
		const Mark after_placement = mark();
		if (open_bracket(OP_LPAREN) && parse_type_id() && accept(OP_RPAREN))
		{
			angle_ = after_placement.angle;
			parse_new_initializer();
			return true;
		}
		reset(after_placement);
		if (parse_new_type_id())
		{
			parse_new_initializer();
			return true;
		}
	}
	return fail(start);
}

bool Recognizer::parse_new_placement()
{
	const Mark start = mark();
	if (open_bracket(OP_LPAREN) && parse_expression_list() && accept(OP_RPAREN))
	{
		angle_ = start.angle;
		return true;
	}
	return fail(start);
}

bool Recognizer::parse_new_type_id()
{
	if (!parse_type_specifier_seq())
	{
		return false;
	}
	parse_new_declarator();
	return true;
}

bool Recognizer::parse_new_declarator()
{
	const std::size_t operators = parse_ptr_operators();
	if (parse_noptr_new_declarator())
	{
		return true;
	}
	return operators != 0;
}

// OP_LSQUARE expression OP_RSQUARE attribute-specifier*
//     (OP_LSQUARE constant-expression OP_RSQUARE attribute-specifier*)*
bool Recognizer::parse_noptr_new_declarator()
{
	const Mark start = mark();
	if (!open_bracket(OP_LSQUARE) || !parse_expression() || !accept(OP_RSQUARE))
	{
		return fail(start);
	}
	angle_ = start.angle;
	parse_attribute_specifier_seq();

	while (at(OP_LSQUARE))
	{
		const Mark bound = mark();
		open_bracket(OP_LSQUARE);
		if (!parse_constant_expression() || !accept(OP_RSQUARE))
		{
			reset(bound);
			break;
		}
		angle_ = bound.angle;
		parse_attribute_specifier_seq();
	}
	return true;
}

bool Recognizer::parse_new_initializer()
{
	if (at(OP_LBRACE))
	{
		return parse_braced_init_list();
	}
	const Mark start = mark();
	if (!open_bracket(OP_LPAREN))
	{
		return false;
	}
	parse_expression_list();
	if (!accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

bool Recognizer::parse_delete_expression()
{
	const Mark start = mark();
	accept(OP_COLON2);
	if (!accept(KW_DELETE))
	{
		return fail(start);
	}
	if (at(OP_LSQUARE) && peek(1) == OP_RSQUARE)
	{
		pos_ += 2;
	}
	if (!parse_cast_expression())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_cast_expression()
{
	const Mark start = mark();
	if (open_bracket(OP_LPAREN) && parse_type_id() && accept(OP_RPAREN))
	{
		angle_ = start.angle;
		if (parse_cast_expression())
		{
			return true;
		}
	}
	reset(start);
	return parse_unary_expression();
}

bool Recognizer::accept_binary_operator(int level)
{
	switch (level)
	{
	case kLevelPointerToMember:
		return accept(OP_DOTSTAR) || accept(OP_ARROWSTAR);
	case kLevelMultiplicative:
		return accept(OP_STAR) || accept(OP_DIV) || accept(OP_MOD);
	case kLevelAdditive:
		return accept(OP_PLUS) || accept(OP_MINUS);
	case kLevelShift:
		if (accept(OP_LSHIFT))
		{
			return true;
		}
		if (!angle_ && at(ST_RSHIFT_1) && peek(1) == ST_RSHIFT_2)
		{
			pos_ += 2;
			return true;
		}
		return false;
	case kLevelRelational:
		if (accept(OP_LT) || accept(OP_LE) || accept(OP_GE))
		{
			return true;
		}
		return !angle_ && accept(OP_GT);
	case kLevelEquality:
		return accept(OP_EQ) || accept(OP_NE);
	case kLevelAnd:
		return accept(OP_AMP);
	case kLevelExclusiveOr:
		return accept(OP_XOR);
	case kLevelInclusiveOr:
		return accept(OP_BOR);
	case kLevelLogicalAnd:
		return accept(OP_LAND);
	default:
		return accept(OP_LOR);
	}
}

bool Recognizer::parse_binary_expression(int level)
{
	if (level == kLevelPointerToMember ? !parse_cast_expression()
	                                   : !parse_binary_expression(level - 1))
	{
		return false;
	}
	while (true)
	{
		const Mark operand = mark();
		if (!accept_binary_operator(level))
		{
			return true;
		}
		const bool matched = level == kLevelPointerToMember
			? parse_cast_expression()
			: parse_binary_expression(level - 1);
		if (!matched)
		{
			reset(operand);
			return true;
		}
	}
}

bool Recognizer::parse_conditional_expression()
{
	return memoize(kMemoConditionalExpression,
	               &Recognizer::parse_conditional_expression_body);
}

bool Recognizer::parse_conditional_expression_body()
{
	if (!parse_binary_expression(kLevelLogicalOr))
	{
		return false;
	}
	parse_conditional_tail();
	return true;
}

// OP_QMARK expression OP_COLON assignment-expression
bool Recognizer::parse_conditional_tail()
{
	const Mark start = mark();
	if (!accept(OP_QMARK) || !parse_expression() || !accept(OP_COLON) ||
	    !parse_assignment_expression())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_assignment_expression()
{
	return memoize(kMemoAssignmentExpression,
	               &Recognizer::parse_assignment_expression_body);
}

// The three alternatives share their left operand, so it is parsed once and
// the token after it decides between an assignment, a conditional and a bare
// logical-or-expression.
bool Recognizer::parse_assignment_expression_body()
{
	if (at(KW_THROW))
	{
		return parse_throw_expression();
	}
	if (!parse_binary_expression(kLevelLogicalOr))
	{
		return false;
	}
	const Mark after_operand = mark();
	if (accept_assignment_operator())
	{
		if (parse_initializer_clause())
		{
			return true;
		}
		reset(after_operand);
		return true;
	}
	parse_conditional_tail();
	return true;
}

bool Recognizer::accept_assignment_operator()
{
	switch (peek())
	{
	case OP_ASS: case OP_STARASS: case OP_DIVASS: case OP_MODASS:
	case OP_PLUSASS: case OP_MINUSASS: case OP_RSHIFTASS: case OP_LSHIFTASS:
	case OP_BANDASS: case OP_XORASS: case OP_BORASS:
		++pos_;
		return true;
	default:
		return false;
	}
}

bool Recognizer::parse_throw_expression()
{
	if (!accept(KW_THROW))
	{
		return false;
	}
	parse_assignment_expression();
	return true;
}

bool Recognizer::parse_expression()
{
	if (!parse_assignment_expression())
	{
		return false;
	}
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_assignment_expression())
		{
			reset(item);
			break;
		}
	}
	return true;
}

bool Recognizer::parse_constant_expression()
{
	return parse_conditional_expression();
}

bool Recognizer::parse_expression_list()
{
	return parse_initializer_list();
}

bool Recognizer::parse_initializer_clause()
{
	return memoize(kMemoInitializerClause,
	               &Recognizer::parse_initializer_clause_body);
}

bool Recognizer::parse_initializer_clause_body()
{
	if (at(OP_LBRACE))
	{
		return parse_braced_init_list();
	}
	return parse_assignment_expression();
}

// initializer-clause-dots (OP_COMMA initializer-clause-dots)*
bool Recognizer::parse_initializer_list()
{
	if (!parse_initializer_clause())
	{
		return false;
	}
	accept(OP_DOTS);
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_initializer_clause())
		{
			reset(item);
			break;
		}
		accept(OP_DOTS);
	}
	return true;
}

bool Recognizer::parse_braced_init_list()
{
	const Mark start = mark();
	if (!open_bracket(OP_LBRACE))
	{
		return false;
	}
	if (parse_initializer_list())
	{
		accept(OP_COMMA);
	}
	if (!accept(OP_RBRACE))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

bool Recognizer::parse_lambda_expression()
{
	const Mark start = mark();
	if (!parse_lambda_introducer())
	{
		return false;
	}
	parse_lambda_declarator();
	if (!parse_compound_statement())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_lambda_introducer()
{
	const Mark start = mark();
	if (!open_bracket(OP_LSQUARE))
	{
		return false;
	}
	parse_lambda_capture();
	if (!accept(OP_RSQUARE))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

// capture-default | capture-list | capture-default OP_COMMA capture-list
bool Recognizer::parse_lambda_capture()
{
	const Mark start = mark();
	// A capture-default is `&` or `=`; `&x` is a capture instead, so the
	// default only stands when a comma or the closing bracket follows.
	if ((at(OP_AMP) || at(OP_ASS)) && (peek(1) == OP_COMMA || peek(1) == OP_RSQUARE))
	{
		++pos_;
		if (!accept(OP_COMMA))
		{
			return true;
		}
		if (parse_capture_list())
		{
			return true;
		}
		return fail(start);
	}
	return parse_capture_list();
}

bool Recognizer::parse_capture_list()
{
	if (!parse_capture())
	{
		return false;
	}
	accept(OP_DOTS);
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_capture())
		{
			reset(item);
			break;
		}
		accept(OP_DOTS);
	}
	return true;
}

bool Recognizer::parse_capture()
{
	if (accept(KW_THIS) || accept(TT_IDENTIFIER))
	{
		return true;
	}
	const Mark start = mark();
	if (accept(OP_AMP) && accept(TT_IDENTIFIER))
	{
		return true;
	}
	return fail(start);
}

// OP_LPAREN parameter-declaration-clause OP_RPAREN KW_MUTABLE?
//     exception-specification? attribute-specifier* trailing-return-type?
bool Recognizer::parse_lambda_declarator()
{
	const Mark start = mark();
	if (!open_bracket(OP_LPAREN) || !parse_parameter_declaration_clause() ||
	    !accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	accept(KW_MUTABLE);
	parse_exception_specification();
	parse_attribute_specifier_seq();
	parse_trailing_return_type();
	return true;
}
