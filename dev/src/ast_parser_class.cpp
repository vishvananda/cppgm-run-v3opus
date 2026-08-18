#include "ast_parser.h"

// Classes, enumerations and templates.

AstNode* AstParser::parse_class_specifier()
{
	const Mark start = mark();
	if (!at(KW_CLASS) && !at(KW_STRUCT) && !at(KW_UNION))
	{
		return nullptr;
	}
	AstNode* key = make_terminal(AstKind::ClassKey);
	// 7.6.2p1: an alignment-specifier may be written on either side of the name
	// in a class-head, and 7.6.2p5 makes what it asks for a fact about the class
	// rather than one the syntax may drop.
	std::vector<AstNode*> alignments;
	skip_attributes(&alignments);
	std::string name;
	const Mark named = mark();
	if (skip_qualified_type_name())
	{
		name = spelled(named);
	}
	else
	{
		reset(named);
	}
	skip_attributes(&alignments);
	while (at(TT_IDENTIFIER) && (spelling() == "final" || spelling() == "override"))
	{
		++pos_;
	}
	AstNode* bases = at(OP_COLON) ? parse_base_clause() : nullptr;
	if (at(OP_COLON) && bases == nullptr)
	{
		return fail(start);
	}
	if (!at(OP_LBRACE))
	{
		if (bases != nullptr || name.empty())
		{
			return fail(start);
		}
		AstNode* node = make_text(AstKind::ClassForwardDeclaration, name);
		node->add(key);
		names_.declare_member(name, take_class_head_kind(name));
		return node;
	}
	AstNode* node = make_text(AstKind::ClassSpecifier, name);
	node->add(key);
	for (std::size_t index = 0; index < alignments.size(); ++index)
	{
		node->add(alignments[index]);
	}
	node->add(bases);
	names_.declare_member(name, take_class_head_kind(name));
	names_.declare_class(name);
	// 10.2p2: the declarations of a base class are found in the scope of this
	// one, which the base-clause is where the parse learns.
	for (std::size_t index = 0;
	     bases != nullptr && index < bases->children.size(); ++index)
	{
		const AstNode* const base = bases->children[index];
		for (std::size_t part = 0; part < base->children.size(); ++part)
		{
			if (base->children[part]->kind == AstKind::BaseName)
			{
				names_.derive(name, base->children[part]->text);
			}
		}
	}
	const std::string own = names_.qualify(name);
	{
		// 3.3.7p1: the potential scope of a member name is the class it is
		// declared in, so what the body declares leaves with it.  The names it
		// declared stay reachable through the prefix the class gives them,
		// which 3.4.1p8's `Reached` is what puts back in force for a
		// declarator-id that names this class.
		BracketGuard brackets(*this, false);
		ScopeGuard members(names_);
		names_.inherit(own);
		PrefixGuard prefix(names_, name);
		++pos_;
		while (!at(OP_RBRACE) && !at(ST_EOF))
		{
			AstNode* member = parse_class_member();
			if (member == nullptr)
			{
				return fail(start);
			}
			node->add(member);
		}
	}
	if (!accept(OP_RBRACE))
	{
		return fail(start);
	}
	// The definition's own terminals.  9.2p13's layout is settled from what the
	// members are, but 16.6's packing alignment is a fact of where the
	// definition was written, and 9.2p2 completes the class at the `}` this
	// just read - which is the position that fact is asked at, and is the
	// class's own however it was declared.
	node->begin = static_cast<std::uint32_t>(start.pos);
	node->end = static_cast<std::uint32_t>(pos_);
	node->completed = static_cast<std::uint32_t>(pos_ - 1);
	return node;
}

AstNode* AstParser::parse_base_clause()
{
	const Mark start = mark();
	if (!accept(OP_COLON))
	{
		return nullptr;
	}
	AstNode* node = make(AstKind::BaseClause);
	do
	{
		AstNode* base = parse_base_specifier();
		if (base == nullptr)
		{
			return fail(start);
		}
		node->add(base);
	}
	while (accept(OP_COMMA));
	return node;
}

