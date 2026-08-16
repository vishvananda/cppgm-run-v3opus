#include "ast_parser.h"

// Expressions: one rule per precedence level of the grammar, plus the forms
// whose operand is a type-id rather than an expression.

namespace
{

// The operators of one precedence level, innermost level last.  `>` and the
// first half of `>>` are only operators while the innermost open bracket pair
// is not an angle bracket pair, which the level rule checks.
const unsigned kBinaryLevels[][4] = {
	{ OP_LOR, 0, 0, 0 },
	{ OP_LAND, 0, 0, 0 },
	{ OP_BOR, 0, 0, 0 },
	{ OP_XOR, 0, 0, 0 },
	{ OP_AMP, 0, 0, 0 },
	{ OP_EQ, OP_NE, 0, 0 },
	{ OP_LT, OP_GT, OP_LE, OP_GE },
	{ OP_LSHIFT, ST_RSHIFT_1, 0, 0 },
	{ OP_PLUS, OP_MINUS, 0, 0 },
	{ OP_STAR, OP_DIV, OP_MOD, 0 },
	{ OP_DOTSTAR, OP_ARROWSTAR, 0, 0 }
};

const int kBinaryLevelCount =
	static_cast<int>(sizeof(kBinaryLevels) / sizeof(kBinaryLevels[0]));

bool is_unary_operator(unsigned type)
{
	switch (type)
	{
	case OP_INC: case OP_DEC: case OP_STAR: case OP_AMP:
	case OP_PLUS: case OP_MINUS: case OP_LNOT: case OP_COMPL:
		return true;
	default:
		return false;
	}
}

bool is_assignment_operator(unsigned type)
{
	switch (type)
	{
	case OP_ASS: case OP_PLUSASS: case OP_MINUSASS: case OP_STARASS:
	case OP_DIVASS: case OP_MODASS: case OP_XORASS: case OP_BANDASS:
	case OP_BORASS: case OP_LSHIFTASS: case OP_RSHIFTASS:
		return true;
	default:
		return false;
	}
}

bool is_builtin_type_keyword(unsigned type)
{
	switch (type)
	{
	case KW_BOOL: case KW_CHAR: case KW_CHAR16_T: case KW_CHAR32_T:
	case KW_DOUBLE: case KW_FLOAT: case KW_INT: case KW_LONG:
	case KW_SHORT: case KW_SIGNED: case KW_UNSIGNED: case KW_VOID:
	case KW_WCHAR_T:
		return true;
	default:
		return false;
	}
}

}

AstNode* AstParser::parse_expression()
{
	AstNode* left = parse_assignment_expression();
	if (left == nullptr)
	{
		return nullptr;
	}
	while (at(OP_COMMA))
	{
		const Mark start = mark();
		AstNode* node = make(AstKind::BinaryExpression);
		node->token = OP_COMMA;
		node->text = spelling();
		++pos_;
		AstNode* right = parse_assignment_expression();
		if (right == nullptr)
		{
			return fail(start);
		}
		node->add(left);
		node->add(right);
		left = node;
	}
	return left;
}

AstNode* AstParser::parse_assignment_expression()
{
	const Mark start = mark();
	AstNode* left = parse_conditional_expression();
	if (left == nullptr)
	{
		return nullptr;
	}
	if (!is_assignment_operator(peek()))
	{
		return left;
	}
	AstNode* node = make(AstKind::AssignmentExpression);
	node->token = static_cast<std::uint16_t>(peek());
	node->text = spelling();
	++pos_;
	AstNode* right = parse_assignment_expression();
	if (right == nullptr)
	{
		return fail(start);
	}
	node->add(left);
	node->add(right);
	return node;
}

