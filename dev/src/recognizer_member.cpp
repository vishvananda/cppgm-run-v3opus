#include "recognizer.h"

// Class bodies, enumerations and templates.
//
// A class-head and an elaborated-type-specifier share their whole prefix, so
// the head is only accepted when a `{` follows it; that one lookahead keeps
// `class C x;` and `class C { };` apart without backtracking through a class
// body twice.

bool Recognizer::parse_class_specifier()
{
	const Mark start = mark();
	if (!parse_class_head())
	{
		return false;
	}
	if (!open_bracket(OP_LBRACE))
	{
		return fail(start);
	}
	while (!at(OP_RBRACE))
	{
		if (!parse_member_specification())
		{
			return fail(start);
		}
	}
	++pos_;
	angle_ = start.angle;
	return true;
}

// class-key attribute-specifier* class-head-name class-virt-specifier?
//     base-clause?
// class-key attribute-specifier* base-clause?
bool Recognizer::parse_class_head()
{
	const Mark start = mark();
	if (!accept(KW_CLASS) && !accept(KW_STRUCT) && !accept(KW_UNION))
	{
		return false;
	}
	parse_attribute_specifier_seq();
	const Mark after_attributes = mark();

	if (parse_class_head_name())
	{
		if (at_identifier(kIsFinal))
		{
			++pos_;
		}
		parse_base_clause();
		if (at(OP_LBRACE))
		{
			return true;
		}
		reset(after_attributes);
	}
	parse_base_clause();
	if (at(OP_LBRACE))
	{
		return true;
	}
	return fail(start);
}

bool Recognizer::parse_class_head_name()
{
	const Mark start = mark();
	if (parse_nested_name_specifier() && parse_class_name())
	{
		return true;
	}
	reset(start);
	return parse_class_name();
}

bool Recognizer::parse_base_clause()
{
	const Mark start = mark();
	if (!accept(OP_COLON) || !parse_base_specifier())
	{
		return fail(start);
	}
	accept(OP_DOTS);
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_base_specifier())
		{
			reset(item);
			break;
		}
		accept(OP_DOTS);
	}
	return true;
}

// attribute-specifier* (KW_VIRTUAL access-specifier?
//                       | access-specifier KW_VIRTUAL?)? base-type-specifier
bool Recognizer::parse_base_specifier()
{
	const Mark start = mark();
	parse_attribute_specifier_seq();
	if (accept(KW_VIRTUAL))
	{
		if (at(KW_PRIVATE) || at(KW_PROTECTED) || at(KW_PUBLIC))
		{
			++pos_;
		}
	}
	else if (at(KW_PRIVATE) || at(KW_PROTECTED) || at(KW_PUBLIC))
	{
		++pos_;
		accept(KW_VIRTUAL);
	}
	if (!parse_class_or_decltype())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_class_or_decltype()
{
	if (at(KW_DECLTYPE))
	{
		return parse_decltype_specifier();
	}
	const Mark start = mark();
	if (parse_nested_name_specifier() && parse_class_name())
	{
		return true;
	}
	reset(start);
	return parse_class_name();
}

bool Recognizer::parse_member_specification()
{
	if (at(KW_PRIVATE) || at(KW_PROTECTED) || at(KW_PUBLIC))
	{
		const Mark start = mark();
		++pos_;
		if (accept(OP_COLON))
		{
			return true;
		}
		return fail(start);
	}
	return parse_member_declaration();
}

bool Recognizer::parse_member_declaration()
{
	switch (peek())
	{
	case KW_USING:
		return parse_alias_declaration() || parse_using_declaration();
	case KW_STATIC_ASSERT:
		return parse_static_assert_declaration();
	case KW_TEMPLATE:
		return parse_template_declaration();
	default:
		break;
	}

	const Mark start = mark();
	if (parse_function_definition())
	{
		accept(OP_SEMICOLON);
		return true;
	}
	reset(start);
	parse_attribute_specifier_seq();
	if (!parse_decl_specifier_seq())
	{
		return fail(start);
	}
	parse_member_declarator_list();
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_member_declarator_list()
{
	if (!parse_member_declarator())
	{
		return false;
	}
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_member_declarator())
		{
			reset(item);
			break;
		}
	}
	return true;
}

// declarator virt-specifier* pure-specifier?
// declarator brace-or-equal-initializer?
// TT_IDENTIFIER? attribute-specifier* OP_COLON constant-expression
bool Recognizer::parse_member_declarator()
{
	const Mark start = mark();

	// The bit-field form is the only one with a `:` where a declarator would
	// otherwise end, so it is recognised first.
	accept(TT_IDENTIFIER);
	parse_attribute_specifier_seq();
	if (accept(OP_COLON) && parse_constant_expression())
	{
		return true;
	}
	reset(start);

	if (!parse_declarator())
	{
		return false;
	}
	parse_virt_specifiers();
	if (at(OP_ASS) && peek(1) == TT_LITERAL && (flags_at(1) & kIsZero) != 0)
	{
		pos_ += 2;  // pure-specifier
		return true;
	}
	parse_brace_or_equal_initializer();
	return true;
}