AstNode* AstParser::parse_base_specifier()
{
	const Mark start = mark();
	AstNode* node = make(AstKind::BaseSpecifier);
	skip_attributes();
	for (;;)
	{
		if (at(KW_VIRTUAL))
		{
			node->add(make_terminal(AstKind::Virtual));
			continue;
		}
		if (at(KW_PUBLIC) || at(KW_PRIVATE) || at(KW_PROTECTED))
		{
			node->add(make_terminal(AstKind::AccessSpecifier));
			continue;
		}
		break;
	}
	const Mark name = mark();
	// 10p1's `class-or-decltype` writes a decltype-specifier both ways: as the
	// whole base, and as the head of the nested-name-specifier a class name
	// follows.  The qualified reading is tried first because it is the longer
	// one - `decltype(e)::type` is a base whose spelling *begins* with a
	// decltype-specifier, and stopping at that specifier would leave the `::`
	// for the member list to choke on.
	if (!skip_qualified_type_name() && !skip_decltype_specifier())
	{
		return fail(start);
	}
	// 7.1.6.2p1 at that base: no spelling answers what the type of an
	// expression is - which declaration a call in it reaches is 13.3's question
	// and not the text's - so the operand is read here and kept beside the
	// spelling it flattens to, exactly as a decltype-specifier written among a
	// declaration's type-specifiers or at the head of a name is.  A dependent
	// one is the whole point: `struct conjunction : decltype(and_fn<Bn...>(0))`
	// names a class only the arguments settle, and the reading of the pattern
	// has nothing but this tree to come back to.
	if (tokens_.type(name.pos) == KW_DECLTYPE)
	{
		const Mark after = mark();
		reset(name);
		pos_ += 2;
		{
			BracketGuard brackets(*this, false);
			AstNode* const operand = parse_expression();
			if (operand == nullptr || !at(OP_RPAREN))
			{
				return fail(start);
			}
			keep_decltype(name, operand);
		}
		reset(after);
	}
	node->add(make_text(AstKind::BaseName, spelled(name)));
	if (accept(OP_DOTS))
	{
		// 14.5.3p4: the base-specifier is a pattern rather than one base, and
		// the class derives from one base per element of the run its packs are
		// bound to - so which of the two was written is a fact of the syntax.
		node->add(make_text(AstKind::ParameterPack, "..."));
	}
	return node;
}

AstNode* AstParser::parse_class_member()
{
	const Mark start = mark();
	skip_attributes();
	if ((at(KW_PUBLIC) || at(KW_PRIVATE) || at(KW_PROTECTED)) && peek(1) == OP_COLON)
	{
		AstNode* node = make_terminal(AstKind::AccessSpecifier);
		++pos_;
		return node;
	}
	reset(start);
	return parse_declaration(true);
}

// The bit-field readings of a member's declarators, against the
// decl-specifier-seq its declaration already read.
AstNode* AstParser::parse_bit_field_declaration(AstNode* specifiers)
{
	const Mark start = mark();
	AstNode* node = make(AstKind::BitFieldDeclaration);
	node->add(specifiers);
	do
	{
		AstNode* field = make(AstKind::BitFieldDeclarator);
		const Mark named = mark();
		AstNode* declarator = parse_declarator(DeclaratorForm::Named);
		if (declarator != nullptr && at(OP_COLON))
		{
			field->add(declarator);
		}
		else
		{
			reset(named);
		}
		if (!accept(OP_COLON))
		{
			return fail(start);
		}
		AstNode* width = parse_assignment_expression();
		if (width == nullptr)
		{
			return fail(start);
		}
		field->add(width);
		node->add(field);
	}
	while (accept(OP_COMMA));
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return node;
}

