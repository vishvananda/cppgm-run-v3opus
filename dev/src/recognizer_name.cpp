#include "recognizer.h"

#include <algorithm>

// Names, template ids and the angle bracket rule of 14.2.3.
//
// PA6 mock name lookup is a fact of the spelling, so a `*-name` nonterminal is
// one flag test on the current token.  The one place lookup changes control
// flow rather than just accepting or rejecting is the angle commit: an
// identifier that is a template-name and is followed by `<` can only be a
// simple-template-id, so no alternative may fall back to reading it as a plain
// identifier.  That is what makes `T1 < 2` ill-formed, and it is also what
// bounds the work a deeply nested template argument list can cost.

bool Recognizer::parse_type_name()
{
	if (!at(TT_IDENTIFIER))
	{
		return false;
	}
	if ((flags_at() & kNameTemplate) != 0 && peek(1) == OP_LT)
	{
		return parse_simple_template_id();
	}
	if ((flags_at() & (kNameClass | kNameEnum | kNameTypedef)) != 0)
	{
		++pos_;
		return true;
	}
	return false;
}

bool Recognizer::parse_class_name()
{
	if (!at(TT_IDENTIFIER))
	{
		return false;
	}
	if ((flags_at() & kNameTemplate) != 0 && peek(1) == OP_LT)
	{
		return parse_simple_template_id();
	}
	if ((flags_at() & kNameClass) != 0)
	{
		++pos_;
		return true;
	}
	return false;
}

bool Recognizer::parse_simple_template_id()
{
	return memoize(kMemoSimpleTemplateId, &Recognizer::parse_simple_template_id_body);
}

// template-name OP_LT template-argument-list? close-angle-bracket
bool Recognizer::parse_simple_template_id_body()
{
	const Mark start = mark();
	if (!at_identifier(kNameTemplate) || peek(1) != OP_LT)
	{
		return false;
	}
	pos_ += 2;
	angle_ = true;
	parse_template_argument_list();
	if (!parse_close_angle_bracket())
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

// One `>` of an `>>` closes one pair, so the two halves of a right shift are
// separate terminals and either may close the innermost pair on its own.
bool Recognizer::parse_close_angle_bracket()
{
	return accept(OP_GT) || accept(ST_RSHIFT_1) || accept(ST_RSHIFT_2);
}

bool Recognizer::parse_template_argument_list()
{
	if (!parse_template_argument_dots())
	{
		return false;
	}
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_template_argument_dots())
		{
			reset(item);
			break;
		}
	}
	return true;
}

bool Recognizer::parse_template_argument_dots()
{
	if (!parse_template_argument())
	{
		return false;
	}
	accept(OP_DOTS);
	return true;
}

bool Recognizer::parse_template_argument()
{
	return memoize(kMemoTemplateArgument, &Recognizer::parse_template_argument_body);
}

// constant-expression | type-id | id-expression, decided by which alternative
// reaches the end of an argument: `TC1<C*>` is a type and `TC1<C+1>` is an
// expression, and only the token after the match tells them apart.
bool Recognizer::parse_template_argument_body()
{
	const Mark start = mark();
	if (parse_type_id() && at_template_argument_end())
	{
		return true;
	}
	reset(start);
	if (parse_constant_expression() && at_template_argument_end())
	{
		return true;
	}
	reset(start);
	if (parse_id_expression() && at_template_argument_end())
	{
		return true;
	}
	return fail(start);
}

bool Recognizer::parse_id_expression()
{
	const Mark start = mark();
	// qualified-id: nested-name-specifier KW_TEMPLATE? unqualified-id
	if (parse_nested_name_specifier())
	{
		accept(KW_TEMPLATE);
		if (parse_unqualified_id())
		{
			return true;
		}
		reset(start);
	}
	return parse_unqualified_id();
}

// TT_IDENTIFIER | operator-id | OP_COMPL (class-name | decltype-specifier)
// | simple-template-id
//
// Every alternative is a token test or a rule that is remembered on its own,
// so re-entering this one costs a constant and it is not itself remembered.
bool Recognizer::parse_unqualified_id()
{
	const Mark start = mark();
	if (accept(OP_COMPL))
	{
		if (parse_class_name() || parse_decltype_specifier())
		{
			return true;
		}
		return fail(start);
	}
	if (at(KW_OPERATOR))
	{
		return parse_operator_id();
	}
	if (!at(TT_IDENTIFIER))
	{
		return false;
	}
	if ((flags_at() & kNameTemplate) != 0 && peek(1) == OP_LT)
	{
		return parse_simple_template_id();
	}
	++pos_;
	return true;
}

