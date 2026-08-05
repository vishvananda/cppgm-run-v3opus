#include "recognizer.h"

// Specifier sequences, declarators and the type-id built from them.
//
// The three specifier sequences of the grammar differ only in which specifiers
// they admit, so they share one loop.  That loop carries the semantic rule the
// handout states: a type-name joins a specifier sequence only while no
// type-specifier other than a cv-qualifier has been seen, which is what stops
// `C c` from reading `c` as a second type and what makes `int C1` declare
// `C1`.

bool Recognizer::parse_decl_specifier_seq()
{
	return memoize(kMemoDeclSpecifierSeq, &Recognizer::parse_decl_specifier_seq_body);
}

bool Recognizer::parse_decl_specifier_seq_body()
{
	return parse_specifier_seq(SpecifierMode::Decl);
}

bool Recognizer::parse_type_specifier_seq()
{
	return memoize(kMemoTypeSpecifierSeq, &Recognizer::parse_type_specifier_seq_body);
}

bool Recognizer::parse_type_specifier_seq_body()
{
	return parse_specifier_seq(SpecifierMode::Type);
}

bool Recognizer::parse_trailing_type_specifier_seq()
{
	return memoize(kMemoTrailingTypeSpecifierSeq,
	               &Recognizer::parse_trailing_type_specifier_seq_body);
}

bool Recognizer::parse_trailing_type_specifier_seq_body()
{
	return parse_specifier_seq(SpecifierMode::Trailing);
}

bool Recognizer::parse_specifier_seq(SpecifierMode mode)
{
	bool seen_type = false;
	std::size_t count = 0;
	while (parse_specifier(mode, seen_type))
	{
		++count;
	}
	if (count == 0)
	{
		return false;
	}
	parse_attribute_specifier_seq();
	return true;
}

bool Recognizer::parse_specifier(SpecifierMode mode, bool& seen_type)
{
	switch (peek())
	{
	case KW_FRIEND: case KW_TYPEDEF: case KW_CONSTEXPR:
	case KW_REGISTER: case KW_STATIC: case KW_THREAD_LOCAL: case KW_EXTERN:
	case KW_MUTABLE: case KW_INLINE: case KW_VIRTUAL: case KW_EXPLICIT:
		// storage-class-specifier, function-specifier and the three keywords
		// that are decl-specifiers on their own.
		if (mode != SpecifierMode::Decl)
		{
			return false;
		}
		++pos_;
		return true;

	case KW_CONST:
	case KW_VOLATILE:
		// A cv-qualifier is a type-specifier that does not name the type, so
		// it never closes the sequence to a following type-name.
		++pos_;
		return true;

	case KW_CHAR: case KW_CHAR16_T: case KW_CHAR32_T: case KW_WCHAR_T:
	case KW_BOOL: case KW_SHORT: case KW_INT: case KW_LONG: case KW_SIGNED:
	case KW_UNSIGNED: case KW_FLOAT: case KW_DOUBLE: case KW_VOID: case KW_AUTO:
		// The keyword simple-type-specifiers combine with each other, so they
		// are admitted whether or not a type has been named.
		++pos_;
		seen_type = true;
		return true;

	default:
		break;
	}

	const Mark start = mark();
	switch (peek())
	{
	case KW_CLASS:
	case KW_STRUCT:
	case KW_UNION:
		if (mode != SpecifierMode::Trailing && parse_class_specifier())
		{
			seen_type = true;
			return true;
		}
		reset(start);
		if (parse_elaborated_type_specifier())
		{
			seen_type = true;
			return true;
		}
		return fail(start);

	case KW_ENUM:
		if (mode != SpecifierMode::Trailing && parse_enum_specifier())
		{
			seen_type = true;
			return true;
		}
		reset(start);
		if (parse_elaborated_type_specifier())
		{
			seen_type = true;
			return true;
		}
		return fail(start);

	case KW_TYPENAME:
		if (parse_typename_specifier())
		{
			seen_type = true;
			return true;
		}
		return fail(start);

	default:
		break;
	}

	// A type-name is the one type-specifier that is spelled the way a
	// declarator is, so it is the one the handout's rule is about: it joins
	// the sequence only while no type has been named.  That is what makes the
	// second name of `C1 C2;` the declarator.  The specifiers that cannot be
	// mistaken for a declarator -- a class, enum, elaborated or typename
	// specifier -- are not restricted, because nothing is decided by holding
	// them out.
	if (seen_type)
	{
		return false;
	}
	if (parse_simple_type_specifier())
	{
		seen_type = true;
		return true;
	}
	return false;
}

