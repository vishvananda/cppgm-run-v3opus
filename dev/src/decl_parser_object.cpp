#include "decl_parser.h"

// The object half of the PA8 parser: what a declaration says about the object
// it declares, which of the program's objects that is, and what the object
// holds.
//
// A declaration reaches the program image only here.  Everything before this
// point is about one translation unit - what its namespaces declare and what
// types its declarators build - and everything from here is about the program,
// because 3.5 makes two declarations in two units name one entity and 3.2 makes
// one of them its definition.

void DeclParser::parse_static_assert_declaration(Namespace& where)
{
	value_scope_ = &where;
	expect(KW_STATIC_ASSERT);
	expect(OP_LPAREN);
	ExprValue condition;
	initializing_ = false;
	if (!parse_expression(condition))
	{
		throw SemanticError("a static_assert declaration has no condition");
	}
	expect(OP_COMMA);
	// The message is a token of the declaration rather than an expression in
	// it, so it names no object of the program.
	if (!at(TT_LITERAL))
	{
		throw SemanticError("a static_assert declaration has no message");
	}
	advance();
	expect(OP_RPAREN);
	expect(OP_SEMICOLON);

	if (!init_->to_bool(condition))
	{
		throw SemanticError("a static_assert declaration is false");
	}
}

Symbol& DeclParser::link_object(Namespace& home, const Specifiers& specifiers,
                                Entity& entity, NameId name, TypeId type)
{
	const SymbolKind kind = entity.kind == EntityKind::Function ? SymbolKind::Function
	                                                            : SymbolKind::Variable;

	// 7.1.1p4 and 7.1.2p1: `thread_local` names a variable and `inline` names a
	// function, and a decl-specifier-seq of this grammar can carry either onto
	// the wrong one.  7.1.5p3 asks a constexpr function for a return type an
	// object can be made of, which `void` is not.
	if (specifiers.is_thread_local && kind != SymbolKind::Variable)
	{
		throw SemanticError("a function is declared thread_local");
	}
	if (specifiers.is_inline && kind != SymbolKind::Function)
	{
		throw SemanticError("a variable is declared inline");
	}
	if (specifiers.is_constexpr && kind == SymbolKind::Function &&
	    types_.is_void(types_.target(type)))
	{
		throw SemanticError("a constexpr function returns a type no object can have");
	}

	if (entity.symbol == 0)
	{
		// 3.5p3: a name declared `static` has internal linkage, and so does a
		// non-volatile const variable that is neither declared `extern` nor
		// already an entity of external linkage - which, within one unit, is
		// exactly the case where the entity already has an object.
		const unsigned cv = types_.object_cv(type);
		const bool internal = specifiers.is_static ||
			(kind == SymbolKind::Variable && !specifiers.is_extern &&
			 (cv & kCvConst) != 0 && (cv & kCvVolatile) == 0);
		entity.symbol = image_->declare(kind, home, name, type, internal).id;
	}
	else if (specifiers.is_static && !image_->at(entity.symbol).internal)
	{
		// 7.1.1p7: the linkages successive declarations imply shall agree.
		// `static` is the only specifier that says internal linkage outright;
		// the const rule of 3.5p3 defers to what the entity already has, so it
		// is only the first declaration that can decide.
		throw SemanticError("a declaration declares static an entity another "
		                    "declaration gave external linkage");
	}

	Symbol& symbol = image_->at(entity.symbol);
	symbol.type = model_.merged(symbol.type, type);
	symbol.is_inline = symbol.is_inline || specifiers.is_inline;
	symbol.is_constexpr = symbol.is_constexpr || specifiers.is_constexpr;

	// 7.1.1p1: if `thread_local` appears in any declaration of a variable it
	// appears in all of them.
	if (symbol.declarations == 0)
	{
		symbol.is_thread_local = specifiers.is_thread_local;
	}
	else if (symbol.is_thread_local != specifiers.is_thread_local)
	{
		throw SemanticError("two declarations of a variable disagree about thread_local");
	}
	++symbol.declarations;
	return symbol;
}

