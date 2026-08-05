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

// nested-name-specifier? class-name | decltype-specifier
//
// A decltype-specifier can also open the nested-name-specifier, as in
// `decltype(x)::C1`, so the qualified form is tried first.
bool Recognizer::parse_class_or_decltype()
{
	const Mark start = mark();
	if (parse_nested_name_specifier() && parse_class_name())
	{
		return true;
	}
	reset(start);
	return parse_class_name() || parse_decltype_specifier();
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

	// As at namespace scope, the definition only stands when a member can
	// follow it: a comma continues the declarator list of the other reading.
	const Mark start = mark();
	if (parse_function_definition() && !at(OP_COMMA))
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
	parse_enum_base(OP_LBRACE);
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

// OP_COLON type-specifier-seq, where `closer` is the token that has to follow.
//
// A type-specifier can define a type, and a definition written here would take
// the `{` that opens the enumeration with it: in `enum a1 : enum a1 { }` the
// base is the elaborated `enum a1` and the braces are the body, not an
// enumeration of its own.  So the defining reading only stands when the
// declaration can still close after it.
bool Recognizer::parse_enum_base(unsigned closer)
{
	const Mark start = mark();
	if (!accept(OP_COLON))
	{
		return false;
	}
	const Mark after_colon = mark();
	if (parse_type_specifier_seq() && at(closer))
	{
		return true;
	}
	reset(after_colon);
	if (parse_trailing_type_specifier_seq() && at(closer))
	{
		return true;
	}
	reset(after_colon);
	if (!parse_trailing_type_specifier_seq())
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
	parse_enum_base(OP_SEMICOLON);
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

// A template template parameter carries a parameter list of its own, which is
// the one cycle in this file that does not pass a memoized rule.
bool Recognizer::parse_template_parameter_list()
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return false;
	}

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

// type-parameter | parameter-declaration
//
// The two share their whole prefix -- `class T1` is a type-parameter and also
// an elaborated-type-specifier -- so the alternative is the one that ends
// where the list can continue or close.  That is what reads the pack in
// `template<class T1...>` as an abstract-pack-declarator, since a
// type-parameter can only carry its `...` before the name.
bool Recognizer::parse_template_parameter()
{
	const Mark start = mark();
	if (parse_type_parameter() && at_template_parameter_end())
	{
		return true;
	}
	reset(start);
	if (parse_parameter_declaration() && at_template_parameter_end())
	{
		return true;
	}
	return fail(start);
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
		// id-expression for a template template parameter.  A type-id is a
		// prefix of many id-expressions -- `E1` of `E1::a1` -- so each
		// alternative has to reach the end of the parameter to stand.
		const Mark after_assign = mark();
		if (!(parse_type_id() && at_template_parameter_end()))
		{
			reset(after_assign);
			if (!(parse_id_expression() && at_template_parameter_end()))
			{
				reset(after_name);
			}
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
