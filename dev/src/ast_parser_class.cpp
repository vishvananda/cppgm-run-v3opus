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
	skip_attributes();
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
	skip_attributes();
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
		declare_name(name, take_declared_kind(NameKind::Type));
		return node;
	}
	AstNode* node = make_text(AstKind::ClassSpecifier, name);
	node->add(key);
	node->add(bases);
	declare_name(name, take_declared_kind(NameKind::Type));
	{
		// A class body declares into the scope around it as well as into
		// itself: PA10 has no lookup to reach a member from outside, so the
		// facts an unqualified member name carries have to stay reachable.
		BracketGuard brackets(*this, false);
		const std::string outer = prefix_;
		prefix_ += name.empty() ? std::string() : name + "::";
		++pos_;
		while (!at(OP_RBRACE) && !at(ST_EOF))
		{
			AstNode* member = parse_class_member();
			if (member == nullptr)
			{
				prefix_ = outer;
				return fail(start);
			}
			node->add(member);
		}
		prefix_ = outer;
	}
	if (!accept(OP_RBRACE))
	{
		return fail(start);
	}
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
	if (!skip_decltype_specifier() && !skip_qualified_type_name())
	{
		return fail(start);
	}
	node->add(make_text(AstKind::BaseName, spelled(name)));
	accept(OP_DOTS);
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
	AstNode* declaration = parse_declaration(true);
	if (declaration != nullptr)
	{
		return declaration;
	}
	reset(start);
	AstNode* field = parse_bit_field_declaration();
	if (field != nullptr)
	{
		return field;
	}
	return fail(start);
}

AstNode* AstParser::parse_bit_field_declaration()
{
	const Mark start = mark();
	AstNode* specifiers = parse_specifier_seq(SpecifierMode::Decl);
	if (specifiers == nullptr)
	{
		return nullptr;
	}
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
	if (at(TT_IDENTIFIER))
	{
		name = spelling();
		++pos_;
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
		if (name.empty() || base != nullptr)
		{
			return fail(start);
		}
		AstNode* node = make_text(AstKind::EnumSpecifier, name);
		node->add(key);
		declare_name(name, take_declared_kind(NameKind::Type));
		return node;
	}
	AstNode* node = make_text(AstKind::EnumSpecifier, name);
	node->add(key);
	node->add(base);
	declare_name(name, NameKind::Type);
	BracketGuard brackets(*this, false);
	++pos_;
	while (at(TT_IDENTIFIER))
	{
		AstNode* enumerator = make_text(AstKind::Enumerator, spelling());
		declare_name(spelling(), NameKind::Value);
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
	{
		ScopeGuard scope(names_);
		clause = parse_template_parameter_clause();
		if (clause != nullptr)
		{
			template_pending_ = true;
			declaration = parse_declaration(in_class);
			template_pending_ = false;
		}
	}
	if (clause == nullptr || declaration == nullptr)
	{
		return fail(start);
	}
	// The template parameters go out of scope with the clause, but what the
	// declaration declared does not, and what it declared is a template.
	declare_template_name(declaration);
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
	if (accept(KW_TEMPLATE))
	{
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
		AstNode* type = parse_type_id();
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
