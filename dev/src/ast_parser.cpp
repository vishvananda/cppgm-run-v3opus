#include "ast_parser.h"

// Declarations: the rules a translation unit is a sequence of.

AstParser::AstParser(const AstTokenStream& tokens, AstArena& arena)
	: tokens_(tokens)
	, arena_(arena)
	, pos_(0)
	, angle_(false)
	, bracket_depth_(0)
	, template_id_veto_depth_(-1)
	, template_pending_(false)
	, template_id_memo_version_(names_.version())
{
}

AstNode* AstParser::make_terminal(AstKind kind)
{
	AstNode* node = make(kind);
	node->token = static_cast<std::uint16_t>(peek());
	node->text = spelling();
	++pos_;
	return node;
}

AstNode* AstParser::make_text(AstKind kind, const std::string& text)
{
	AstNode* node = make(kind);
	node->text = text;
	return node;
}

AstNode* AstParser::carried(AstNode* expression)
{
	if (expression == nullptr)
	{
		return nullptr;
	}
	AstNode* node = make(AstKind::CarriedExpression);
	node->add(expression);
	return node;
}

AstNode* AstParser::run()
{
	AstNode* root = make(AstKind::TranslationUnit);
	// A rule that stopped at the depth limit reports no more than that it did
	// not match, and a caller that only wanted an optional part would take
	// that for an answer, so the whole descent is asked once at the end.
	if (!parse_declaration_seq(root, ST_EOF) || !at(ST_EOF) || depth_.overflowed())
	{
		return nullptr;
	}
	return root;
}

AstNode* AstParser::parse_declaration_seq(AstNode* parent, unsigned closer)
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return nullptr;
	}
	while (!at(closer) && !at(ST_EOF))
	{
		AstNode* declaration = parse_declaration(false);
		if (declaration == nullptr)
		{
			return nullptr;
		}
		parent->add(declaration);
	}
	return parent;
}

AstNode* AstParser::parse_declaration(bool in_class)
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return nullptr;
	}
	const Mark start = mark();
	skip_attributes(nullptr, true);
	if (accept(OP_SEMICOLON))
	{
		return make(AstKind::EmptyDeclaration);
	}
	AstNode* node = nullptr;
	if (at(KW_NAMESPACE) || (at(KW_INLINE) && peek(1) == KW_NAMESPACE))
	{
		node = parse_namespace();
	}
	else if (at(KW_USING))
	{
		node = parse_using();
	}
	else if (at(KW_EXTERN) && peek(1) == TT_LITERAL)
	{
		node = parse_linkage_specification();
	}
	else if (at(KW_EXTERN) && peek(1) == KW_TEMPLATE)
	{
		node = parse_explicit_instantiation();
	}
	else if (at(KW_TEMPLATE) && peek(1) == OP_LT)
	{
		node = parse_template_declaration(in_class);
	}
	else if (at(KW_TEMPLATE))
	{
		// 14.7.2p1: `template` with no template-parameter-clause after it is
		// an explicit instantiation of the declaration it stands on, which is
		// the same target `extern template` writes.
		node = parse_explicit_instantiation();
	}
	else if (at(KW_STATIC_ASSERT))
	{
		node = parse_static_assert();
	}
	else
	{
		node = parse_general_declaration(in_class);
	}
	if (node == nullptr)
	{
		return fail(start);
	}
	// The declaration owns the terminals it was written from.  A declaration
	// whose specifiers are its whole content hands its specifier node back as
	// itself, so the span reaches past the `;` that the specifier rule did not
	// read - which is the whole declaration, and what names an unnamed one.
	node->begin = static_cast<std::uint32_t>(start.pos);
	node->end = static_cast<std::uint32_t>(pos_);
	return node;
}

