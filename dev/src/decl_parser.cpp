#include "decl_parser.h"

DeclParser::Specifiers::Specifiers()
	: is_typedef(false)
	, is_static(false)
	, is_extern(false)
	, is_thread_local(false)
	, is_inline(false)
	, is_constexpr(false)
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

DeclParser::DeclParser(const std::vector<SemaToken>& tokens,
                       const std::vector<LiteralValue>& literals, TypeTable& types,
                       TranslationUnitModel& model, const ImageContext* image)
	: tokens_(tokens)
	, literals_(literals)
	, pos_(0)
	, types_(types)
	, model_(model)
	, image_(image == nullptr ? nullptr : image->image)
	, init_(image == nullptr ? nullptr : image->init)
	, value_scope_(&model.global())
	, initializing_(false)
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
	if (image_ != nullptr && at(KW_STATIC_ASSERT))
	{
		parse_static_assert_declaration(where);
		return;
	}
	parse_simple_declaration(where);
}

void DeclParser::parse_namespace_definition(Namespace& where, bool is_inline)
{
	// A namespace body is the other place the descent re-enters itself, and a
	// file can nest one as deeply as it is long, so it shares the declarator's
	// bound on how much machine stack a file may ask for.
	const ParseDepth::Frame frame(depth_);
	if (frame.overflowed())
	{
		throw SemanticError("a namespace nests deeper than the tool supports");
	}

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
	model_.bind_alias(where, name, target);
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
	// 7.3.3p8: a using-declaration shall not name a namespace.
	if (entity->kind == EntityKind::Namespace)
	{
		throw SemanticError("a using-declaration names a namespace");
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
	// 3.4.3p3: a name written here is looked up in this namespace until a
	// qualified declarator-id says otherwise.
	value_scope_ = &where;

	Specifiers specifiers;
	if (!parse_specifier_seq(where, true, specifiers))
	{
		throw SemanticError("a declaration has no decl-specifier-seq");
	}
	check_storage_class(specifiers);
	const TypeId base = specifier_type(specifiers);

	bool first = true;
	for (;;)
	{
		DeclaratorId id;
		bool function_declarator = false;
		DeclaratorNode* node = parse_declarator(where, true, id);
		if (node == nullptr || id.name == kNoName)
		{
			throw SemanticError("a declarator declares no name");
		}
		TypeId type = build_type(*node, base, &function_declarator);
		if (specifiers.is_constexpr)
		{
			// 7.1.5p1: `constexpr` on the declaration of an object declares
			// the object const, which is what decides its linkage under 3.5p3
			// and what a redeclaration of it has to say too.
			type = types_.qualified(type, kCvConst);
		}

		// `function-definition` and `simple-declaration` share their first two
		// parts, so which one this is only becomes clear at the body.
		const bool body = image_ != nullptr && first && at(OP_LBRACE);
		if (body && (!function_declarator || types_.kind(type) != TypeKind::Function))
		{
			// 8.4p1: the declarator of a function-definition shall declare a
			// function, which a typedef-name for one does not.
			throw SemanticError("a function definition does not declare a function");
		}

		Entity& entity = declare(where, specifiers, id, type);
		if (image_ != nullptr)
		{
			analyse_object(where, specifiers, id, entity, type, body);
		}
		if (body)
		{
			return;
		}
		first = false;
		if (!accept(OP_COMMA))
		{
			break;
		}
	}
	expect(OP_SEMICOLON);
}

EntityKind DeclParser::declared_kind(const Specifiers& specifiers, TypeId type,
                                     const TypeTable& types)
{
	if (specifiers.is_typedef)
	{
		return EntityKind::Typedef;
	}
	return types.kind(type) == TypeKind::Function ? EntityKind::Function
	                                             : EntityKind::Variable;
}

// 7.1.1p1: a declaration has at most one storage class specifier, except that
// `thread_local` may join `static` or `extern`, and `typedef` is a specifier
// that admits none of the others.
void DeclParser::check_storage_class(const Specifiers& specifiers)
{
	if (specifiers.is_static && specifiers.is_extern)
	{
		throw SemanticError("a declaration is both static and extern");
	}
	if (specifiers.is_typedef &&
	    (specifiers.is_static || specifiers.is_extern || specifiers.is_thread_local ||
	     specifiers.is_inline || specifiers.is_constexpr))
	{
		throw SemanticError("a typedef declaration has another specifier");
	}
}

Entity& DeclParser::declare(Namespace& where, const Specifiers& specifiers,
                            const DeclaratorId& id, TypeId type)
{
	const EntityKind kind = declared_kind(specifiers, type, types_);
	if (id.qualifier == nullptr)
	{
		return model_.declare(where, kind, id.name, type);
	}

	// 7.3.1.2p2: a member may be declared outside its namespace, but only in a
	// namespace that encloses it.
	if (!encloses(where, *id.qualifier))
	{
		throw SemanticError("a qualified declarator-id names a member of a namespace "
		                    "this one does not enclose");
	}

	// 8.3p1: a qualified declarator-id redeclares a member of the namespace it
	// names, so nothing new is declared and the entity keeps the place it
	// already has in that namespace.
	Entity* entity = model_.lookup_qualified(*id.qualifier, id.name, LookupFilter::Any);
	if (entity != nullptr && kind == EntityKind::Function &&
	    entity->kind == EntityKind::Function)
	{
		// 13.1: which of the functions the name reaches this redeclares is
		// decided by the parameter type list, which the overload set is
		// indexed by rather than searched for.
		entity = model_.find_overload(*entity, types_.signature(type));
	}
	if (entity == nullptr)
	{
		throw SemanticError("a qualified declarator-id names nothing");
	}
	model_.redeclare(*entity, kind, type);
	return *entity;
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

bool DeclParser::parse_specifier_seq(Namespace& where, bool declaration, Specifiers& out)
{
	out = Specifiers();
	for (;;)
	{
		const unsigned token = peek();
		if (declaration &&
		    (token == KW_STATIC || token == KW_THREAD_LOCAL || token == KW_EXTERN ||
		     token == KW_TYPEDEF || token == KW_INLINE || token == KW_CONSTEXPR))
		{
			advance();
			out.is_static |= token == KW_STATIC;
			out.is_thread_local |= token == KW_THREAD_LOCAL;
			out.is_extern |= token == KW_EXTERN;
			out.is_typedef |= token == KW_TYPEDEF;
			out.is_inline |= token == KW_INLINE;
			out.is_constexpr |= token == KW_CONSTEXPR;
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
	if (!table_10_names_a_type(specifiers.counted))
	{
		throw SemanticError("the simple-type-specifiers of a declaration name no type");
	}
	return types_.qualified(types_.fundamental(table_10_type(specifiers.counted)),
	                       specifiers.cv);
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