AstNode* AstParser::parse_conditional_expression()
{
	const Mark start = mark();
	AstNode* condition = parse_binary_expression(0);
	if (condition == nullptr || !at(OP_QMARK))
	{
		return condition;
	}
	++pos_;
	AstNode* consequent = parse_expression();
	if (consequent == nullptr || !accept(OP_COLON))
	{
		return fail(start);
	}
	AstNode* alternative = parse_assignment_expression();
	if (alternative == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::ConditionalExpression);
	node->add(condition);
	node->add(consequent);
	node->add(alternative);
	return node;
}

AstNode* AstParser::parse_binary_expression(int level)
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return nullptr;
	}
	if (level == kBinaryLevelCount)
	{
		return parse_cast_expression();
	}
	AstNode* left = parse_binary_expression(level + 1);
	if (left == nullptr)
	{
		return nullptr;
	}
	for (;;)
	{
		const unsigned* operators = kBinaryLevels[level];
		unsigned found = 0;
		for (int index = 0; index < 4 && operators[index] != 0; ++index)
		{
			if (peek() == operators[index])
			{
				found = operators[index];
				break;
			}
		}
		if (found == 0 ||
		    (angle_ && (found == OP_GT || found == ST_RSHIFT_1)))
		{
			return left;
		}
		const Mark start = mark();
		AstNode* node = make(AstKind::BinaryExpression);
		node->token = static_cast<std::uint16_t>(found);
		node->text = spelling();
		++pos_;
		if (found == ST_RSHIFT_1)
		{
			if (!accept(ST_RSHIFT_2))
			{
				return fail(start);
			}
			node->token = OP_RSHIFT;
			node->text = ">>";
		}
		AstNode* right = parse_binary_expression(level + 1);
		if (right == nullptr)
		{
			return fail(start);
		}
		node->add(left);
		node->add(right);
		left = node;
	}
}

// A parenthesized C style cast, or the unary expression that is not one.
AstNode* AstParser::parse_cast_expression()
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return nullptr;
	}
	const Mark start = mark();
	if (at(OP_LPAREN))
	{
		++pos_;
		AstNode* type = nullptr;
		{
			BracketGuard brackets(*this, false);
			type = parse_type_id();
		}
		if (type != nullptr && at(OP_RPAREN) && is_definite_type_id(type))
		{
			++pos_;
			AstNode* operand = parse_cast_expression();
			if (operand != nullptr)
			{
				AstNode* node = make(AstKind::CastExpression);
				node->token = OP_LPAREN;
				node->add(type);
				node->add(operand);
				return node;
			}
		}
		reset(start);
	}
	return parse_unary_expression();
}

// The contents of the parentheses of `sizeof`, `typeid`, `alignof` or
// `noexcept`, which the grammar writes either way round.
AstNode* AstParser::parse_parenthesized_operand(bool prefer_type)
{
	const Mark start = mark();
	if (!at(OP_LPAREN))
	{
		return nullptr;
	}
	++pos_;
	AstNode* result = nullptr;
	{
		BracketGuard brackets(*this, false);
		const Mark inner = mark();
		AstNode* type = parse_type_id();
		const bool usable = type != nullptr && at(OP_RPAREN);
		if (usable && (prefer_type || is_definite_type_id(type)))
		{
			result = type;
		}
		else
		{
			const Mark after_type = mark();
			reset(inner);
			AstNode* expression = parse_expression();
			if (expression != nullptr && at(OP_RPAREN))
			{
				result = expression;
			}
			else if (usable)
			{
				reset(after_type);
				result = type;
			}
		}
		if (result == nullptr || !at(OP_RPAREN))
		{
			return fail(start);
		}
		++pos_;
	}
	return result;
}