AstNode* AstParser::parse_namespace()
{
	const Mark start = mark();
	AstNode* inline_marker = at(KW_INLINE) ? make(AstKind::Inline) : nullptr;
	if (inline_marker != nullptr)
	{
		++pos_;
	}
	if (!accept(KW_NAMESPACE))
	{
		return fail(start);
	}
	std::string name;
	if (at(TT_IDENTIFIER))
	{
		name = spelling();
		++pos_;
	}
	if (accept(OP_ASS))
	{
		const Mark target = mark();
		if (!skip_nested_name_specifier() || !skip_unqualified_id())
		{
			reset(target);
			if (!skip_unqualified_id())
			{
				return fail(start);
			}
		}
		const std::string named = spelled(target);
		AstNode* node = make_text(AstKind::NamespaceAliasDefinition, name);
		node->add(make_text(AstKind::Target, named));
		if (!accept(OP_SEMICOLON))
		{
			return fail(start);
		}
		names_.declare_member(name, NameKind::Type);
		names_.alias(name, named);
		return node;
	}
	if (!at(OP_LBRACE))
	{
		return fail(start);
	}
	AstNode* node = make_text(AstKind::NamespaceDefinition,
	                          name.empty() ? std::string("<unnamed>") : name);
	node->add(inline_marker);
	names_.declare_member(name, NameKind::Type);
	{
		BracketGuard brackets(*this, false);
		ScopeGuard scope(names_);
		PrefixGuard prefix(names_, name);
		++pos_;
		if (parse_declaration_seq(node, OP_RBRACE) == nullptr)
		{
			return fail(start);
		}
	}
	if (!accept(OP_RBRACE))
	{
		return fail(start);
	}
	return node;
}

AstNode* AstParser::parse_using()
{
	const Mark start = mark();
	accept(KW_USING);
	if (accept(KW_NAMESPACE))
	{
		const Mark target = mark();
		skip_nested_name_specifier();
		if (!skip_unqualified_id())
		{
			return fail(start);
		}
		const std::string named = spelled(target);
		AstNode* node = make(AstKind::UsingDirective);
		node->add(make_text(AstKind::Target, named));
		if (!accept(OP_SEMICOLON))
		{
			return fail(start);
		}
		names_.nominate(named);
		return node;
	}
	if (at(TT_IDENTIFIER) && peek(1) == OP_ASS)
	{
		const std::string name = spelling();
		pos_ += 2;
		AstNode* type = parse_type_id();
		if (type == nullptr || !accept(OP_SEMICOLON))
		{
			return fail(start);
		}
		AstNode* node = make_text(AstKind::AliasDeclaration, name);
		node->add(type);
		names_.declare_member(name, NameKind::Type);
		return node;
	}
	const Mark target = mark();
	accept(KW_TYPENAME);
	skip_nested_name_specifier();
	const Mark introduced = mark();
	if (!skip_unqualified_id())
	{
		return fail(start);
	}
	const std::string named = spelled(target);
	const std::string declared = tokens_.flatten(introduced.pos, pos_);
	AstNode* node = make(AstKind::UsingDeclaration);
	node->add(make_text(AstKind::Target, named));
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	// 7.3.3p1: the using-declaration binds its name to the declaration the
	// target names, so a use of the name reads as that declaration does.  A
	// target the parse knows nothing about leaves the name as it was rather
	// than hiding what an enclosing scope declared with a guess.
	const NameKind kind = names_.kind_of(named);
	if (kind != NameKind::Unknown)
	{
		names_.declare_member(declared, kind);
	}
	return node;
}

AstNode* AstParser::parse_linkage_specification()
{
	const Mark start = mark();
	accept(KW_EXTERN);
	std::string language;
	if (!tokens_.string_value(pos_, language))
	{
		return fail(start);
	}
	++pos_;
	AstNode* node = make_text(AstKind::LinkageSpecification, language);
	if (at(OP_LBRACE))
	{
		{
			BracketGuard brackets(*this, false);
			++pos_;
			if (parse_declaration_seq(node, OP_RBRACE) == nullptr)
			{
				return fail(start);
			}
		}
		if (!accept(OP_RBRACE))
		{
			return fail(start);
		}
		return node;
	}
	AstNode* declaration = parse_declaration(false);
	if (declaration == nullptr)
	{
		return fail(start);
	}
	node->add(declaration);
	return node;
}