bool Recognizer::parse_simple_type_specifier()
{
	switch (peek())
	{
	case KW_CHAR: case KW_CHAR16_T: case KW_CHAR32_T: case KW_WCHAR_T:
	case KW_BOOL: case KW_SHORT: case KW_INT: case KW_LONG: case KW_SIGNED:
	case KW_UNSIGNED: case KW_FLOAT: case KW_DOUBLE: case KW_VOID: case KW_AUTO:
		++pos_;
		return true;
	default:
		break;
	}

	// nested-name-specifier? type-name
	// nested-name-specifier KW_TEMPLATE simple-template-id
	// decltype-specifier
	//
	// A decltype-specifier is both a specifier of its own and a
	// nested-name-specifier root, so `decltype(x)::Y1` has to be offered to
	// the qualified form before the bare one takes the `decltype(x)`.  The
	// qualifications are tried longest first and the unqualified type-name
	// last, so `::E1::a1` reads as the type `::E1` followed by the declarator
	// `::a1` when `a1` names nothing.
	const Mark start = mark();
	std::vector<std::size_t> ends;
	nested_name_specifier_ends(ends);
	for (std::size_t i = 0; i < ends.size(); ++i)
	{
		pos_ = ends[i];
		const Mark after_names = mark();
		if (accept(KW_TEMPLATE) && parse_simple_template_id())
		{
			return true;
		}
		reset(after_names);
		if (parse_type_name())
		{
			return true;
		}
	}
	reset(start);
	return parse_decltype_specifier();
}

bool Recognizer::parse_declarator()
{
	return memoize(kMemoDeclarator, &Recognizer::parse_declarator_body);
}

// ptr-declarator | noptr-declarator trailing-return-type
bool Recognizer::parse_declarator_body()
{
	const Mark start = mark();
	const std::size_t operators = parse_ptr_operators();
	if (!parse_noptr_declarator())
	{
		return fail(start);
	}
	if (operators == 0 && at(OP_ARROW))
	{
		const Mark after_noptr = mark();
		if (!parse_trailing_return_type())
		{
			reset(after_noptr);
		}
	}
	return true;
}

// A parenthesized declarator nests through here without passing a memoized
// rule, so this is where that cycle counts its depth.
bool Recognizer::parse_ptr_declarator()
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return false;
	}

	parse_ptr_operators();
	return parse_noptr_declarator();
}

bool Recognizer::parse_noptr_declarator()
{
	if (!parse_noptr_declarator_root())
	{
		return false;
	}
	while (parse_noptr_declarator_suffix())
	{
	}
	return true;
}

// declarator-id attribute-specifier* | OP_LPAREN ptr-declarator OP_RPAREN
bool Recognizer::parse_noptr_declarator_root()
{
	const Mark start = mark();
	if (at(OP_LPAREN))
	{
		open_bracket(OP_LPAREN);
		if (parse_ptr_declarator() && accept(OP_RPAREN))
		{
			angle_ = start.angle;
			return true;
		}
		reset(start);
	}
	if (!parse_declarator_id())
	{
		return false;
	}
	parse_attribute_specifier_seq();
	return true;
}

// parameters-and-qualifiers
// OP_LSQUARE constant-expression? OP_RSQUARE attribute-specifier*
bool Recognizer::parse_noptr_declarator_suffix()
{
	if (at(OP_LPAREN))
	{
		return parse_parameters_and_qualifiers();
	}
	const Mark start = mark();
	if (!open_bracket(OP_LSQUARE))
	{
		return false;
	}
	parse_constant_expression();
	if (!accept(OP_RSQUARE))
	{
		return fail(start);
	}
	angle_ = start.angle;
	parse_attribute_specifier_seq();
	return true;
}

bool Recognizer::parse_declarator_id()
{
	const Mark start = mark();
	accept(OP_DOTS);
	if (!parse_id_expression())
	{
		return fail(start);
	}
	return true;
}

std::size_t Recognizer::parse_ptr_operators()
{
	std::size_t count = 0;
	while (parse_ptr_operator())
	{
		++count;
	}
	return count;
}

bool Recognizer::parse_ptr_operator()
{
	if (accept(OP_AMP) || accept(OP_LAND))
	{
		parse_attribute_specifier_seq();
		return true;
	}
	if (accept(OP_STAR))
	{
		parse_attribute_specifier_seq();
		parse_cv_qualifiers();
		return true;
	}
	const Mark start = mark();
	if (parse_nested_name_specifier() && accept(OP_STAR))
	{
		parse_attribute_specifier_seq();
		parse_cv_qualifiers();
		return true;
	}
	return fail(start);
}

bool Recognizer::parse_cv_qualifiers()
{
	while (at(KW_CONST) || at(KW_VOLATILE))
	{
		++pos_;
	}
	return true;
}

bool Recognizer::parse_parameters_and_qualifiers()
{
	return memoize(kMemoParametersAndQualifiers,
	               &Recognizer::parse_parameters_and_qualifiers_body);
}

