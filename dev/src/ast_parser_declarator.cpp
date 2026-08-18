#include "ast_parser.h"

#include "sema_name.h"

// Specifier sequences, type-ids, declarators and initializers.

namespace
{

// The keywords `simple-type-specifier` names.
bool is_simple_type_specifier(unsigned type)
{
	switch (type)
	{
	case KW_BOOL: case KW_CHAR: case KW_CHAR16_T: case KW_CHAR32_T:
	case KW_DOUBLE: case KW_FLOAT: case KW_INT: case KW_LONG:
	case KW_SHORT: case KW_SIGNED: case KW_UNSIGNED: case KW_VOID:
	case KW_WCHAR_T: case KW_AUTO:
		return true;
	default:
		return false;
	}
}

// The keywords that are a decl-specifier but never a type-specifier.
bool is_declaration_keyword(unsigned type)
{
	switch (type)
	{
	case KW_TYPEDEF: case KW_EXTERN: case KW_STATIC: case KW_INLINE:
	case KW_VIRTUAL: case KW_CONSTEXPR: case KW_THREAD_LOCAL: case KW_FRIEND:
	case KW_MUTABLE: case KW_REGISTER:
		return true;
	default:
		return false;
	}
}

}

AstNode* AstParser::parse_specifier_seq(SpecifierMode mode)
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return nullptr;
	}
	const Mark start = mark();
	AstNode* seq = make(mode == SpecifierMode::Decl
		? AstKind::DeclSpecifierSeq
		: AstKind::TypeSpecifierSeq);
	bool seen_type = false;
	while (parse_specifier(seq, mode, seen_type))
	{
	}
	if (seq->children.empty())
	{
		return fail(start);
	}
	return seq;
}

bool AstParser::parse_specifier(AstNode* seq, SpecifierMode mode, bool& seen_type)
{
	// 7.6.2p1 and 7p1: an alignment-specifier written before the decl-specifiers
	// appertains to what the declaration declares, and 7.6.2p5 makes what that
	// one asks for a fact about it rather than one the syntax may drop - the
	// PA10 dump writes no node for it, which is why the sequence may hold one.
	// One written after a decl-specifier appertains to the type those specifiers
	// named, which is not an entity an alignment-specifier says anything about,
	// so it asks for nothing.
	const bool leading = seq->children.empty();
	std::vector<AstNode*> alignments;
	skip_attributes(&alignments);
	for (std::size_t index = 0; leading && index < alignments.size(); ++index)
	{
		seq->add(alignments[index]);
	}
	const unsigned type = peek();
	if (type == KW_CONST || type == KW_VOLATILE)
	{
		seq->add(make_terminal(mode == SpecifierMode::Decl
			? AstKind::DeclSpecifier
			: AstKind::CvQualifier));
		return true;
	}
	if (is_simple_type_specifier(type))
	{
		seen_type = true;
		seq->add(make_terminal(mode == SpecifierMode::Decl
			? AstKind::DeclSpecifier
			: AstKind::TypeSpecifier));
		return true;
	}
	if (is_declaration_keyword(type))
	{
		if (mode != SpecifierMode::Decl)
		{
			return false;
		}
		seq->add(make_terminal(AstKind::DeclSpecifier));
		return true;
	}
	if (seen_type)
	{
		return false;
	}
	if (type == KW_CLASS || type == KW_STRUCT || type == KW_UNION ||
	    type == KW_ENUM)
	{
		AstNode* definition = (type == KW_ENUM)
			? parse_enum_specifier()
			: parse_class_specifier();
		if (definition == nullptr)
		{
			return false;
		}
		seen_type = true;
		seq->add(definition);
		return true;
	}
	return parse_name_specifier(seq, mode, seen_type);
}

