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

AstNode* AstParser::run()
{
	AstNode* root = make(AstKind::TranslationUnit);
	if (!parse_declaration_seq(root, ST_EOF) || !at(ST_EOF))
	{
		return nullptr;
	}
	return root;
}

AstNode* AstParser::parse_declaration_seq(AstNode* parent, unsigned closer)
{
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
	const Mark start = mark();
	skip_attributes();
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
		AstNode* node = make_text(AstKind::NamespaceAliasDefinition, name);
		node->add(make_text(AstKind::Target, spelled(target)));
		if (!accept(OP_SEMICOLON))
		{
			return fail(start);
		}
		declare_name(name, NameKind::Type);
		return node;
	}
	if (!at(OP_LBRACE))
	{
		return fail(start);
	}
	AstNode* node = make_text(AstKind::NamespaceDefinition,
	                          name.empty() ? std::string("<unnamed>") : name);
	node->add(inline_marker);
	declare_name(name, NameKind::Type);
	{
		BracketGuard brackets(*this, false);
		ScopeGuard scope(names_);
		const std::string outer = prefix_;
		prefix_ += name.empty() ? std::string() : name + "::";
		++pos_;
		const AstNode* body = parse_declaration_seq(node, OP_RBRACE);
		prefix_ = outer;
		if (body == nullptr)
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
		AstNode* node = make(AstKind::UsingDirective);
		node->add(make_text(AstKind::Target, spelled(target)));
		if (!accept(OP_SEMICOLON))
		{
			return fail(start);
		}
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
		declare_name(name, NameKind::Type);
		return node;
	}
	const Mark target = mark();
	accept(KW_TYPENAME);
	skip_nested_name_specifier();
	if (!skip_unqualified_id())
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::UsingDeclaration);
	node->add(make_text(AstKind::Target, spelled(target)));
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return node;
}

AstNode* AstParser::parse_linkage_specification()
{
	const Mark start = mark();
	accept(KW_EXTERN);
	std::string text = spelling();
	if (text.size() >= 2 && text[0] == '"')
	{
		text = text.substr(1, text.size() - 2);
	}
	++pos_;
	AstNode* node = make_text(AstKind::LinkageSpecification, text);
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

AstNode* AstParser::parse_explicit_instantiation()
{
	const Mark start = mark();
	pos_ += 2;
	AstNode* target = parse_declaration(false);
	if (target == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::ExplicitInstantiationDeclaration);
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
	if (list == nullptr || !accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	node->add(list);
	declare_init_declarators(specifiers, list);
	(void)in_class;
	return node;
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
	const std::string name = parse_special_member_name();
	if (name.empty() || !at(OP_LPAREN))
	{
		return fail(start);
	}
	AstNode* declarator = make(AstKind::Declarator);
	declarator->add(make_text(AstKind::Identifier, name));
	AstNode* clause = parse_parameter_clause();
	if (clause == nullptr)
	{
		return fail(start);
	}
	declarator->add(clause);
	parse_function_suffixes(declarator);
	if (in_class)
	{
		AstNode* declaration = parse_special_member_tail(specifiers, declarator, name);
		if (declaration != nullptr)
		{
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
// only a declaration may carry.
AstNode* AstParser::parse_special_member_tail(AstNode* specifiers,
                                              AstNode* declarator,
                                              const std::string& name)
{
	const Mark start = mark();
	AstNode* initializer = nullptr;
	if (at(OP_ASS) && (peek(1) == KW_DEFAULT || peek(1) == KW_DELETE))
	{
		initializer = make(AstKind::Initializer);
		initializer->add(make_text(AstKind::SpecialInitializer, spelling(1)));
		pos_ += 2;
	}
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	AstNode* node = make_text(AstKind::SpecialMemberDeclaration, name);
	node->add(specifiers);
	node->add(declarator);
	node->add(initializer);
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
	accept(OP_DOTS);
	return node;
}
