#include "decl_parser.h"

// The expression half of the PA8 parser.
//
// `pa8.gram` gives an expression no sub-expressions worth the name: a literal,
// a keyword literal, an id-expression, or any of those in parentheses.  So one
// pass produces everything clause 3 and clause 5 say about it - its type, its
// value category and what is known of its value - and the context it appears in
// never has to be pushed down into it.

namespace
{

// 2.14.2: which literals are *integer* literals, which is what 4.10p1 asks of
// a null pointer constant.  A character literal has a character type and a
// boolean literal is a keyword, so the type alone separates the three.
bool is_integer_literal(EFundamentalType type)
{
	switch (type)
	{
	case FT_INT:
	case FT_LONG_INT:
	case FT_LONG_LONG_INT:
	case FT_UNSIGNED_INT:
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT:
		return true;

	default:
		return false;
	}
}

}

ExprValue DeclParser::keyword_expression(unsigned token)
{
	ExprValue out;
	if (token == KW_NULLPTR)
	{
		// The unnamed fundamental type of 2.14.7, whose only value is the null
		// pointer constant of 4.10p1.
		out.type = types_.fundamental(FT_NULLPTR_T);
		out.value = ConstValue::null();
		out.null_constant = true;
		return out;
	}
	out.type = types_.fundamental(FT_BOOL);
	out.value = ConstValue::integer(token == KW_TRUE ? 1 : 0);
	return out;
}

ExprValue DeclParser::literal_expression()
{
	const LiteralValue& literal = literal_at();
	if (literal.type == kFundamentalTypeCount)
	{
		throw SemanticError("a user-defined literal is not an expression of this grammar");
	}

	ExprValue out;
	if (literal.array)
	{
		// 2.14.5: a string literal is an lvalue naming an object of type
		// `array of n const T`, and that object is part of the program.
		const TypeId element =
			types_.qualified(types_.fundamental(literal.type), kCvConst);
		const TypeId type = types_.array_of(element, true, literal.elements);
		const Symbol& object =
			image_->add_string_literal(type, literal.data, initializing_);
		out.type = type;
		out.category = ValueCategory::LValue;
		out.value = ConstValue::address(object.id, 0);
		out.string_literal = true;
		return out;
	}

	out.type = types_.fundamental(literal.type);
	if (literal.integral())
	{
		const unsigned long long value = literal.integer();
		out.value = ConstValue::integer(value);
		out.null_constant = value == 0 && is_integer_literal(literal.type);
		return out;
	}
	out.value = ConstValue::real(literal.real());
	return out;
}

ExprValue DeclParser::entity_expression(const Entity& entity)
{
	if (entity.kind != EntityKind::Variable && entity.kind != EntityKind::Function)
	{
		throw SemanticError("an expression names something that is not an object "
		                    "or a function");
	}
	// 13.4: naming an overloaded function needs a target type to choose by,
	// which no context of `pa8.gram` supplies.
	if (entity.overload != nullptr)
	{
		throw SemanticError("an expression names an overloaded function");
	}

	// 3.10p1: an id-expression is an lvalue, and a reference is dereferenced
	// where it is named, so the lvalue designates whatever the reference was
	// bound to rather than the reference itself.
	ExprValue out;
	out.category = ValueCategory::LValue;
	const Symbol& symbol = image_->at(entity.symbol);
	if (types_.is_reference(entity.type))
	{
		out.type = types_.target(entity.type);
		if (symbol.initialized && symbol.value.kind == ValueKind::Address)
		{
			out.value = symbol.value;
		}
		return out;
	}
	out.type = entity.type;
	out.value = ConstValue::address(entity.symbol, 0);
	return out;
}

bool DeclParser::parse_id_expression(ExprValue& out)
{
	const Mark saved = mark();
	Namespace* scope = nullptr;
	const bool qualified = parse_nested_name_specifier(*value_scope_, scope);
	if (!at(TT_IDENTIFIER))
	{
		reset(saved);
		return false;
	}

	const NameId name = name_at();
	const Entity* entity =
		qualified ? model_.lookup_qualified(*scope, name, LookupFilter::Any)
		          : model_.lookup_unqualified(*value_scope_, name, LookupFilter::Any);
	if (entity == nullptr)
	{
		throw SemanticError("an expression names something that was not declared");
	}
	advance();
	out = entity_expression(*entity);
	return true;
}

bool DeclParser::parse_expression(ExprValue& out)
{
	switch (peek())
	{
	case KW_TRUE:
	case KW_FALSE:
	case KW_NULLPTR:
		out = keyword_expression(peek());
		advance();
		return true;

	case TT_LITERAL:
		out = literal_expression();
		advance();
		return true;

	case OP_LPAREN:
	{
		// Parentheses are the one place an expression of this grammar
		// re-enters itself, so they share the bound on how much machine stack
		// a file may ask for.
		const ParseDepth::Frame frame(depth_);
		if (frame.overflowed())
		{
			throw SemanticError("an expression nests deeper than the tool supports");
		}
		const Mark saved = mark();
		advance();
		if (!parse_expression(out) || !accept(OP_RPAREN))
		{
			reset(saved);
			return false;
		}
		return true;
	}

	default:
		return parse_id_expression(out);
	}
}
