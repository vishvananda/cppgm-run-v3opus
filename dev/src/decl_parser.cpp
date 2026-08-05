#include "decl_parser.h"

DeclParser::Specifiers::Specifiers()
	: is_typedef(false)
	, has_type_name(false)
	, cv(kCvNone)
	, type_name(kNoType)
	, builtins(0)
	, count(0)
{
	for (std::size_t index = 0; index < kSimpleTypeSpecifierCount; ++index)
	{
		counted[index] = 0;
	}
}

DeclParser::DeclParser(const std::vector<SemaToken>& tokens, TypeTable& types,
                       TranslationUnitModel& model)
	: tokens_(tokens)
	, pos_(0)
	, types_(types)
	, model_(model)
{}

void DeclParser::expect(unsigned type)
{
	if (!accept(type))
	{
		throw SemanticError("the translation unit does not match the grammar");
	}
}

void DeclParser::run()
{
	parse_declaration_seq(model_.global(), ST_EOF);
	expect(ST_EOF);
}

void DeclParser::parse_declaration_seq(Namespace& where, unsigned closer)
{
	while (!at(closer))
	{
		if (at(ST_EOF))
		{
			throw SemanticError("a namespace body reaches the end of the file");
		}
		parse_declaration(where);
	}
}

void DeclParser::parse_declaration(Namespace& where)
{
	// Every declarator of the last declaration has been turned into a type by
	// now, so its nodes go back at once.
	arena_.clear();

	if (accept(OP_SEMICOLON))
	{
		return;
	}
	if (at(KW_INLINE) && peek(1) == KW_NAMESPACE)
	{
		advance();
		parse_namespace_definition(where, true);
		return;
	}
	if (at(KW_NAMESPACE))
	{
		if (peek(1) == TT_IDENTIFIER && peek(2) == OP_ASS)
		{
			parse_namespace_alias_definition(where);
			return;
		}
		parse_namespace_definition(where, false);
		return;
	}
	if (at(KW_USING))
	{
		if (peek(1) == KW_NAMESPACE)
		{
			parse_using_directive(where);
			return;
		}
		if (peek(1) == TT_IDENTIFIER && peek(2) == OP_ASS)
		{
			parse_alias_declaration(where);
			return;
		}
		parse_using_declaration(where);
		return;
	}
	parse_simple_declaration(where);
}

void DeclParser::parse_namespace_definition(Namespace& where, bool is_inline)
{
	expect(KW_NAMESPACE);
	NameId name = kNoName;
	if (at(TT_IDENTIFIER))
	{
		name = name_at();
		advance();
	}
	expect(OP_LBRACE);

	Namespace& space = name == kNoName
		? model_.open_unnamed_namespace(where, is_inline)
		: model_.open_namespace(where, name, is_inline);

	parse_declaration_seq(space, OP_RBRACE);
	expect(OP_RBRACE);
}

void DeclParser::parse_namespace_alias_definition(Namespace& where)
{
	expect(KW_NAMESPACE);
	const NameId name = name_at();
	expect(TT_IDENTIFIER);
	expect(OP_ASS);
	Entity& target = resolve_namespace_name(where);
	expect(OP_SEMICOLON);
	model_.bind(where, name, target);
}

void DeclParser::parse_using_directive(Namespace& where)
{
	expect(KW_USING);
	expect(KW_NAMESPACE);
	Entity& target = resolve_namespace_name(where);
	expect(OP_SEMICOLON);
	model_.nominate(where, *target.space);
}

void DeclParser::parse_using_declaration(Namespace& where)
{
	expect(KW_USING);
	Namespace* scope = nullptr;
	if (!parse_nested_name_specifier(where, scope))
	{
		throw SemanticError("a using-declaration names no namespace");
	}
	if (!at(TT_IDENTIFIER))
	{
		throw SemanticError("a using-declaration names nothing");
	}
	const NameId name = name_at();
	advance();
	expect(OP_SEMICOLON);

	Entity* entity = model_.lookup_qualified(*scope, name, LookupFilter::Any);
	if (entity == nullptr)
	{
		throw SemanticError("a using-declaration names an entity that does not exist");
	}
	model_.bind(where, name, *entity);
}

void DeclParser::parse_alias_declaration(Namespace& where)
{
	expect(KW_USING);
	const NameId name = name_at();
	expect(TT_IDENTIFIER);
	expect(OP_ASS);
	TypeId type = kNoType;
	if (!parse_type_id(where, type))
	{
		throw SemanticError("an alias-declaration has no type-id");
	}
	expect(OP_SEMICOLON);
	model_.declare(where, EntityKind::Typedef, name, type);
}