// The three `operator` forms, plus the template-id built on the first two.
bool Recognizer::parse_operator_id()
{
	const Mark start = mark();
	if (parse_literal_operator_id() || parse_operator_function_id())
	{
		if (at(OP_LT))
		{
			const Mark after_id = mark();
			++pos_;
			angle_ = true;
			parse_template_argument_list();
			if (parse_close_angle_bracket())
			{
				angle_ = start.angle;
				return true;
			}
			reset(after_id);
		}
		return true;
	}
	reset(start);
	return parse_conversion_function_id();
}

// KW_OPERATOR ST_EMPTYSTR TT_IDENTIFIER
bool Recognizer::parse_literal_operator_id()
{
	const Mark start = mark();
	if (!accept(KW_OPERATOR))
	{
		return false;
	}
	if (!at_literal(kIsEmptyString))
	{
		return fail(start);
	}
	++pos_;
	if (!accept(TT_IDENTIFIER))
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_operator_function_id()
{
	const Mark start = mark();
	if (!accept(KW_OPERATOR))
	{
		return false;
	}
	switch (peek())
	{
	case KW_NEW:
	case KW_DELETE:
		++pos_;
		if (at(OP_LSQUARE) && peek(1) == OP_RSQUARE)
		{
			pos_ += 2;
		}
		return true;

	case OP_LPAREN:
		if (peek(1) != OP_RPAREN)
		{
			return fail(start);
		}
		pos_ += 2;
		return true;

	case OP_LSQUARE:
		if (peek(1) != OP_RSQUARE)
		{
			return fail(start);
		}
		pos_ += 2;
		return true;

	case ST_RSHIFT_1:
		if (peek(1) != ST_RSHIFT_2)
		{
			return fail(start);
		}
		pos_ += 2;
		return true;

	case OP_PLUS: case OP_MINUS: case OP_STAR: case OP_DIV: case OP_MOD:
	case OP_XOR: case OP_AMP: case OP_BOR: case OP_COMPL: case OP_LNOT:
	case OP_ASS: case OP_LT: case OP_GT: case OP_PLUSASS: case OP_MINUSASS:
	case OP_STARASS: case OP_DIVASS: case OP_MODASS: case OP_XORASS:
	case OP_BANDASS: case OP_BORASS: case OP_LSHIFT: case OP_RSHIFTASS:
	case OP_LSHIFTASS: case OP_EQ: case OP_NE: case OP_LE: case OP_GE:
	case OP_LAND: case OP_LOR: case OP_INC: case OP_DEC: case OP_COMMA:
	case OP_ARROWSTAR: case OP_ARROW:
		++pos_;
		return true;

	default:
		return fail(start);
	}
}

// KW_OPERATOR conversion-type-id, where conversion-type-id is
// type-specifier-seq ptr-operator*
bool Recognizer::parse_conversion_function_id()
{
	const Mark start = mark();
	if (!accept(KW_OPERATOR) || !parse_type_specifier_seq())
	{
		return fail(start);
	}
	parse_ptr_operators();
	return true;
}

bool Recognizer::parse_nested_name_specifier()
{
	return memoize(kMemoNestedNameSpecifier,
	               &Recognizer::parse_nested_name_specifier_body);
}

bool Recognizer::parse_nested_name_specifier_body()
{
	if (!parse_nested_name_specifier_root())
	{
		return false;
	}
	while (parse_nested_name_specifier_suffix())
	{
	}
	return true;
}

bool Recognizer::parse_nested_name_specifier_root()
{
	if (accept(OP_COLON2))
	{
		return true;
	}
	const Mark start = mark();
	if (parse_type_name() && accept(OP_COLON2))
	{
		return true;
	}
	reset(start);
	if (at_identifier(kNameNamespace) && peek(1) == OP_COLON2)
	{
		pos_ += 2;
		return true;
	}
	if (parse_decltype_specifier() && accept(OP_COLON2))
	{
		return true;
	}
	return fail(start);
}

bool Recognizer::parse_nested_name_specifier_suffix()
{
	if (at(TT_IDENTIFIER) && peek(1) == OP_COLON2)
	{
		pos_ += 2;
		return true;
	}
	const Mark start = mark();
	accept(KW_TEMPLATE);
	if (parse_simple_template_id() && accept(OP_COLON2))
	{
		return true;
	}
	return fail(start);
}

// KW_DECLTYPE OP_LPAREN expression OP_RPAREN
bool Recognizer::parse_decltype_specifier()
{
	const Mark start = mark();
	if (!accept(KW_DECLTYPE) || !open_bracket(OP_LPAREN) || !parse_expression() ||
	    !accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

// Where every prefix of the nested-name-specifier at the cursor ends, longest
// first, followed by the cursor itself for the empty prefix.
//
// A nested-name-specifier is a greedy loop, but its last step can belong to
// the name that follows it instead: in `union T1<>::a1` the `T1<>::` qualifies
// the declarator-id `::a1` rather than the type.  A caller that needs a name
// right after the qualification therefore has to be able to hand a step back.
// Every step leaves the angle bracket state as it found it, so a prefix is
// restored by moving the position alone.
void Recognizer::nested_name_specifier_ends(std::vector<std::size_t>& ends)
{
	const Mark start = mark();
	if (parse_nested_name_specifier_root())
	{
		ends.push_back(pos_);
		while (parse_nested_name_specifier_suffix())
		{
			ends.push_back(pos_);
		}
	}
	reset(start);
	std::reverse(ends.begin(), ends.end());
	ends.push_back(start.pos);
}

// KW_TYPENAME nested-name-specifier (KW_TEMPLATE? simple-template-id
//                                    | TT_IDENTIFIER)
bool Recognizer::parse_typename_specifier()
{
	const Mark start = mark();
	if (!accept(KW_TYPENAME))
	{
		return false;
	}

	std::vector<std::size_t> ends;
	nested_name_specifier_ends(ends);
	ends.pop_back();  // the qualification is not optional here
	for (std::size_t i = 0; i < ends.size(); ++i)
	{
		pos_ = ends[i];
		const Mark after_names = mark();
		accept(KW_TEMPLATE);
		if (parse_simple_template_id())
		{
			return true;
		}
		reset(after_names);
		if (accept(TT_IDENTIFIER))
		{
			return true;
		}
	}
	return fail(start);
}

// class-key attribute-specifier* nested-name-specifier? TT_IDENTIFIER
// class-key nested-name-specifier? KW_TEMPLATE? simple-template-id
// KW_ENUM nested-name-specifier? TT_IDENTIFIER
bool Recognizer::parse_elaborated_type_specifier()
{
	const Mark start = mark();
	if (accept(KW_ENUM))
	{
		std::vector<std::size_t> ends;
		nested_name_specifier_ends(ends);
		for (std::size_t i = 0; i < ends.size(); ++i)
		{
			pos_ = ends[i];
			if (accept(TT_IDENTIFIER))
			{
				return true;
			}
		}
		return fail(start);
	}
	if (!accept(KW_CLASS) && !accept(KW_STRUCT) && !accept(KW_UNION))
	{
		return false;
	}
	parse_attribute_specifier_seq();

	// At one qualification the simple-template-id form is tried first, because
	// the identifier form would otherwise match its template-name and leave
	// the argument list behind.  The whole qualification is tried before any
	// shorter one, so a name that can read as part of the type keeps doing so.
	std::vector<std::size_t> ends;
	nested_name_specifier_ends(ends);
	for (std::size_t i = 0; i < ends.size(); ++i)
	{
		pos_ = ends[i];
		const Mark after_names = mark();
		accept(KW_TEMPLATE);
		if (parse_simple_template_id())
		{
			return true;
		}
		reset(after_names);
		if (accept(TT_IDENTIFIER))
		{
			return true;
		}
	}
	return fail(start);
}

// nested-name-specifier? OP_COMPL type-name | OP_COMPL decltype-specifier
bool Recognizer::parse_pseudo_destructor_name()
{
	const Mark start = mark();
	parse_nested_name_specifier();
	if (!accept(OP_COMPL))
	{
		return fail(start);
	}
	if (parse_type_name())
	{
		return true;
	}
	if (start.pos + 1 == pos_ && parse_decltype_specifier())
	{
		return true;
	}
	return fail(start);
}