// A `qualified-type-name`, a `typename`-introduced one, or a
// `decltype-specifier`: the specifiers whose identity is a name the grammar
// leaves for a semantic assignment to resolve.
bool AstParser::parse_name_specifier(AstNode* seq, SpecifierMode mode,
                                     bool& seen_type)
{
	const Mark start = mark();
	const bool introduced = accept(KW_TYPENAME);
	const Mark spelled_from = mark();
	if (at(KW_DECLTYPE) && skip_decltype_specifier() && !at(OP_COLON2))
	{
		const std::string text = spelled(spelled_from);
		reset(spelled_from);
		pos_ += 2;
		AstNode* expression = nullptr;
		{
			BracketGuard brackets(*this, false);
			expression = parse_expression();
			if (expression == nullptr || !at(OP_RPAREN))
			{
				reset(start);
				return false;
			}
			++pos_;
		}
		AstNode* node = make_text(mode == SpecifierMode::Decl
			? AstKind::DeclSpecifier
			: AstKind::DecltypeSpecifier, text);
		node->add(expression);
		// 14.2: a template-argument-list is read this same way and then
		// flattened into the name around it, so this reading is kept where the
		// parse owns it and a spelling of it can ask the tree back.
		arena_.keep_spelled(text, node);
		seen_type = true;
		seq->add(node);
		return true;
	}
	reset(spelled_from);
	std::string template_name;
	if (!skip_qualified_type_name(&template_name))
	{
		reset(start);
		return false;
	}
	const std::string text = spelled(spelled_from);
	// 7.1.6.2p1: a nested-name-specifier may begin with a decltype-specifier,
	// whose region is the class the type of an expression names.  No spelling
	// answers what that type is, so the expression is read here and kept beside
	// the name, which is the one thing the semantics cannot recover.
	AstNode* operand = nullptr;
	if (tokens_.type(spelled_from.pos) == KW_DECLTYPE)
	{
		const Mark after = mark();
		reset(spelled_from);
		pos_ += 2;
		{
			BracketGuard brackets(*this, false);
			operand = parse_expression();
			if (operand == nullptr || !at(OP_RPAREN))
			{
				reset(start);
				return false;
			}
			keep_decltype(spelled_from, operand);
		}
		reset(after);
	}
	const bool plain = !introduced && pos_ == spelled_from.pos + 1;
	const NameKind kind = names_.kind_of(text);
	// 14.2p3: a template-id of a function template names an overload set rather
	// than a type, however its argument list is written, so the name it was
	// written on is what says whether this reading is a type at all.
	const NameKind named = template_name.empty()
		? NameKind::Unknown
		: names_.kind_of(template_name);
	// 14.2 and 14.6.1p1: a template-name says which class only once an argument
	// list is written after it, so a plain one is a type-specifier exactly
	// where 14.6.1p1's injected-class-name stands - and `close_impl(which);`
	// written where no class of that name encloses it is 6.8p1's expression and
	// not a declaration of `which`.
	if (kind == NameKind::Value || named == NameKind::FunctionTemplate ||
	    (kind == NameKind::Template && !template_place_default_ &&
	     (!plain || !names_.injected(text))))
	{
		reset(start);
		return false;
	}
	seen_type = true;
	if (mode != SpecifierMode::Decl)
	{
		AstNode* named = make_text(AstKind::TypeName, text);
		// 14.6p2: the keyword is no part of the name, and whether it was
		// written is what says a dependent qualified one names a type at all.
		named->introduced = introduced;
		named->add(carried(operand));
		seq->add(named);
		return true;
	}
	AstNode* node = make_text(AstKind::DeclSpecifier, text);
	if (plain)
	{
		node->token = TT_IDENTIFIER;
	}
	node->introduced = introduced;
	node->add(carried(operand));
	seq->add(node);
	return true;
}

AstNode* AstParser::parse_type_id()
{
	const Mark start = mark();
	AstNode* seq = parse_specifier_seq(SpecifierMode::Type);
	if (seq == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::TypeId);
	node->add(seq);
	AstNode* declarator = parse_declarator(DeclaratorForm::Abstract);
	if (declarator != nullptr && !declarator->children.empty())
	{
		node->add(declarator);
	}
	return node;
}