void DeclParser::parse_simple_declaration(Namespace& where)
{
	Specifiers specifiers;
	if (!parse_specifier_seq(where, true, specifiers))
	{
		throw SemanticError("a declaration has no decl-specifier-seq");
	}
	const TypeId base = specifier_type(specifiers);

	for (;;)
	{
		DeclaratorId id;
		DeclaratorNode* node = parse_declarator(where, true, id);
		if (node == nullptr || id.name == kNoName)
		{
			throw SemanticError("a declarator declares no name");
		}
		declare(where, specifiers, id, build_type(*node, base));
		if (!accept(OP_COMMA))
		{
			break;
		}
	}
	expect(OP_SEMICOLON);
}

void DeclParser::declare(Namespace& where, const Specifiers& specifiers,
                         const DeclaratorId& id, TypeId type)
{
	EntityKind kind = EntityKind::Variable;
	if (specifiers.is_typedef)
	{
		kind = EntityKind::Typedef;
	}
	else if (types_.kind(type) == TypeKind::Function)
	{
		kind = EntityKind::Function;
	}

	if (id.qualifier == nullptr)
	{
		model_.declare(where, kind, id.name, type);
		return;
	}

	// 8.3p1: a qualified declarator-id redeclares a member of the namespace it
	// names, so nothing new is declared and the entity keeps the place it
	// already has in that namespace.
	Entity* entity = model_.lookup_qualified(*id.qualifier, id.name, LookupFilter::Any);
	if (entity == nullptr)
	{
		throw SemanticError("a qualified declarator-id names nothing");
	}
	model_.redeclare(*entity, kind, type);
}

bool DeclParser::parse_nested_name_specifier(Namespace& where, Namespace*& out)
{
	// Nothing is consumed until a namespace has been found for it, so a
	// specifier that turns out not to name one leaves the cursor where it was
	// without a mark to restore.
	Namespace* scope = nullptr;

	if (accept(OP_COLON2))
	{
		scope = &model_.global();
	}
	else if (at(TT_IDENTIFIER) && peek(1) == OP_COLON2)
	{
		Entity* entity = model_.lookup_unqualified(where, name_at(), LookupFilter::Space);
		if (entity == nullptr)
		{
			return false;
		}
		scope = entity->space;
		advance();
		advance();
	}
	else
	{
		return false;
	}

	while (at(TT_IDENTIFIER) && peek(1) == OP_COLON2)
	{
		Entity* entity = model_.lookup_qualified(*scope, name_at(), LookupFilter::Space);
		if (entity == nullptr)
		{
			break;
		}
		scope = entity->space;
		advance();
		advance();
	}

	out = scope;
	return true;
}

Entity& DeclParser::resolve_namespace_name(Namespace& where)
{
	Namespace* scope = nullptr;
	const bool qualified = parse_nested_name_specifier(where, scope);
	if (!at(TT_IDENTIFIER))
	{
		throw SemanticError("a namespace name is expected");
	}
	Entity* entity = qualified
		? model_.lookup_qualified(*scope, name_at(), LookupFilter::Space)
		: model_.lookup_unqualified(where, name_at(), LookupFilter::Space);
	if (entity == nullptr)
	{
		throw SemanticError("a namespace name names no namespace");
	}
	advance();
	return *entity;
}

bool DeclParser::parse_type_name(Namespace& where, TypeId& out)
{
	const Mark saved = mark();
	Namespace* scope = nullptr;
	const bool qualified = parse_nested_name_specifier(where, scope);
	if (!at(TT_IDENTIFIER))
	{
		reset(saved);
		return false;
	}
	Entity* entity = qualified
		? model_.lookup_qualified(*scope, name_at(), LookupFilter::Type)
		: model_.lookup_unqualified(where, name_at(), LookupFilter::Type);
	if (entity == nullptr)
	{
		reset(saved);
		return false;
	}
	advance();
	out = entity->type;
	return true;
}

int DeclParser::builtin_specifier(unsigned token)
{
	switch (token)
	{
	case KW_CHAR: return kSpecChar;
	case KW_CHAR16_T: return kSpecChar16;
	case KW_CHAR32_T: return kSpecChar32;
	case KW_WCHAR_T: return kSpecWchar;
	case KW_BOOL: return kSpecBool;
	case KW_SHORT: return kSpecShort;
	case KW_INT: return kSpecInt;
	case KW_LONG: return kSpecLong;
	case KW_SIGNED: return kSpecSigned;
	case KW_UNSIGNED: return kSpecUnsigned;
	case KW_FLOAT: return kSpecFloat;
	case KW_DOUBLE: return kSpecDouble;
	case KW_VOID: return kSpecVoid;
	default: return -1;
	}
}