AstNode* AstParser::parse_unary_expression()
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return nullptr;
	}
	const Mark start = mark();
	if (is_unary_operator(peek()))
	{
		AstNode* node = make(AstKind::UnaryExpression);
		node->token = static_cast<std::uint16_t>(peek());
		node->text = spelling();
		++pos_;
		AstNode* operand = parse_cast_expression();
		if (operand == nullptr)
		{
			return fail(start);
		}
		node->add(operand);
		return node;
	}
	if (at(KW_SIZEOF) && peek(1) == OP_DOTS)
	{
		// 5.3.3p5: `sizeof ... ( identifier )`, whose operand is a parameter
		// pack and never an expression - the parentheses are part of the
		// operator rather than a primary-expression around a name.
		pos_ += 2;
		AstNode* node = make(AstKind::SizeofPackExpression);
		if (!at(OP_LPAREN) || tokens_.type(pos_ + 1) != TT_IDENTIFIER ||
		    tokens_.type(pos_ + 2) != OP_RPAREN)
		{
			return fail(start);
		}
		++pos_;
		node->add(make_text(AstKind::Identifier, spelling()));
		pos_ += 2;
		return parse_postfix_suffixes(node);
	}
	if (at(KW_SIZEOF))
	{
		++pos_;
		AstNode* node = make(AstKind::SizeofExpression);
		AstNode* operand = at(OP_LPAREN)
			? parse_parenthesized_operand(false)
			: parse_unary_expression();
		if (operand == nullptr)
		{
			return fail(start);
		}
		node->add(operand);
		return node;
	}
	// 1.4p8 and 5.3.6: `__alignof` and `__alignof__` are the spellings this
	// implementation reserves for the operator 5.3.6 spells `alignof`, and 2.12
	// leaves them identifiers - so the tree carries the keyword the operator is
	// and the spelling the program wrote, which is what the reference dumps.
	// Neither takes an operand that is not parenthesized, so a `__alignof` that
	// no `(` follows is the identifier it was lexed as.
	const bool gnu_alignof = at(TT_IDENTIFIER) && peek(1) == OP_LPAREN &&
		(spelling() == "__alignof" || spelling() == "__alignof__");
	if (gnu_alignof || at(KW_TYPEID) || at(KW_ALIGNOF) || at(KW_NOEXCEPT))
	{
		AstNode* node = make(AstKind::TypeTraitExpression);
		node->token = static_cast<std::uint16_t>(gnu_alignof ? KW_ALIGNOF
		                                                     : peek());
		node->text = spelling();
		const bool prefer_type = gnu_alignof || at(KW_ALIGNOF);
		++pos_;
		AstNode* operand = parse_parenthesized_operand(prefer_type);
		if (operand == nullptr)
		{
			return fail(start);
		}
		node->add(operand);
		return parse_postfix_suffixes(node);
	}
	if (at(KW_DYNAMIC_CAST) || at(KW_STATIC_CAST) || at(KW_REINTERPET_CAST) ||
	    at(KW_CONST_CAST))
	{
		AstNode* node = make(AstKind::CastExpression);
		node->token = static_cast<std::uint16_t>(peek());
		node->text = spelling();
		++pos_;
		AstNode* type = nullptr;
		if (!at(OP_LT))
		{
			return fail(start);
		}
		{
			BracketGuard brackets(*this, true);
			++pos_;
			type = parse_type_id();
			if (type == nullptr || !at_close_angle())
			{
				return fail(start);
			}
			++pos_;
		}
		node->add(type);
		if (!at(OP_LPAREN))
		{
			return fail(start);
		}
		AstNode* operand = nullptr;
		{
			BracketGuard brackets(*this, false);
			++pos_;
			operand = parse_expression();
			if (operand == nullptr || !at(OP_RPAREN))
			{
				return fail(start);
			}
			++pos_;
		}
		node->add(operand);
		// 5.2p1: a named cast is a postfix-expression, so a call, a subscript,
		// a member access or an increment may be written on what it produces.
		return parse_postfix_suffixes(node);
	}
	if (at(KW_NEW) || (at(OP_COLON2) && peek(1) == KW_NEW))
	{
		return parse_new_expression();
	}
	if (at(KW_DELETE) || (at(OP_COLON2) && peek(1) == KW_DELETE))
	{
		return parse_delete_expression();
	}
	return parse_postfix_expression();
}