namespace
{

// The grammar's `explicit-instantiation-target`: a class-declaration or a
// simple-declaration, which is what 14.7.2p1 leaves room for a template-id in.
// A declaration of any other shape declares no specialization, so `template;`
// and `template using X = int;` are no explicit instantiation at all.
bool is_explicit_instantiation_target(const AstNode& target)
{
	return target.kind == AstKind::ClassSpecifier ||
		target.kind == AstKind::ClassForwardDeclaration ||
		target.kind == AstKind::SimpleDeclaration;
}

}

AstNode* AstParser::parse_explicit_instantiation()
{
	const Mark start = mark();
	// 14.7.2p1 and 14.7.2p9: the two forms differ by the one keyword written
	// in front, and what it says - whether this unit owes the definitions or
	// leaves them to another - is a fact about the whole declaration rather
	// than a terminal inside it, so each form is a node of its own.
	const bool extern_form = accept(KW_EXTERN);
	++pos_;
	AstNode* target = parse_declaration(false);
	if (target == nullptr || !is_explicit_instantiation_target(*target))
	{
		return fail(start);
	}
	AstNode* node = make(extern_form
		? AstKind::ExplicitInstantiationDeclaration
		: AstKind::ExplicitInstantiationDefinition);
	node->add(target);
	return node;
}

AstNode* AstParser::parse_static_assert()
{
	const Mark start = mark();
	++pos_;
	if (!at(OP_LPAREN))
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::StaticAssertDeclaration);
	{
		BracketGuard brackets(*this, false);
		++pos_;
		AstNode* condition = parse_assignment_expression();
		if (condition == nullptr)
		{
			return fail(start);
		}
		node->add(condition);
		if (accept(OP_COMMA))
		{
			if (!at(TT_LITERAL))
			{
				return fail(start);
			}
			node->add(make_text(AstKind::Message, spelling()));
			++pos_;
		}
		if (!at(OP_RPAREN))
		{
			return fail(start);
		}
		++pos_;
	}
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return node;
}

// The declarations that start with a decl-specifier-seq, plus the special
// member forms that have none.
AstNode* AstParser::parse_general_declaration(bool in_class)
{
	const Mark start = mark();
	AstNode* special = parse_special_member(in_class);
	if (special != nullptr)
	{
		return special;
	}
	reset(start);
	AstNode* specifiers = parse_specifier_seq(SpecifierMode::Decl);
	if (specifiers == nullptr)
	{
		return fail(start);
	}
	AstNode* node = parse_simple_or_function_declaration(specifiers, in_class);
	if (node == nullptr)
	{
		return fail(start);
	}
	return node;
}

namespace
{

// True when a decl-specifier-seq is nothing but a class or enum specifier, in
// which case a `;` after it is the class-declaration or enum-declaration of
// the grammar rather than a simple-declaration with no declarators.
bool is_lone_type_definition(const AstNode* specifiers)
{
	if (specifiers->children.size() != 1)
	{
		return false;
	}
	const AstKind kind = specifiers->children[0]->kind;
	return kind == AstKind::ClassSpecifier ||
		kind == AstKind::ClassForwardDeclaration ||
		kind == AstKind::EnumSpecifier;
}

}