// True when the syntax of `type_id` can only be a type.  A lone name that no
// declaration in scope made a type could equally be an expression, and a
// function type is never what a cast, `sizeof` or `typeid` is written with, so
// both are left to the expression reading of the same tokens.
bool AstParser::is_definite_type_id(const AstNode* type_id) const
{
	const AstNode* seq = type_id->children[0];
	bool definite = false;
	for (std::size_t index = 0; index < seq->children.size(); ++index)
	{
		const AstNode* specifier = seq->children[index];
		if (specifier->kind != AstKind::TypeName)
		{
			definite = true;
			continue;
		}
		const NameKind kind = names_.kind_of(specifier->text);
		if (kind == NameKind::Type ||
		    (kind == NameKind::Unknown &&
		     specifier->text.find_first_of(":<") != std::string::npos))
		{
			definite = true;
		}
	}
	if (!definite || type_id->children.size() < 2)
	{
		return definite;
	}
	return !declares_bare_function(type_id->children[1]);
}

// True when a declarator declares a function type outright, rather than a
// pointer or reference to one.  Redundant parentheses around such a declarator
// do not change that, which is what tells `(f(x))` apart from `(int(*)(int))`.
bool AstParser::declares_bare_function(const AstNode* declarator)
{
	if (declarator->children.empty())
	{
		return false;
	}
	const AstNode* first = declarator->children[0];
	if (first->kind == AstKind::ParameterClause)
	{
		return true;
	}
	return first->kind == AstKind::NestedDeclarator &&
		declares_bare_function(first->children[0]);
}

AstNode* AstParser::parse_ptr_operator()
{
	if (at(OP_STAR) || at(OP_AMP) || at(OP_LAND))
	{
		return make_terminal(AstKind::PtrOperator);
	}
	const Mark start = mark();
	if (skip_nested_name_specifier() && at(OP_STAR))
	{
		++pos_;
		return make_text(AstKind::PtrOperator, spelled(start));
	}
	reset(start);
	return nullptr;
}

AstNode* AstParser::parse_declarator(DeclaratorForm form)
{
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return nullptr;
	}
	const Mark start = mark();
	AstNode* node = make(form == DeclaratorForm::Abstract
		? AstKind::AbstractDeclarator
		: AstKind::Declarator);
	for (;;)
	{
		AstNode* op = parse_ptr_operator();
		if (op == nullptr)
		{
			break;
		}
		node->add(op);
		while (at(KW_CONST) || at(KW_VOLATILE))
		{
			node->add(make_terminal(AstKind::CvQualifier));
		}
	}
	skip_attributes();
	if (form != DeclaratorForm::New && at(OP_DOTS))
	{
		++pos_;
		node->add(make_text(AstKind::ParameterPack, "..."));
	}
	if (form == DeclaratorForm::New)
	{
		parse_declarator_suffixes(node, false);
		return node;
	}
	bool direct = false;
	if (at(OP_LPAREN) && form != DeclaratorForm::Named)
	{
		AstNode* clause = parse_parameter_clause();
		if (clause != nullptr)
		{
			node->add(clause);
			parse_function_suffixes(node);
			direct = true;
		}
	}
	if (!direct && at(OP_LPAREN))
	{
		const Mark nested = mark();
		++pos_;
		BracketGuard brackets(*this, false);
		// 8.1p1: a type-id names an unnamed object, so what stands inside the
		// parentheses of its declarator is a ptr-abstract-declarator and writes
		// no declarator-id either - which is what parts `int (*)(char)` from
		// `dispatch<T>(ex)`.  The latter is no type-id at all, so 5.4p2's
		// `( type-id ) cast-expression` is not what `(dispatch<T>(ex))(f, w)`
		// is: the parentheses hold an expression and the arguments after them
		// call the object it made.
		AstNode* inner = parse_declarator(form);
		if (inner != nullptr && !inner->children.empty() && at(OP_RPAREN))
		{
			++pos_;
			AstNode* wrapper = make(AstKind::NestedDeclarator);
			wrapper->add(inner);
			node->add(wrapper);
			direct = true;
		}
		else
		{
			reset(nested);
		}
	}
	if (!direct && form != DeclaratorForm::Abstract)
	{
		AstNode* id = parse_declarator_id();
		if (id != nullptr)
		{
			node->add(id);
			direct = true;
		}
	}
	if (!direct && form == DeclaratorForm::Named)
	{
		return fail(start);
	}
	{
		// 3.4.1p8: the rest of a declarator whose declarator-id is qualified is
		// read where that name reaches, so a parameter type or a
		// trailing-return-type may name what the class declares.
		ReachedGuard reached(names_, declarator_qualifier(node));
		parse_declarator_suffixes(node);
	}
	if (form == DeclaratorForm::Named && !has_declarator_identifier(node))
	{
		return fail(start);
	}
	return node;
}