AstNode* AstParser::parse_new_expression()
{
	const Mark start = mark();
	AstNode* node = make(AstKind::NewExpression);
	if (accept(OP_COLON2))
	{
		node->add(make(AstKind::GlobalScope));
	}
	++pos_;
	const Mark before_placement = mark();
	AstNode* placement = nullptr;
	if (at(OP_LPAREN))
	{
		AstNode* arguments = parse_argument_suffix(AstKind::ParenArgumentList);
		if (arguments != nullptr && !arguments->children.empty())
		{
			placement = make_text(AstKind::Placement, spelled(before_placement));
			placement->add(arguments);
		}
		else
		{
			reset(before_placement);
		}
	}
	AstNode* type = parse_new_type();
	if (type == nullptr && placement != nullptr)
	{
		reset(before_placement);
		placement = nullptr;
		type = parse_new_type();
	}
	if (type == nullptr)
	{
		return fail(start);
	}
	node->add(placement);
	node->add(type);
	if (at(OP_LPAREN) || at(OP_LBRACE))
	{
		node->add(parse_initializer());
	}
	return node;
}

// The type a `new` allocates: either a parenthesized type-id or a
// type-specifier-seq with a new-declarator, which is a declarator without the
// function and parenthesized forms so that `new C()` is a call and not a type.
AstNode* AstParser::parse_new_type()
{
	const Mark start = mark();
	if (at(OP_LPAREN))
	{
		++pos_;
		AstNode* type = nullptr;
		{
			BracketGuard brackets(*this, false);
			type = parse_type_id();
		}
		if (type != nullptr && at(OP_RPAREN))
		{
			++pos_;
			return type;
		}
		reset(start);
		return nullptr;
	}
	AstNode* seq = parse_specifier_seq(SpecifierMode::Type);
	if (seq == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::TypeId);
	node->add(seq);
	AstNode* declarator = parse_declarator(DeclaratorForm::New);
	if (declarator != nullptr && !declarator->children.empty())
	{
		node->add(declarator);
	}
	return node;
}

AstNode* AstParser::parse_delete_expression()
{
	const Mark start = mark();
	AstNode* node = make(AstKind::DeleteExpression);
	if (accept(OP_COLON2))
	{
		node->add(make(AstKind::GlobalScope));
	}
	++pos_;
	if (at(OP_LSQUARE) && peek(1) == OP_RSQUARE)
	{
		pos_ += 2;
		node->add(make(AstKind::ArrayDelete));
	}
	AstNode* operand = parse_cast_expression();
	if (operand == nullptr)
	{
		return fail(start);
	}
	node->add(operand);
	return node;
}

// 5.2.3p1 and 5.2.3p3: the operands of an explicit type conversion written in
// functional notation, which are a parenthesized expression-list or the one
// braced-init-list 5.2.3p3 writes.  Both stand where the arguments of a call
// do, because the grammar cannot say whether the name before them is a type;
// the braced form stands as the single clause of that list, which is the one
// thing that tells the two apart where the semantics asks.
AstNode* AstParser::parse_conversion_arguments()
{
	if (!at(OP_LBRACE))
	{
		return parse_argument_suffix(AstKind::ParenArgumentList);
	}
	AstNode* braced = parse_braced_init_list();
	if (braced == nullptr)
	{
		return nullptr;
	}
	AstNode* arguments = make(AstKind::ArgumentList);
	// 5.2.3p3: the braces were written where the parentheses of 5.2.3p1 would
	// be, which is the one thing that tells `T{a}` from `T({a})`.
	arguments->braced = true;
	arguments->add(braced);
	return arguments;
}

AstNode* AstParser::parse_postfix_expression()
{
	AstNode* expression = parse_primary_expression();
	if (expression == nullptr)
	{
		return nullptr;
	}
	return parse_postfix_suffixes(expression);
}