AstNode* AstParser::parse_simple_or_function_declaration(AstNode* specifiers,
                                                         bool in_class)
{
	const Mark start = mark();
	const Mark after_specifiers = mark();
	AstNode* declarator = parse_declarator(DeclaratorForm::Named);
	if (declarator != nullptr && at(OP_LBRACE) && declares_function(declarator))
	{
		AstNode* node = make(AstKind::FunctionDefinition);
		node->add(specifiers);
		node->add(declarator);
		declare_init_declarators(specifiers, declarator);
		// 3.4.1p8: a member function defined outside its class reads the names
		// in its body as the class declares them.
		ReachedGuard reached(names_, declarator_qualifier(declarator));
		ScopeGuard scope(names_);
		declare_parameters(declarator);
		AstNode* body = parse_compound_statement();
		if (body != nullptr)
		{
			node->add(body);
			return node;
		}
	}
	reset(after_specifiers);
	AstNode* node = make(AstKind::SimpleDeclaration);
	node->add(specifiers);
	if (accept(OP_SEMICOLON))
	{
		if (is_lone_type_definition(specifiers))
		{
			return specifiers->children[0];
		}
		return node;
	}
	AstNode* list = parse_init_declarator_list();
	if (list != nullptr && accept(OP_SEMICOLON))
	{
		node->add(list);
		declare_init_declarators(specifiers, list);
		return node;
	}
	// A bit-field is the same declaration with `: width` where an initializer
	// would go, so it reads the declarators again but never the specifiers:
	// they may hold a class definition, and reading that twice per class would
	// cost `2^N` for `N` nested ones.
	reset(after_specifiers);
	if (in_class)
	{
		AstNode* fields = parse_bit_field_declaration(specifiers);
		if (fields != nullptr)
		{
			return fields;
		}
	}
	return fail(start);
}

AstNode* AstParser::parse_member_specifiers()
{
	AstNode* node = nullptr;
	for (;;)
	{
		skip_attributes();
		const unsigned type = peek();
		if (type != KW_INLINE && type != KW_VIRTUAL && type != KW_EXPLICIT &&
		    type != KW_CONSTEXPR && type != KW_FRIEND && type != KW_STATIC)
		{
			return node;
		}
		if (node == nullptr)
		{
			node = make(AstKind::MemberSpecifiers);
		}
		if (type == KW_EXPLICIT)
		{
			node->add(make_text(AstKind::Specifier, spelling()));
			++pos_;
		}
		else
		{
			node->add(make_terminal(AstKind::Specifier));
		}
	}
}

AstNode* AstParser::parse_special_member(bool in_class)
{
	const Mark start = mark();
	skip_attributes();
	AstNode* specifiers = parse_member_specifiers();
	AstNode* conversion_type = nullptr;
	std::string written_qualifier;
	const std::string name =
		parse_special_member_name(&conversion_type, &written_qualifier);
	if (name.empty() || !at(OP_LPAREN))
	{
		return fail(start);
	}
	AstNode* conversion = carried_conversion(conversion_type,
	                                         written_qualifier);
	// 3.4.1p8: a special member defined outside its class is read where its
	// declarator-id names, which is everything after that name - the parameter
	// clause, the mem-initializers and the body alike.  A member defined in its
	// class writes no nested-name-specifier, so the guard opens nothing there.
	// 12.3.2p1's conversion-type-id may hold a `::` of its own, so the prefix
	// the parse read is what says which class, not the last `::` of the name.
	ReachedGuard reached(names_, conversion != nullptr
		? written_qualifier
		: name_qualifier(name));
	AstNode* declarator = make(AstKind::Declarator);
	declarator->add(make_text(AstKind::Identifier, name));
	AstNode* clause = parse_parameter_clause();
	if (clause == nullptr)
	{
		return fail(start);
	}
	declarator->add(clause);
	parse_function_suffixes(declarator);
	// 8.4.2p2: `= default` and `= delete` may stand on a declaration written
	// outside the class as readily as on the one the class body wrote, and a
	// definition is what such a declaration is.  Outside the class it is the
	// only `;` form there is - 3.4.3p3's qualified declarator-id declares
	// nothing new - so the tail is tried there only where one of the two was
	// written and a bare `;` still reaches no rule.
	if (in_class || (at(OP_ASS) && (peek(1) == KW_DEFAULT || peek(1) == KW_DELETE)))
	{
		AstNode* declaration =
			parse_special_member_tail(specifiers, declarator, conversion);
		if (declaration != nullptr)
		{
			declaration->text = name;
			return declaration;
		}
	}
	AstNode* initializer = parse_ctor_initializer();
	if (!at(OP_LBRACE))
	{
		return fail(start);
	}
	AstNode* node = make_text(AstKind::SpecialMemberDefinition, name);
	node->add(specifiers);
	node->add(declarator);
	node->add(initializer);
	// 12.3.2p1's carried type-id stands before the body, because the body is
	// the last child a definition has and the reader that runs it takes it
	// from there.
	node->add(conversion);
	ScopeGuard scope(names_);
	declare_parameters(declarator);
	AstNode* body = parse_compound_statement();
	if (body == nullptr)
	{
		return fail(start);
	}
	node->add(body);
	return node;
}