void AstParser::parse_declarator_suffixes(AstNode* declarator, bool functions)
{
	for (;;)
	{
		if (at(OP_LPAREN) && functions)
		{
			AstNode* clause = parse_parameter_clause();
			if (clause == nullptr)
			{
				return;
			}
			declarator->add(clause);
			parse_function_suffixes(declarator);
			continue;
		}
		if (at(OP_LSQUARE))
		{
			const Mark start = mark();
			++pos_;
			BracketGuard brackets(*this, false);
			AstNode* suffix = make(AstKind::ArraySuffix);
			if (!at(OP_RSQUARE))
			{
				AstNode* bound = parse_expression();
				if (bound == nullptr)
				{
					reset(start);
					return;
				}
				suffix->add(bound);
			}
			if (!at(OP_RSQUARE))
			{
				reset(start);
				return;
			}
			++pos_;
			declarator->add(suffix);
			continue;
		}
		return;
	}
}

void AstParser::parse_function_suffixes(AstNode* declarator)
{
	for (;;)
	{
		skip_attributes();
		if (at(KW_CONST) || at(KW_VOLATILE))
		{
			declarator->add(make_terminal(AstKind::CvQualifier));
			continue;
		}
		if (at(OP_AMP) || at(OP_LAND))
		{
			declarator->add(make_text(AstKind::FunctionQualifier, spelling()));
			++pos_;
			continue;
		}
		if (at(TT_IDENTIFIER) && (spelling() == "override" || spelling() == "final"))
		{
			declarator->add(make_terminal(AstKind::VirtSpecifier));
			continue;
		}
		if (at(KW_NOEXCEPT))
		{
			AstNode* qualifier = parse_noexcept_qualifier();
			if (qualifier == nullptr)
			{
				return;
			}
			declarator->add(qualifier);
			continue;
		}
		if (at(KW_THROW) && peek(1) == OP_LPAREN)
		{
			const Mark start = mark();
			pos_ += 2;
			if (!skip_balanced(OP_RPAREN))
			{
				reset(start);
				return;
			}
			declarator->add(make_text(AstKind::FunctionQualifier, spelled(start)));
			continue;
		}
		if (at(OP_ARROW))
		{
			AstNode* trailing = parse_trailing_return_type();
			if (trailing == nullptr)
			{
				return;
			}
			declarator->add(trailing);
			continue;
		}
		return;
	}
}

// `noexcept` as a function suffix, with the constant expression a parenthesized
// one carries.
AstNode* AstParser::parse_noexcept_qualifier()
{
	const Mark start = mark();
	++pos_;
	AstNode* condition = nullptr;
	if (at(OP_LPAREN))
	{
		BracketGuard brackets(*this, false);
		++pos_;
		condition = parse_expression();
		if (condition == nullptr || !at(OP_RPAREN))
		{
			return fail(start);
		}
		++pos_;
	}
	AstNode* node = make_text(AstKind::FunctionQualifier, spelled(start));
	node->add(condition);
	return node;
}