AstNode* AstParser::parse_postfix_suffixes(AstNode* expression)
{
	for (;;)
	{
		if (at(OP_LPAREN))
		{
			AstNode* arguments = parse_argument_suffix(AstKind::ArgumentList);
			if (arguments == nullptr)
			{
				return expression;
			}
			AstNode* node = make(AstKind::CallExpression);
			node->add(expression);
			node->add(arguments);
			expression = node;
			continue;
		}
		if (at(OP_LSQUARE))
		{
			const Mark start = mark();
			++pos_;
			AstNode* index = nullptr;
			{
				BracketGuard brackets(*this, false);
				index = parse_expression();
				if (index == nullptr || !at(OP_RSQUARE))
				{
					reset(start);
					return expression;
				}
				++pos_;
			}
			AstNode* node = make(AstKind::SubscriptExpression);
			node->add(expression);
			node->add(index);
			expression = node;
			continue;
		}
		if (at(OP_DOT) || at(OP_ARROW))
		{
			const Mark start = mark();
			AstNode* node = make(AstKind::MemberExpression);
			node->token = static_cast<std::uint16_t>(peek());
			node->text = spelling();
			++pos_;
			AstNode* member = parse_member_id();
			if (member == nullptr)
			{
				reset(start);
				return expression;
			}
			node->add(expression);
			node->add(member);
			expression = node;
			continue;
		}
		if (at(OP_LBRACE) &&
		    (expression->kind == AstKind::IdExpression ||
		     expression->kind == AstKind::DecltypeSpecifier))
		{
			// 5.2.3p3: a simple-type-specifier or a typename-specifier followed
			// by a braced-init-list is an explicit type conversion, written
			// where a call is written and told from one only by the braces.
			// The grammar cannot say whether the name is a type, so the shape
			// is the one a call takes and the list stands where the arguments
			// do; what the braces mean - 8.5.4's list-initialization rather
			// than 8.5's arguments - is a question for the semantics.
			AstNode* arguments = parse_conversion_arguments();
			if (arguments == nullptr)
			{
				return expression;
			}
			AstNode* node = make(AstKind::CallExpression);
			node->add(expression);
			node->add(arguments);
			expression = node;
			continue;
		}
		if (at(OP_INC) || at(OP_DEC))
		{
			AstNode* node = make(AstKind::PostfixExpression);
			node->token = static_cast<std::uint16_t>(peek());
			node->text = spelling();
			++pos_;
			node->add(expression);
			expression = node;
			continue;
		}
		return expression;
	}
}