// OP_LPAREN parameter-declaration-clause OP_RPAREN cv-qualifier*
//     ref-qualifier? exception-specification? attribute-specifier*
bool Recognizer::parse_parameters_and_qualifiers_body()
{
	const Mark start = mark();
	if (!open_bracket(OP_LPAREN) || !parse_parameter_declaration_clause() ||
	    !accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	parse_cv_qualifiers();
	if (!accept(OP_AMP))
	{
		accept(OP_LAND);
	}
	parse_exception_specification();
	parse_attribute_specifier_seq();
	return true;
}

// parameter-declaration-list? OP_DOTS?
// parameter-declaration-list OP_COMMA OP_DOTS
bool Recognizer::parse_parameter_declaration_clause()
{
	parse_parameter_declaration_list();
	if (at(OP_COMMA) && peek(1) == OP_DOTS)
	{
		pos_ += 2;
		return true;
	}
	accept(OP_DOTS);
	return true;
}

bool Recognizer::parse_parameter_declaration_list()
{
	if (!parse_parameter_declaration())
	{
		return false;
	}
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_parameter_declaration())
		{
			reset(item);
			break;
		}
	}
	return true;
}

// attribute-specifier* decl-specifier-seq
//     (declarator | abstract-declarator?) (OP_ASS initializer-clause)?
//
// A named and an unnamed parameter start the same way, so the alternative is
// chosen by which one ends where a parameter can end.
bool Recognizer::parse_parameter_declaration()
{
	const Mark start = mark();
	parse_attribute_specifier_seq();
	if (!parse_decl_specifier_seq())
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
		const Mark after_declarator = mark();
		if (accept(OP_ASS) && !parse_initializer_clause())
		{
			reset(after_declarator);
		}
		if (at_parameter_end())
		{
			return true;
		}
	}
	return fail(start);
}

// OP_ARROW trailing-type-specifier-seq abstract-declarator?
bool Recognizer::parse_trailing_return_type()
{
	const Mark start = mark();
	if (!accept(OP_ARROW) || !parse_trailing_type_specifier_seq())
	{
		return fail(start);
	}
	parse_abstract_declarator();
	return true;
}

bool Recognizer::parse_type_id()
{
	return memoize(kMemoTypeId, &Recognizer::parse_type_id_body);
}

bool Recognizer::parse_type_id_body()
{
	if (!parse_type_specifier_seq())
	{
		return false;
	}
	parse_abstract_declarator();
	return true;
}

bool Recognizer::parse_abstract_declarator()
{
	return memoize(kMemoAbstractDeclarator, &Recognizer::parse_abstract_declarator_body);
}

// ptr-abstract-declarator | noptr-abstract-declarator? trailing-return-type
// | abstract-pack-declarator
bool Recognizer::parse_abstract_declarator_body()
{
	const Mark start = mark();
	parse_noptr_abstract_declarator();
	if (parse_trailing_return_type())
	{
		return true;
	}
	reset(start);
	if (parse_abstract_pack_declarator())
	{
		return true;
	}
	reset(start);
	return parse_ptr_abstract_declarator();
}

// ptr-operator* noptr-abstract-declarator | ptr-operator+
bool Recognizer::parse_ptr_abstract_declarator()
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return false;
	}

	const std::size_t operators = parse_ptr_operators();
	if (parse_noptr_abstract_declarator())
	{
		return true;
	}
	return operators != 0;
}

bool Recognizer::parse_noptr_abstract_declarator()
{
	if (!parse_noptr_abstract_declarator_root())
	{
		return false;
	}
	while (parse_noptr_declarator_suffix())
	{
	}
	return true;
}

// noptr-declarator-suffix | OP_LPAREN ptr-abstract-declarator OP_RPAREN
bool Recognizer::parse_noptr_abstract_declarator_root()
{
	const Mark start = mark();
	if (at(OP_LPAREN))
	{
		open_bracket(OP_LPAREN);
		if (parse_ptr_abstract_declarator() && accept(OP_RPAREN))
		{
			angle_ = start.angle;
			return true;
		}
		reset(start);
	}
	return parse_noptr_declarator_suffix();
}

// ptr-operator* OP_DOTS noptr-declarator-suffix*
bool Recognizer::parse_abstract_pack_declarator()
{
	const Mark start = mark();
	parse_ptr_operators();
	if (!accept(OP_DOTS))
	{
		return fail(start);
	}
	while (parse_noptr_declarator_suffix())
	{
	}
	return true;
}

// KW_THROW OP_LPAREN type-id-list? OP_RPAREN
// KW_NOEXCEPT (OP_LPAREN constant-expression OP_RPAREN)?
bool Recognizer::parse_exception_specification()
{
	const Mark start = mark();
	if (accept(KW_THROW))
	{
		if (!open_bracket(OP_LPAREN))
		{
			return fail(start);
		}
		parse_type_id_list();
		if (!accept(OP_RPAREN))
		{
			return fail(start);
		}
		angle_ = start.angle;
		return true;
	}
	if (!accept(KW_NOEXCEPT))
	{
		return false;
	}
	const Mark after_noexcept = mark();
	if (open_bracket(OP_LPAREN) && parse_constant_expression() && accept(OP_RPAREN))
	{
		angle_ = start.angle;
		return true;
	}
	reset(after_noexcept);
	return true;
}

// type-id-dots (OP_COMMA type-id-dots)*
bool Recognizer::parse_type_id_list()
{
	if (!parse_type_id())
	{
		return false;
	}
	accept(OP_DOTS);
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_type_id())
		{
			reset(item);
			break;
		}
		accept(OP_DOTS);
	}
	return true;
}