// The `;` form of a special member, with the `= default` or `= delete` that
// only a declaration may carry - and 9.2's pure-specifier, which a destructor's
// member-declarator writes exactly as any other member function's does.
AstNode* AstParser::parse_special_member_tail(AstNode* specifiers,
                                              AstNode* declarator,
                                              AstNode* conversion)
{
	const Mark start = mark();
	AstNode* initializer = nullptr;
	if (at(OP_ASS) && (peek(1) == KW_DEFAULT || peek(1) == KW_DELETE))
	{
		initializer = make(AstKind::Initializer);
		initializer->add(make_text(AstKind::SpecialInitializer, spelling(1)));
		pos_ += 2;
	}
	else if (at(OP_ASS) && peek(1) == TT_LITERAL && spelling(1) == "0")
	{
		// 9.2's member-declarator is `declarator virt-specifier-seq_opt
		// pure-specifier_opt`, and 12.4p9 lets a destructor be pure virtual like
		// any other member function - so `= 0` stands here beside the two forms
		// 8.4.2 writes, spelled as the literal the reader of a pure-specifier
		// already looks for.
		initializer = make(AstKind::Initializer);
		initializer->add(make_text(AstKind::Literal, spelling(1)));
		pos_ += 2;
	}
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::SpecialMemberDeclaration);
	node->add(specifiers);
	node->add(declarator);
	node->add(initializer);
	node->add(conversion);
	return node;
}

AstNode* AstParser::parse_ctor_initializer()
{
	const Mark start = mark();
	if (!accept(OP_COLON))
	{
		return nullptr;
	}
	AstNode* node = make(AstKind::CtorInitializer);
	do
	{
		AstNode* initializer = parse_mem_initializer();
		if (initializer == nullptr)
		{
			return fail(start);
		}
		node->add(initializer);
	}
	while (accept(OP_COMMA));
	return node;
}

AstNode* AstParser::parse_mem_initializer()
{
	const Mark start = mark();
	const Mark name = mark();
	if (!skip_decltype_specifier() && !skip_qualified_type_name())
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::MemInitializer);
	node->add(make_text(AstKind::MemInitializerId, spelled(name)));
	AstNode* arguments = nullptr;
	if (at(OP_LPAREN))
	{
		arguments = parse_argument_suffix(AstKind::ParenArgumentList);
	}
	else if (at(OP_LBRACE))
	{
		arguments = parse_braced_init_list();
	}
	if (arguments == nullptr)
	{
		return fail(start);
	}
	node->add(arguments);
	if (accept(OP_DOTS))
	{
		// 14.5.3p4 and 12.6.2p1: the mem-initializer is a pattern standing for
		// one mem-initializer per element of the run its packs are bound to,
		// which is the same `...` a base-specifier writes and is recorded the
		// same way.
		node->add(make(AstKind::ParameterPack));
	}
	return node;
}