// True when a declarator declares a function, which is what tells a function
// definition apart from a declaration with a braced initializer.
//
// 8.3p1 builds the type outside in, so which constructor the declarator-id
// ends up under is not the same question as which suffixes the declarator
// wrote.  A declarator hands its direct-declarator the type its ptr-operators
// made, and a direct-declarator hands its core the type its *outermost* suffix
// made - and the outermost is the last one, so the constructor the core is
// left with is the *first* suffix at its own level, or whatever the level
// around it handed down where that level wrote none.  `int (*f)(int)` is the
// pointer its own level makes of the parameter-clause above it, and
// `int (&f(int))[2]` is the function its own parameter-clause makes however
// many suffixes stand outside it.
bool AstParser::declares_function(const AstNode* declarator, bool inherited)
{
	std::size_t core = declarator->children.size();
	for (std::size_t index = 0; index < declarator->children.size(); ++index)
	{
		const AstKind kind = declarator->children[index]->kind;
		if (kind == AstKind::PtrOperator)
		{
			inherited = false;
		}
		if (kind == AstKind::Identifier || kind == AstKind::NestedDeclarator)
		{
			core = index;
			break;
		}
	}
	if (core == declarator->children.size())
	{
		return false;
	}
	for (std::size_t index = core + 1;
	     index < declarator->children.size(); ++index)
	{
		const AstKind kind = declarator->children[index]->kind;
		if (kind == AstKind::ParameterClause || kind == AstKind::ArraySuffix)
		{
			inherited = kind == AstKind::ParameterClause;
			break;
		}
	}
	const AstNode* node = declarator->children[core];
	if (node->kind != AstKind::NestedDeclarator)
	{
		return inherited;
	}
	return !node->children.empty() &&
		declares_function(node->children[0], inherited);
}

AstNode* AstParser::parse_trailing_return_type()
{
	const Mark start = mark();
	if (!accept(OP_ARROW))
	{
		return nullptr;
	}
	AstNode* type = parse_type_id();
	if (type == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::TrailingReturnType);
	const AstNode* seq = type->children[0];
	if (type->children.size() == 1 && seq->children.size() == 1 &&
	    seq->children[0]->kind == AstKind::TypeName)
	{
		node->text = seq->children[0]->text;
	}
	node->add(type);
	return node;
}

AstNode* AstParser::parse_parameter_clause()
{
	const Mark start = mark();
	if (!at(OP_LPAREN))
	{
		return nullptr;
	}
	AstNode* node = make(AstKind::ParameterClause);
	{
		BracketGuard brackets(*this, false);
		++pos_;
		while (!at(OP_RPAREN))
		{
			if (accept(OP_DOTS))
			{
				node->add(make_text(AstKind::ParameterPack, "..."));
				break;
			}
			AstNode* parameter = parse_parameter_declaration();
			if (parameter == nullptr)
			{
				return fail(start);
			}
			node->add(parameter);
			if (!accept(OP_COMMA))
			{
				break;
			}
		}
		if (!at(OP_RPAREN))
		{
			return fail(start);
		}
		++pos_;
	}
	return node;
}