bool DeclParser::defines_object(const Specifiers& specifiers, TypeId type,
                                bool has_initializer, bool is_function_body)
{
	if (types_.kind(type) == TypeKind::Function)
	{
		// 3.1p2: a function declaration is a definition only when it has a
		// body.
		return is_function_body;
	}
	// 7.1.5p9: every declaration of a constexpr variable initializes it.
	if (specifiers.is_constexpr && !has_initializer)
	{
		throw SemanticError("a constexpr variable is declared without an initializer");
	}
	// 3.1p2: a declaration with `extern` and no initializer declares without
	// defining.
	if (specifiers.is_extern && !has_initializer)
	{
		return false;
	}

	if (!has_initializer)
	{
		// 8.5.3p3 and 8.5p6.
		if (types_.is_reference(type))
		{
			throw SemanticError("a reference is defined without an initializer");
		}
		if ((types_.object_cv(type) & kCvConst) != 0)
		{
			throw SemanticError("an object of a const type is defined without "
			                    "an initializer");
		}
	}
	return true;
}

void DeclParser::set_initial_value(Symbol& symbol, const Specifiers& specifiers,
                                   TypeId& type, const ExprValue& source)
{
	const Initialization result = init_->initialize(type, source);
	type = result.type;
	if (specifiers.is_constexpr && !result.constant)
	{
		throw SemanticError("a constexpr variable has an initializer that is not "
		                    "a constant expression");
	}
	// 3.6.2: an object whose initializer is a constant expression is constant
	// initialized; every other object of static storage duration is zero
	// initialized, which is what the image already holds for it.
	if (!result.constant)
	{
		return;
	}
	symbol.initialized = true;
	symbol.value = result.value;
	// 8.5.2p2: the characters of the string literal are copied into the array
	// being initialized, which is an object of its own.
	symbol.from_string = result.literal != 0;
	if (symbol.from_string)
	{
		symbol.bytes = image_->at(result.literal).bytes;
	}
}

void DeclParser::analyse_object(Namespace& where, const Specifiers& specifiers,
                                const DeclaratorId& id, Entity& entity, TypeId type,
                                bool is_function_body)
{
	if (entity.kind == EntityKind::Typedef)
	{
		if (at(OP_ASS))
		{
			throw SemanticError("a typedef declaration has an initializer");
		}
		return;
	}

	Namespace& home = id.qualifier != nullptr ? *id.qualifier : where;
	Symbol& symbol = link_object(home, specifiers, entity, id.name, type);

	if (is_function_body)
	{
		expect(OP_LBRACE);
		expect(OP_RBRACE);
	}

	ExprValue source;
	bool has_initializer = false;
	if (!is_function_body && accept(OP_ASS))
	{
		if (entity.kind == EntityKind::Function)
		{
			throw SemanticError("a function declaration has an initializer");
		}
		initializing_ = true;
		if (!parse_expression(source))
		{
			throw SemanticError("an initializer is not an expression");
		}
		initializing_ = false;
		has_initializer = true;
	}

	const bool definition =
		defines_object(specifiers, type, has_initializer, is_function_body);
	if (definition)
	{
		// 3.2p3 and 3.2p5: one definition in the program, except that an
		// inline function is defined once in each unit that needs it.
		const bool inline_elsewhere = symbol.is_inline &&
			entity.kind == EntityKind::Function && symbol.unit != image_->unit();
		if (symbol.defined && !inline_elsewhere)
		{
			throw SemanticError("an entity of the program is defined twice");
		}
		symbol.defined = true;
		symbol.unit = image_->unit();
	}

	if (has_initializer)
	{
		TypeId completed = type;
		set_initial_value(symbol, specifiers, completed, source);
		if (completed != type)
		{
			// 8.5.2p1: the string literal gave the array its bound.
			model_.redeclare(entity, entity.kind, completed);
			symbol.type = model_.merged(symbol.type, completed);
		}
	}

	if (definition && types_.is_incomplete(symbol.type))
	{
		throw SemanticError("an object is defined with a type that has no size");
	}
}