AstNode* AstParser::parse_enum_specifier()
{
	const Mark start = mark();
	if (!accept(KW_ENUM))
	{
		return nullptr;
	}
	AstNode* key = (at(KW_CLASS) || at(KW_STRUCT))
		? make_terminal(AstKind::EnumKey)
		: nullptr;
	skip_attributes();
	std::string name;
	const Mark named = mark();
	// 7.2p1 and 9.1p2: a member enumeration may be defined outside its class,
	// where its name is written with the nested-name-specifier that reaches it.
	if (skip_qualified_type_name())
	{
		name = spelled(named);
	}
	else
	{
		reset(named);
	}
	AstNode* base = nullptr;
	if (accept(OP_COLON))
	{
		base = parse_type_id();
		if (base == nullptr)
		{
			return fail(start);
		}
	}
	if (!at(OP_LBRACE))
	{
		// 7.2p2: an opaque-enum-declaration, which names an enumeration whose
		// underlying type is fixed - by an enum-base, or by the enum-key that
		// makes it a scoped enumeration.
		if (name.empty())
		{
			return fail(start);
		}
		AstNode* node = make_text(AstKind::EnumSpecifier, name);
		node->add(key);
		node->add(base);
		names_.declare_member(name, take_declared_kind(NameKind::Type));
		return node;
	}
	AstNode* node = make_text(AstKind::EnumSpecifier, name);
	node->add(key);
	node->add(base);
	names_.declare_member(name, NameKind::Type);
	BracketGuard brackets(*this, false);
	++pos_;
	while (at(TT_IDENTIFIER))
	{
		AstNode* enumerator = make_text(AstKind::Enumerator, spelling());
		names_.declare_member(spelling(), NameKind::Value);
		++pos_;
		if (accept(OP_ASS))
		{
			AstNode* value = parse_assignment_expression();
			if (value == nullptr)
			{
				return fail(start);
			}
			enumerator->add(value);
		}
		node->add(enumerator);
		if (!accept(OP_COMMA))
		{
			break;
		}
	}
	if (!accept(OP_RBRACE))
	{
		return fail(start);
	}
	return node;
}

AstNode* AstParser::parse_template_declaration(bool in_class)
{
	const Mark start = mark();
	accept(KW_TEMPLATE);
	AstNode* clause = nullptr;
	AstNode* declaration = nullptr;
	bool specialization = false;
	{
		ScopeGuard scope(names_);
		clause = parse_template_parameter_clause();
		if (clause != nullptr)
		{
			// 14.7.3p1: a head that declares no parameters is an explicit
			// specialization, which declares the *specialization* and no
			// template - so what follows names a class or a function, and a
			// `<` written after that name later is its own argument list and
			// not the opening of one.
			specialization = clause->children.empty();
			template_pending_ = !specialization;
			// 14.7.3p1: the declaration such a head stands on names the
			// specialization rather than declaring anything of its own, which
			// is what lets it be a constructor's and end at a `;`.
			const bool outer = names_specialization_;
			names_specialization_ = specialization;
			declaration = parse_declaration(in_class);
			names_specialization_ = outer;
			template_pending_ = false;
		}
	}
	if (clause == nullptr || declaration == nullptr)
	{
		return fail(start);
	}
	// The template parameters go out of scope with the clause, but what the
	// declaration declared does not, and what it declared is a template.
	if (!specialization)
	{
		declare_template_name(declaration);
	}
	AstNode* node = make(AstKind::TemplateDeclaration);
	node->add(clause);
	node->add(declaration);
	return node;
}

AstNode* AstParser::parse_template_parameter_clause()
{
	const Mark start = mark();
	if (!at(OP_LT))
	{
		return nullptr;
	}
	AstNode* node = make(AstKind::TemplateParameterClause);
	{
		BracketGuard brackets(*this, true);
		++pos_;
		if (!at_close_angle())
		{
			AstNode* list = make(AstKind::TemplateParameterList);
			do
			{
				AstNode* parameter = parse_template_parameter();
				if (parameter == nullptr)
				{
					return fail(start);
				}
				list->add(parameter);
			}
			while (accept(OP_COMMA));
			node->add(list);
		}
		if (!at_close_angle())
		{
			return fail(start);
		}
		++pos_;
	}
	return node;
}

AstNode* AstParser::parse_template_parameter()
{
	const Mark start = mark();
	AstNode* node = parse_type_parameter();
	if (node != nullptr && (at(OP_COMMA) || at_close_angle()))
	{
		return node;
	}
	reset(start);
	node = parse_non_type_template_parameter();
	if (node != nullptr && (at(OP_COMMA) || at_close_angle()))
	{
		return node;
	}
	return fail(start);
}