AstNode* AstParser::parse_parameter_declaration()
{
	const Mark start = mark();
	skip_attributes();
	AstNode* specifiers = parse_specifier_seq(SpecifierMode::Decl);
	if (specifiers == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::ParameterDeclaration);
	node->add(specifiers);
	const Mark after_specifiers = mark();
	AstNode* declarator = parse_declarator(DeclaratorForm::Parameter);
	skip_attributes();
	if (declarator != nullptr && !at(OP_COMMA) && !at(OP_RPAREN) && !at(OP_ASS))
	{
		reset(after_specifiers);
		declarator = nullptr;
	}
	if (declarator != nullptr && !declarator->children.empty())
	{
		node->add(declarator);
	}
	if (at(OP_ASS) || at(OP_LBRACE))
	{
		AstNode* initializer = parse_initializer();
		if (initializer == nullptr)
		{
			return fail(start);
		}
		AstNode* argument = make(AstKind::DefaultArgument);
		argument->add(initializer);
		node->add(argument);
	}
	if (!at(OP_COMMA) && !at(OP_RPAREN))
	{
		return fail(start);
	}
	return node;
}

AstNode* AstParser::parse_init_declarator_list()
{
	const Mark start = mark();
	AstNode* node = make(AstKind::InitDeclaratorList);
	do
	{
		const Mark wrote = mark();
		AstNode* declarator = parse_declarator(DeclaratorForm::Named);
		if (declarator == nullptr)
		{
			return fail(start);
		}
		AstNode* item = make(AstKind::InitDeclarator);
		item->add(declarator);
		item->add(parse_initializer());
		// The terminals this one declarator and its initializer were written
		// from.  A declaration may declare several, and 3.7.1p3's object a
		// block declares `static` is named in the image by where it was
		// written - so the span belongs to the init-declarator and not to the
		// declaration that holds it.
		item->begin = static_cast<std::uint32_t>(wrote.pos);
		item->end = static_cast<std::uint32_t>(pos_);
		node->add(item);
	}
	while (accept(OP_COMMA));
	return node;
}

AstNode* AstParser::parse_initializer()
{
	const Mark start = mark();
	if (accept(OP_ASS))
	{
		AstNode* node = make(AstKind::Initializer);
		node->copied = true;
		if (at(KW_DEFAULT) || at(KW_DELETE))
		{
			node->add(make_text(AstKind::SpecialInitializer, spelling()));
			++pos_;
			return node;
		}
		AstNode* value = parse_initializer_clause();
		if (value == nullptr)
		{
			return fail(start);
		}
		node->add(value);
		return node;
	}
	if (at(OP_LBRACE))
	{
		AstNode* list = parse_braced_init_list();
		if (list == nullptr)
		{
			return nullptr;
		}
		AstNode* node = make(AstKind::Initializer);
		node->add(list);
		return node;
	}
	if (at(OP_LPAREN))
	{
		AstNode* list = parse_argument_suffix(AstKind::ParenInitializer);
		if (list == nullptr)
		{
			return nullptr;
		}
		AstNode* node = make(AstKind::Initializer);
		node->add(list);
		return node;
	}
	return nullptr;
}

const AstNode* AstParser::declarator_identifier(const AstNode* declarator)
{
	for (std::size_t index = 0; index < declarator->children.size(); ++index)
	{
		const AstNode* child = declarator->children[index];
		if (child->kind == AstKind::Identifier)
		{
			return child;
		}
		if (child->kind == AstKind::NestedDeclarator)
		{
			const AstNode* inner = declarator_identifier(child->children[0]);
			if (inner != nullptr)
			{
				return inner;
			}
		}
	}
	return nullptr;
}

bool AstParser::has_declarator_identifier(const AstNode* declarator)
{
	return declarator_identifier(declarator) != nullptr;
}

// 3.4.1p8: the nested-name-specifier a name was written with.  A `::` inside a
// template-argument-list or a parenthesized decltype-specifier belongs to the
// component around it, so the split is the one `QualifiedName` already makes
// rather than the last `::` of the spelling.
std::string AstParser::name_qualifier(const std::string& spelling)
{
	const QualifiedName written(spelling);
	if (!written.qualified())
	{
		return std::string();
	}
	return written.prefix();
}

std::string AstParser::declarator_qualifier(const AstNode* declarator)
{
	const AstNode* const id = declarator_identifier(declarator);
	return id == nullptr ? std::string() : name_qualifier(id->text);
}