bool DeclParser::parse_specifier_seq(Namespace& where, bool declaration, Specifiers& out)
{
	out = Specifiers();
	for (;;)
	{
		const unsigned token = peek();
		if (declaration &&
		    (token == KW_STATIC || token == KW_THREAD_LOCAL || token == KW_EXTERN))
		{
			advance();
			++out.count;
			continue;
		}
		if (declaration && token == KW_TYPEDEF)
		{
			advance();
			out.is_typedef = true;
			++out.count;
			continue;
		}
		if (token == KW_CONST || token == KW_VOLATILE)
		{
			advance();
			out.cv |= token == KW_CONST ? kCvConst : kCvVolatile;
			++out.count;
			continue;
		}

		const int builtin = builtin_specifier(token);
		if (builtin >= 0 && !out.has_type_name)
		{
			advance();
			++out.counted[builtin];
			++out.builtins;
			++out.count;
			continue;
		}

		// A type-name is a specifier only while the sequence still has no
		// type: once one has been read, the next identifier is the
		// declarator-id.
		if ((token == TT_IDENTIFIER || token == OP_COLON2) && !out.has_type_name &&
		    out.builtins == 0)
		{
			TypeId named = kNoType;
			if (!parse_type_name(where, named))
			{
				break;
			}
			out.has_type_name = true;
			out.type_name = named;
			++out.count;
			continue;
		}
		break;
	}
	return out.count != 0;
}

TypeId DeclParser::specifier_type(const Specifiers& specifiers)
{
	if (specifiers.has_type_name)
	{
		return types_.qualified(specifiers.type_name, specifiers.cv);
	}
	if (specifiers.builtins == 0)
	{
		throw SemanticError("a declaration names no type");
	}
	return types_.qualified(types_.fundamental(table_10_type(specifiers.counted)),
	                       specifiers.cv);
}

// A specifier that decides the type on its own is asked about first; what is
// left is the integer types, which `signed`, `unsigned`, `short` and `long`
// choose between.
EFundamentalType DeclParser::table_10_type(const unsigned* counted)
{
	const bool is_unsigned = counted[kSpecUnsigned] != 0;
	if (counted[kSpecVoid] != 0)
	{
		return FT_VOID;
	}
	if (counted[kSpecBool] != 0)
	{
		return FT_BOOL;
	}
	if (counted[kSpecChar] != 0)
	{
		if (is_unsigned)
		{
			return FT_UNSIGNED_CHAR;
		}
		return counted[kSpecSigned] != 0 ? FT_SIGNED_CHAR : FT_CHAR;
	}
	if (counted[kSpecChar16] != 0)
	{
		return FT_CHAR16_T;
	}
	if (counted[kSpecChar32] != 0)
	{
		return FT_CHAR32_T;
	}
	if (counted[kSpecWchar] != 0)
	{
		return FT_WCHAR_T;
	}
	if (counted[kSpecFloat] != 0)
	{
		return FT_FLOAT;
	}
	if (counted[kSpecDouble] != 0)
	{
		return counted[kSpecLong] != 0 ? FT_LONG_DOUBLE : FT_DOUBLE;
	}
	if (counted[kSpecShort] != 0)
	{
		return is_unsigned ? FT_UNSIGNED_SHORT_INT : FT_SHORT_INT;
	}
	if (counted[kSpecLong] >= 2)
	{
		return is_unsigned ? FT_UNSIGNED_LONG_LONG_INT : FT_LONG_LONG_INT;
	}
	if (counted[kSpecLong] == 1)
	{
		return is_unsigned ? FT_UNSIGNED_LONG_INT : FT_LONG_INT;
	}
	return is_unsigned ? FT_UNSIGNED_INT : FT_INT;
}

bool DeclParser::parse_type_id(Namespace& where, TypeId& out)
{
	Specifiers specifiers;
	if (!parse_specifier_seq(where, false, specifiers))
	{
		return false;
	}
	if (!specifiers.has_type_name && specifiers.builtins == 0)
	{
		return false;
	}
	const TypeId base = specifier_type(specifiers);
	DeclaratorId id;
	DeclaratorNode* node = parse_declarator(where, false, id);
	out = node == nullptr ? base : build_type(*node, base);
	return true;
}
