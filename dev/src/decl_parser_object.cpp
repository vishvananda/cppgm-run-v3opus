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

namespace
{

// 3.9.3p5: an array is as cv-qualified as its elements, so the qualifiers an
// object was declared with are read past however many dimensions there are.
unsigned object_cv(const TypeTable& types, TypeId type)
{
	while (types.kind(type) == TypeKind::Array)
	{
		type = types.target(type);
	}
	return types.cv(type);
}

// 5.19p2: an lvalue-to-rvalue conversion may read a `constexpr` object, or a
// const non-volatile object of integral type whose initializer was a constant
// expression.
bool readable_object(const TypeTable& types, const Symbol& symbol)
{
	if (!symbol.initialized || symbol.from_string)
	{
		return false;
	}
	if (symbol.is_constexpr)
	{
		return true;
	}
	const unsigned cv = object_cv(types, symbol.type);
	if ((cv & kCvConst) == 0 || (cv & kCvVolatile) != 0)
	{
		return false;
	}
	return types.kind(symbol.type) == TypeKind::Fundamental &&
		fundamental_type_is_integral(types.fundamental_type(symbol.type));
}

}

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
	if (entity.symbol == 0)
	{
		// 3.5p3: a name declared `static` has internal linkage, and so does a
		// non-volatile const variable that is neither declared `extern` nor
		// already an entity of external linkage - which, within one unit, is
		// exactly the case where the entity already has an object.
		const unsigned cv = object_cv(types_, type);
		const bool internal = specifiers.is_static ||
			(kind == SymbolKind::Variable && !specifiers.is_extern &&
			 (cv & kCvConst) != 0 && (cv & kCvVolatile) == 0);
		entity.symbol = image_->declare(kind, home, name, type, internal).id;
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
	if (specifiers.is_inline)
	{
		throw SemanticError("a variable is declared inline");
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
		if ((object_cv(types_, type) & kCvConst) != 0)
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
	symbol.bytes = result.bytes;
	symbol.from_string = result.from_string;
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
	symbol.readable = readable_object(types_, symbol);
}