void AstParser::declare_init_declarators(const AstNode* specifiers,
                                         const AstNode* declarators)
{
	bool is_typedef = false;
	for (std::size_t index = 0; index < specifiers->children.size(); ++index)
	{
		if (specifiers->children[index]->token == KW_TYPEDEF)
		{
			is_typedef = true;
		}
	}
	const NameKind kind =
		take_declared_kind(is_typedef ? NameKind::Type : NameKind::Value);
	if (declarators->kind == AstKind::Declarator)
	{
		const AstNode* id = declarator_identifier(declarators);
		if (id != nullptr)
		{
			names_.declare_member(id->text, kind);
		}
		return;
	}
	for (std::size_t index = 0; index < declarators->children.size(); ++index)
	{
		const AstNode* item = declarators->children[index];
		if (item->children.empty())
		{
			continue;
		}
		const AstNode* id = declarator_identifier(item->children[0]);
		if (id != nullptr)
		{
			names_.declare_member(id->text, kind);
		}
	}
}

// The names a template-declaration declared, which are template-names however
// the declaration inside it spells them.
void AstParser::declare_template_name(const AstNode* declaration)
{
	switch (declaration->kind)
	{
	case AstKind::TemplateDeclaration:
	case AstKind::ExplicitInstantiationDeclaration:
	case AstKind::ExplicitInstantiationDefinition:
		if (declaration->children.size() >= 2)
		{
			declare_template_name(declaration->children[1]);
		}
		return;

	case AstKind::ClassSpecifier:
	case AstKind::ClassForwardDeclaration:
		// 14.5.5p1: a class-head written on a template-id declares a
		// specialization of a template already declared, so the spelling names
		// the type its arguments make and no template of its own.
		names_.declare_member(
			declaration->text,
			class_head_is_template_id(declaration->text) ? NameKind::Type
			                                             : NameKind::Template);
		return;

	case AstKind::EnumSpecifier:
	case AstKind::AliasDeclaration:
		names_.declare_member(declaration->text, NameKind::Template);
		return;

	case AstKind::FunctionDefinition:
	case AstKind::SimpleDeclaration:
		break;

	default:
		return;
	}
	for (std::size_t index = 1; index < declaration->children.size(); ++index)
	{
		const AstNode* child = declaration->children[index];
		// 14p1: a template-declaration over a simple-declaration or a function
		// definition declares a function template, whose name names no type.
		if (child->kind == AstKind::Declarator)
		{
			const AstNode* id = declarator_identifier(child);
			if (id != nullptr)
			{
				names_.declare_member(id->text, NameKind::FunctionTemplate);
			}
		}
		if (child->kind != AstKind::InitDeclaratorList)
		{
			continue;
		}
		for (std::size_t item = 0; item < child->children.size(); ++item)
		{
			const AstNode* declarator = child->children[item];
			const AstNode* id = declarator->children.empty()
				? nullptr
				: declarator_identifier(declarator->children[0]);
			if (id != nullptr)
			{
				names_.declare_member(id->text, NameKind::FunctionTemplate);
			}
		}
	}
}

void AstParser::declare_parameters(const AstNode* declarator)
{
	for (std::size_t index = 0; index < declarator->children.size(); ++index)
	{
		const AstNode* child = declarator->children[index];
		if (child->kind != AstKind::ParameterClause)
		{
			continue;
		}
		for (std::size_t item = 0; item < child->children.size(); ++item)
		{
			const AstNode* parameter = child->children[item];
			if (parameter->children.size() < 2)
			{
				continue;
			}
			if (parameter->children[1]->kind != AstKind::Declarator)
			{
				continue;
			}
			const AstNode* id = declarator_identifier(parameter->children[1]);
			if (id != nullptr)
			{
				names_.declare(id->text, NameKind::Value);
			}
		}
	}
}