// enum-head OP_LBRACE enumerator-list? OP_COMMA? OP_RBRACE
bool Recognizer::parse_enum_specifier()
{
	const Mark start = mark();
	if (!parse_enum_head() || !open_bracket(OP_LBRACE))
	{
		return fail(start);
	}
	if (parse_enumerator_list())
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

// enum-key attribute-specifier* (nested-name-specifier TT_IDENTIFIER
//                                | TT_IDENTIFIER?) enum-base?
bool Recognizer::parse_enum_head()
{
	const Mark start = mark();
	if (!parse_enum_key())
	{
		return false;
	}
	parse_attribute_specifier_seq();
	const Mark after_attributes = mark();
	if (!parse_nested_name_specifier() || !accept(TT_IDENTIFIER))
	{
		reset(after_attributes);
		accept(TT_IDENTIFIER);
	}
	parse_enum_base();
	if (!at(OP_LBRACE))
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_enum_key()
{
	if (!accept(KW_ENUM))
	{
		return false;
	}
	if (!accept(KW_CLASS))
	{
		accept(KW_STRUCT);
	}
	return true;
}

bool Recognizer::parse_enum_base()
{
	const Mark start = mark();
	if (!accept(OP_COLON) || !parse_type_specifier_seq())
	{
		return fail(start);
	}
	return true;
}

// enumerator-definition (OP_COMMA enumerator-definition)*
bool Recognizer::parse_enumerator_list()
{
	if (!at(TT_IDENTIFIER))
	{
		return false;
	}
	++pos_;
	const Mark after_value = mark();
	if (accept(OP_ASS) && !parse_constant_expression())
	{
		reset(after_value);
	}

	while (at(OP_COMMA) && peek(1) == TT_IDENTIFIER)
	{
		pos_ += 2;
		const Mark after_next = mark();
		if (accept(OP_ASS) && !parse_constant_expression())
		{
			reset(after_next);
		}
	}
	return true;
}

// enum-key attribute-specifier* TT_IDENTIFIER enum-base? OP_SEMICOLON
bool Recognizer::parse_opaque_enum_declaration()
{
	const Mark start = mark();
	if (!parse_enum_key())
	{
		return false;
	}
	parse_attribute_specifier_seq();
	if (!accept(TT_IDENTIFIER))
	{
		return fail(start);
	}
	parse_enum_base();
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

// KW_TEMPLATE OP_LT template-parameter-list close-angle-bracket declaration
bool Recognizer::parse_template_declaration()
{
	const Mark start = mark();
	if (!accept(KW_TEMPLATE) || !accept(OP_LT))
	{
		return fail(start);
	}
	angle_ = true;
	if (!parse_template_parameter_list() || !parse_close_angle_bracket())
	{
		return fail(start);
	}
	angle_ = start.angle;
	if (!parse_declaration())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_template_parameter_list()
{
	if (!parse_template_parameter())
	{
		return false;
	}
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_template_parameter())
		{
			reset(item);
			break;
		}
	}
	return true;
}

bool Recognizer::parse_template_parameter()
{
	const Mark start = mark();
	if (parse_type_parameter() && at_parameter_end())
	{
		return true;
	}
	reset(start);
	return parse_parameter_declaration();
}

// KW_CLASS and KW_TYPENAME introduce a type parameter; KW_TEMPLATE introduces
// a template template parameter, whose own parameter list nests.
bool Recognizer::parse_type_parameter()
{
	const Mark start = mark();
	if (accept(KW_TEMPLATE))
	{
		if (!accept(OP_LT))
		{
			return fail(start);
		}
		angle_ = true;
		if (!parse_template_parameter_list() || !parse_close_angle_bracket())
		{
			return fail(start);
		}
		angle_ = start.angle;
		if (!accept(KW_CLASS))
		{
			return fail(start);
		}
	}
	else if (!accept(KW_CLASS) && !accept(KW_TYPENAME))
	{
		return false;
	}

	if (accept(OP_DOTS))
	{
		accept(TT_IDENTIFIER);
		return true;
	}
	accept(TT_IDENTIFIER);
	const Mark after_name = mark();
	if (accept(OP_ASS))
	{
		// A default argument is a type-id for a type parameter and an
		// id-expression for a template template parameter.
		if (!parse_type_id() && !parse_id_expression())
		{
			reset(after_name);
		}
	}
	return true;
}

// KW_EXTERN? KW_TEMPLATE declaration
bool Recognizer::parse_explicit_instantiation()
{
	const Mark start = mark();
	accept(KW_EXTERN);
	if (!accept(KW_TEMPLATE) || !parse_declaration())
	{
		return fail(start);
	}
	return true;
}

// KW_TEMPLATE OP_LT close-angle-bracket declaration
bool Recognizer::parse_explicit_specialization()
{
	const Mark start = mark();
	if (!accept(KW_TEMPLATE) || !accept(OP_LT))
	{
		return fail(start);
	}
	angle_ = true;
	if (!parse_close_angle_bracket())
	{
		return fail(start);
	}
	angle_ = start.angle;
	if (!parse_declaration())
	{
		return fail(start);
	}
	return true;
}