AstNode* AstParser::parse_type_parameter()
{
	const Mark start = mark();
	AstNode* node = make(AstKind::TypeParameter);
	bool place = false;
	if (accept(KW_TEMPLATE))
	{
		place = true;
		node->add(make(AstKind::TemplateTemplateParameter));
		AstNode* clause = parse_template_parameter_clause();
		if (clause == nullptr || !at(KW_CLASS))
		{
			return fail(start);
		}
		node->add(clause);
	}
	else if (!at(KW_CLASS) && !at(KW_TYPENAME))
	{
		return fail(start);
	}
	node->add(make_terminal(AstKind::ParameterKey));
	if (accept(OP_DOTS))
	{
		node->add(make_text(AstKind::ParameterPack, "..."));
	}
	if (at(TT_IDENTIFIER))
	{
		names_.declare(spelling(), NameKind::Type);
		node->add(make_text(AstKind::Identifier, spelling()));
		++pos_;
	}
	if (accept(OP_ASS))
	{
		// 14.1p2: the default argument at a template place is an id-expression
		// naming a template, which 14.2 leaves no type-specifier of its own -
		// so the one reading is told that a template-name stands there, and
		// what it builds is the shape every other default argument is written
		// in.
		const bool enclosing = template_place_default_;
		template_place_default_ = place;
		AstNode* type = parse_type_id();
		template_place_default_ = enclosing;
		if (type == nullptr)
		{
			return fail(start);
		}
		AstNode* argument = make(AstKind::DefaultTemplateArgument);
		argument->add(type);
		node->add(argument);
	}
	return node;
}

namespace
{

// True when every specifier of `specifiers` is a keyword, so that the type it
// names is settled without looking anything up.
bool is_builtin_parameter(const AstNode* specifiers)
{
	for (std::size_t index = 0; index < specifiers->children.size(); ++index)
	{
		if (specifiers->children[index]->token == kNoAstToken)
		{
			return false;
		}
	}
	return true;
}

}

AstNode* AstParser::parse_non_type_template_parameter()
{
	const Mark start = mark();
	AstNode* specifiers = parse_specifier_seq(SpecifierMode::Decl);
	if (specifiers == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::NonTypeTemplateParameter);
	node->add(specifiers);
	if (accept(OP_DOTS))
	{
		node->add(make_text(AstKind::ParameterPack, "..."));
	}
	const Mark named = mark();
	AstNode* declarator = parse_declarator(DeclaratorForm::Named);
	if (declarator == nullptr)
	{
		// 14.1p3: a non-type template-parameter is written as 8.3.5's
		// parameter-declaration, whose declarator may be abstract - so
		// `template<M *>` declares a place of pointer type that no name
		// reaches, exactly as an unnamed function parameter does.  The named
		// form is tried first because a declarator-id is what makes `T v` two
		// words rather than one type-id.
		reset(named);
		declarator = parse_declarator(DeclaratorForm::Abstract);
		if (declarator != nullptr && declarator->children.empty())
		{
			// An abstract declarator that wrote nothing is no declarator at
			// all, and the place is of the type its specifiers named.
			declarator = nullptr;
			reset(named);
		}
	}
	if (declarator != nullptr)
	{
		node->add(declarator);
		const AstNode* identifier = declarator_identifier(declarator);
		if (identifier != nullptr)
		{
			names_.declare(identifier->text, NameKind::Value);
		}
	}
	else
	{
		reset(named);
	}
	if (!accept(OP_ASS))
	{
		return node;
	}
	AstNode* argument = make(AstKind::DefaultTemplateArgument);
	// A parameter with no declarator and a built-in type has neither a name to
	// bind its default to nor a type that a later assignment has to look at,
	// so the default is kept as the terminal it was written as rather than as
	// an expression tree.
	if (declarator == nullptr && at(TT_LITERAL) && is_builtin_parameter(specifiers) &&
	    (peek(1) == OP_COMMA || tokens_.type(pos_ + 1) == OP_GT ||
	     tokens_.type(pos_ + 1) == ST_RSHIFT_1))
	{
		argument->add(make_terminal(AstKind::Literal));
	}
	else
	{
		AstNode* value = parse_assignment_expression();
		if (value == nullptr)
		{
			return fail(start);
		}
		argument->add(value);
	}
	node->add(argument);
	return node;
}