AstNode* AstParser::parse_primary_expression()
{
	const unsigned type = peek();
	if (type == KW_TRUE || type == KW_FALSE || type == KW_NULLPTR ||
	    type == KW_THIS)
	{
		return make_terminal(AstKind::KeywordLiteral);
	}
	if (type == TT_LITERAL)
	{
		AstNode* node = make_text(AstKind::Literal, spelling());
		++pos_;
		return node;
	}
	if (is_builtin_type_keyword(type))
	{
		// 5.2.3p1: an explicit type conversion may be written with any
		// simple-type-specifier, and Table 10 spells several of them with more
		// than one keyword, so the whole run of them is the type named.
		std::size_t words = 0;
		std::string named;
		while (is_builtin_type_keyword(peek(words)))
		{
			if (!named.empty())
			{
				named += " ";
			}
			named += spelling(words);
			++words;
		}
		if (peek(words) != OP_LPAREN && peek(words) != OP_LBRACE)
		{
			return parse_id_expression();
		}
		const Mark start = mark();
		AstNode* callee = make_text(AstKind::IdExpression, named);
		pos_ += words;
		AstNode* arguments = parse_conversion_arguments();
		if (arguments == nullptr)
		{
			return fail(start);
		}
		AstNode* node = make(AstKind::CallExpression);
		node->add(callee);
		node->add(arguments);
		return node;
	}
	if (type == KW_DECLTYPE)
	{
		// 5.2.3p1: an explicit type conversion may be written with any
		// simple-type-specifier, and 7.1.6.2 counts a decltype-specifier among
		// them.  The grammar reads the conversion as the call it is written as,
		// so the type stands where a callee does.  A decltype-specifier written
		// anywhere else in an expression is a name, and is read as one.
		const Mark start = mark();
		if (skip_decltype_specifier() && (at(OP_LPAREN) || at(OP_LBRACE)))
		{
			const std::string named = spelled(start);
			reset(start);
			pos_ += 2;
			AstNode* operand = nullptr;
			{
				BracketGuard brackets(*this, false);
				operand = parse_expression();
				if (operand == nullptr || !at(OP_RPAREN))
				{
					return fail(start);
				}
				++pos_;
			}
			AstNode* callee = make_text(AstKind::DecltypeSpecifier, named);
			callee->add(operand);
			AstNode* arguments = parse_conversion_arguments();
			if (arguments == nullptr)
			{
				return fail(start);
			}
			AstNode* node = make(AstKind::CallExpression);
			node->add(callee);
			node->add(arguments);
			return node;
		}
		reset(start);
	}
	if (type == OP_LSQUARE)
	{
		return parse_lambda_expression();
	}
	if (type == OP_LBRACE)
	{
		return parse_braced_init_list();
	}
	if (type == OP_LPAREN)
	{
		const Mark start = mark();
		++pos_;
		AstNode* inner = nullptr;
		{
			BracketGuard brackets(*this, false);
			inner = parse_expression();
			if (inner == nullptr || !at(OP_RPAREN))
			{
				return fail(start);
			}
			++pos_;
		}
		AstNode* node = make(AstKind::ParenthesizedExpression);
		node->add(inner);
		return node;
	}
	return parse_id_expression();
}

AstNode* AstParser::parse_argument_suffix(AstKind kind)
{
	const Mark start = mark();
	if (!at(OP_LPAREN))
	{
		return nullptr;
	}
	AstNode* node = make(kind);
	{
		BracketGuard brackets(*this, false);
		++pos_;
		if (!at(OP_RPAREN) && !parse_initializer_clause_list(node, OP_RPAREN))
		{
			return fail(start);
		}
		if (!at(OP_RPAREN))
		{
			return fail(start);
		}
		++pos_;
	}
	return node;
}

AstNode* AstParser::parse_braced_init_list()
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return nullptr;
	}
	const Mark start = mark();
	if (!at(OP_LBRACE))
	{
		return nullptr;
	}
	AstNode* node = make(AstKind::BracedInitList);
	{
		BracketGuard brackets(*this, false);
		++pos_;
		if (!at(OP_RBRACE) && !parse_initializer_clause_list(node, OP_RBRACE))
		{
			return fail(start);
		}
		accept(OP_COMMA);
		if (!at(OP_RBRACE))
		{
			return fail(start);
		}
		++pos_;
	}
	return node;
}

bool AstParser::parse_initializer_clause_list(AstNode* parent, unsigned closer)
{
	for (;;)
	{
		AstNode* clause = parse_initializer_clause();
		if (clause == nullptr)
		{
			return false;
		}
		parent->add(clause);
		if (!at(OP_COMMA) || tokens_.type(pos_ + 1) == closer)
		{
			return true;
		}
		++pos_;
	}
}

AstNode* AstParser::parse_initializer_clause()
{
	AstNode* clause = at(OP_LBRACE)
		? parse_braced_init_list()
		: parse_assignment_expression();
	if (clause == nullptr || !at(OP_DOTS))
	{
		return clause;
	}
	++pos_;
	AstNode* node = make(AstKind::PackExpansionExpression);
	node->add(clause);
	return node;
}

AstNode* AstParser::parse_lambda_expression()
{
	const Mark start = mark();
	if (!at(OP_LSQUARE))
	{
		return nullptr;
	}
	++pos_;
	if (!skip_balanced(OP_RSQUARE))
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::LambdaExpression);
	node->add(make_text(AstKind::LambdaIntroducer, spelled(start)));
	if (at(OP_LPAREN))
	{
		AstNode* declarator = make(AstKind::LambdaDeclarator);
		AstNode* clause = parse_parameter_clause();
		if (clause == nullptr)
		{
			return fail(start);
		}
		declarator->add(clause);
		if (at(KW_MUTABLE))
		{
			declarator->add(make_terminal(AstKind::LambdaSpecifier));
		}
		if (at(KW_NOEXCEPT))
		{
			++pos_;
			AstNode* specification = make(AstKind::NoexceptSpecification);
			if (at(OP_LPAREN))
			{
				BracketGuard brackets(*this, false);
				++pos_;
				AstNode* condition = parse_expression();
				if (condition == nullptr || !at(OP_RPAREN))
				{
					return fail(start);
				}
				++pos_;
				specification->add(condition);
			}
			declarator->add(specification);
		}
		if (at(OP_ARROW))
		{
			AstNode* trailing = parse_trailing_return_type();
			if (trailing == nullptr)
			{
				return fail(start);
			}
			trailing->text.clear();
			declarator->add(trailing);
		}
		node->add(declarator);
	}
	// A lambda's parameters name objects in its body just as a function's do,
	// which is what makes `x<1>(2)` two comparisons there rather than a call
	// through a template-id.
	ScopeGuard scope(names_);
	if (!node->children.empty())
	{
		declare_parameters(node->children.back());
	}
	AstNode* body = parse_compound_statement();
	if (body == nullptr)
	{
		return fail(start);
	}
	node->add(body);
	return node;
}

bool AstParser::at_template_argument_end() const
{
	return at(OP_COMMA) || at(OP_DOTS) || at_close_angle();
}

// One `template-argument`, read once per position.
//
// The list around it is read once per attempt made at the name in front of it,
// and every attempt reads the same arguments - so the reading below is the same
// question `skip_simple_template_id` memoises one level up, and it is
// remembered the same way.  The nodes it builds are dropped by the rule that
// asks for it, which only wants to know where the argument ended, so a
// remembered answer costs the position alone.
bool AstParser::parse_template_argument()
{
	const Mark start = mark();
	memo_is_current();
	const std::size_t key = template_argument_memo_key(start);
	const std::unordered_map<std::size_t, std::size_t>::const_iterator found =
		template_argument_memo_.find(key);
	if (found != template_argument_memo_.end())
	{
		if (found->second == 0)
		{
			return false;
		}
		pos_ = found->second - 1;
		return true;
	}
	const bool matched = parse_template_argument_body();
	// A reading the depth limit cut short is not a fact about the position, and
	// the refusal is sticky, so the whole descent is already failing.  An
	// argument holding a lambda declares names, which is what takes the table
	// away, and `memo_is_current` says so.
	if (!depth_.overflowed() && template_id_memo_version_ == names_.version())
	{
		template_argument_memo_[key] = matched ? pos_ + 1 : 0;
	}
	return matched;
}

// The grammar writes an argument as either a type-id or an expression and
// leaves for a later assignment to choose between them.  An argument that
// neither reading completes is read once more with its outermost `<` as the
// relational operator it also spells, which is how `C<a, b < c>` closes.
bool AstParser::parse_template_argument_body()
{
	const Mark start = mark();
	if (parse_type_id() != nullptr && at_template_argument_end())
	{
		return true;
	}
	reset(start);
	if (parse_assignment_expression() != nullptr && at_template_argument_end())
	{
		return true;
	}
	reset(start);
	const int saved = template_id_veto_depth_;
	template_id_veto_depth_ = bracket_depth_;
	const bool matched = parse_assignment_expression() != nullptr &&
		at_template_argument_end();
	template_id_veto_depth_ = saved;
	if (matched)
	{
		return true;
	}
	reset(start);
	return false;
}
