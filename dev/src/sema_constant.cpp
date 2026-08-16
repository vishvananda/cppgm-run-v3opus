#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "literal_scan.h"
#include "post_token.h"
#include "sema_constexpr.h"
#include "sema_pack.h"
#include "string_literal.h"

// The 5.19 subset PA11 needs: what an array bound, an enumerator and a
// static_assert may be written with, plus the 7.1.6.2 decltype forms.
//
// A value is carried as its bits and the type that says how wide it is and
// whether it is signed, sign extended to 64 bits when signed, which is the one
// form every conversion here starts from.

namespace
{

// 2.14: the bytes 2.14.2 and 2.14.3 give a literal, read back as one value.
unsigned long long integer_of(const std::string& data)
{
	unsigned long long value = 0;
	for (std::size_t index = data.size(); index-- > 0;)
	{
		value = (value << 8) | static_cast<unsigned char>(data[index]);
	}
	return value;
}


}

bool SemaAnalyzer::is_signed(TypeId type) const
{
	const TypeId arithmetic = arithmetic_type(type);
	if (arithmetic == kNoType)
	{
		return true;
	}
	return fundamental_type_class(types_.fundamental_type(arithmetic)) ==
		FundamentalTypeClass::SignedIntegral;
}

unsigned SemaAnalyzer::width_of(TypeId type) const
{
	const TypeId arithmetic = arithmetic_type(type);
	if (arithmetic == kNoType)
	{
		return 64;
	}
	return static_cast<unsigned>(
		fundamental_type_size(types_.fundamental_type(arithmetic)) * 8);
}

// 4.5 and 7.2p9: the integral type a value of `type` is read as, which for an
// enumeration is its underlying type and for anything else is itself.
TypeId SemaAnalyzer::arithmetic_type(TypeId type) const
{
	TypeId current = type;
	if (types_.kind(current) == TypeKind::Enum)
	{
		current = types_.target(current);
	}
	if (types_.kind(current) != TypeKind::Fundamental ||
	    !fundamental_type_is_integral(types_.fundamental_type(current)))
	{
		return kNoType;
	}
	return current;
}

std::string SemaAnalyzer::spell_value(TypeId type, unsigned long long bits) const
{
	std::string digits;
	unsigned long long magnitude = bits;
	const bool negative = is_signed(type) && (bits >> 63) != 0;
	if (negative)
	{
		magnitude = 0ULL - bits;
	}
	while (magnitude != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (magnitude % 10)));
		magnitude /= 10;
	}
	if (digits.empty())
	{
		digits = "0";
	}
	return negative ? "-" + digits : digits;
}

SemaAnalyzer::Constant SemaAnalyzer::convert(const Constant& value,
                                             TypeId type) const
{
	Constant out;
	out.type = type;
	if (types_.kind(type) == TypeKind::Fundamental &&
	    types_.fundamental_type(type) == FT_BOOL)
	{
		// 4.12p1: a value converted to `bool` is `false` where it is zero and
		// `true` for every other one, which keeps no low bits at all - so
		// 14.3.2p5 makes `A<3>` and `A<true>` one specialization of
		// `template<bool>` and `int a[(bool)5]` one element.
		out.bits = value.bits != 0 ? 1 : 0;
		return out;
	}
	const unsigned width = width_of(type);
	if (width >= 64)
	{
		out.bits = value.bits;
		return out;
	}
	// 4.7p2 and 4.7p3: a value converted to a narrower type keeps its low
	// bits, and is read back signed or unsigned as the type says.
	const unsigned long long mask = (1ULL << width) - 1;
	unsigned long long bits = value.bits & mask;
	if (is_signed(type) && (bits & (1ULL << (width - 1))) != 0)
	{
		bits |= ~mask;
	}
	out.bits = bits;
	return out;
}

// 4.5: a value of a type whose rank is below `int` is read as an `int`.
SemaAnalyzer::Constant SemaAnalyzer::promote(const Constant& value)
{
	const TypeId arithmetic = arithmetic_type(value.type);
	if (arithmetic == kNoType)
	{
		throw NotConstant("a constant expression has a type that is not "
		                         "integral");
	}
	if (width_of(arithmetic) >= 32)
	{
		Constant out = value;
		out.type = arithmetic;
		return out;
	}
	return convert(value, types_.fundamental(FT_INT));
}

// 5p10: the usual arithmetic conversions over the integral types.
TypeId SemaAnalyzer::common_type(TypeId left, TypeId right)
{
	if (left == right)
	{
		return left;
	}
	const unsigned left_width = width_of(left);
	const unsigned right_width = width_of(right);
	const bool left_signed = is_signed(left);
	const bool right_signed = is_signed(right);
	if (left_signed == right_signed)
	{
		return left_width >= right_width ? left : right;
	}
	const TypeId unsigned_type = left_signed ? right : left;
	const TypeId signed_type = left_signed ? left : right;
	// An unsigned operand at least as wide as the signed one wins; otherwise
	// the signed type holds every value of the unsigned one.
	return width_of(unsigned_type) >= width_of(signed_type) ? unsigned_type
	                                                        : signed_type;
}

SemaAnalyzer::Constant SemaAnalyzer::literal_constant(const std::string& spelling)
{
	// 5.19p2: only an integral literal has a value a constant expression can
	// hold, and which of 2.14p1's literals this one is is the same question the
	// expression layer asks - so it is asked of the same owner, and a character
	// literal that holds a `"` is a character literal here too.
	PostToken token;
	scan_literal(spelling, token);
	if (token.kind != PostTokenKind::Literal ||
	    !fundamental_type_is_integral(token.type))
	{
		throw NotConstant("a constant expression holds a literal that has "
		                         "no integral value");
	}

	Constant out;
	out.type = types_.fundamental(token.type);
	out.bits = integer_of(token.data);
	return convert(out, out.type);
}

// 5.19p2: the lvalue-to-rvalue conversion applied to a subobject of a string
// literal, which is the one object 5.19 reads out of storage rather than out
// of a declaration.  2.14.5 makes the literal an array of the code units phase
// 6 built, so the element is one of them and which one is a constant
// expression of its own.
SemaAnalyzer::Constant SemaAnalyzer::string_element(const std::string& spelling,
                                                    unsigned long long index)
{
	PostToken token;
	if (scan_literal(spelling, token) != LiteralForm::String ||
	    token.kind != PostTokenKind::LiteralArray)
	{
		throw NotConstant("a constant expression subscripts something that is "
		                  "not a string literal");
	}
	// 8.3.4p6: the element is the one the index names, and the terminating
	// null 2.14.5p12 appended is an element of the array like any other.
	if (index >= token.element_count)
	{
		throw NotConstant("a constant expression subscripts a string literal "
		                  "outside its bounds");
	}
	const std::size_t width = fundamental_type_size(token.type);
	const std::size_t at = static_cast<std::size_t>(index) * width;
	Constant out;
	out.type = types_.fundamental(token.type);
	out.bits = 0;
	for (std::size_t byte = width; byte-- > 0; )
	{
		out.bits = (out.bits << 8) |
			static_cast<unsigned char>(token.data[at + byte]);
	}
	return convert(out, out.type);
}




SemaAnalyzer::Constant SemaAnalyzer::evaluate(const AstNode& node,
                                              const Context& ctx)
{
	switch (node.kind)
	{
	case AstKind::Literal:
		return literal_constant(node.text);

	case AstKind::KeywordLiteral:
	{
		if (node.token != KW_TRUE && node.token != KW_FALSE)
		{
			throw NotConstant("a constant expression names no value");
		}
		Constant out;
		out.type = types_.fundamental(FT_BOOL);
		out.bits = node.token == KW_TRUE ? 1 : 0;
		return out;
	}

	case AstKind::ParenthesizedExpression:
		return evaluate(*node.children[0], ctx);

	case AstKind::SubscriptExpression:
	{
		const AstNode* array = node.children[0];
		while (array->kind == AstKind::ParenthesizedExpression)
		{
			array = array->children[0];
		}
		if (array->kind != AstKind::Literal)
		{
			throw NotConstant("a constant expression subscripts something "
			                  "that is not a string literal");
		}
		return string_element(array->text,
		                      evaluate(*node.children[1], ctx).bits);
	}

	case AstKind::IdExpression:
		return ConstexprReading(*this).id_constant(node, ctx);

	case AstKind::UnaryExpression:
		return ConstexprReading(*this).unary_constant(node, ctx);

	case AstKind::BinaryExpression:
		return ConstexprReading(*this).binary_constant(node, ctx);

	case AstKind::ConditionalExpression:
	{
		const bool condition = evaluate(*node.children[0], ctx).bits != 0;
		return evaluate(*node.children[condition ? 1 : 2], ctx);
	}

	case AstKind::CastExpression:
	{
		if (node.children.size() != 2 ||
		    node.children[0]->kind != AstKind::TypeId)
		{
			throw NotConstant("a constant expression holds a cast PA11 "
			                         "does not evaluate");
		}
		const TypeId type = type_id_type(*node.children[0], ctx);
		if (arithmetic_type(type) == kNoType)
		{
			throw NotConstant("a constant expression casts to a type that "
			                         "is not integral");
		}
		const Constant value = evaluate(*node.children[1], ctx);
		Constant out = convert(value, type);
		out.type = type;
		return out;
	}

	case AstKind::CallExpression:
		// 5.2.3p1 and 5.2.2p1: one shape the grammar could not tell
		// apart - a cast written in functional notation, an object of
		// literal class type, and a call of a constexpr function - which
		// `sema_constexpr.h` reads because the last of them walks a body.
		return ConstexprReading(*this).call_or_cast(node, ctx);

	case AstKind::SizeofPackExpression:
	{
		// 5.3.3p5: how many elements the run bound to the pack holds, which is
		// a constant wherever an argument list has settled it.  14.6p8 leaves
		// it unknown where the pattern stands, and the reading stands a value
		// in for it exactly as it does for the size of a dependent type.
		const long long run =
			PackReading(*this).length(node.children[0]->text, ctx);
		Constant out;
		out.type = types_.fundamental(FT_UNSIGNED_LONG_INT);
		if (run < 0)
		{
			++stood_in_;
			out.bits = 1;
			return out;
		}
		out.bits = static_cast<unsigned long long>(run);
		return out;
	}

	case AstKind::SizeofExpression:
	case AstKind::TypeTraitExpression:
	{
		if (node.kind == AstKind::TypeTraitExpression && node.token != KW_ALIGNOF)
		{
			throw NotConstant("a constant expression holds an operator "
			                         "PA11 does not evaluate");
		}
		if (node.children.empty() ||
		    (node.children[0]->kind != AstKind::TypeId &&
		     (node.kind != AstKind::SizeofExpression ||
		      (!semantics() && checking_ == 0))))
		{
			// 5.3.3p1 gives `sizeof` an expression or a parenthesized type-id
			// and 5.3.6p1 gives `alignof` the type-id alone; PA11 types no
			// expression, so the first arm is one only a later dialect reads.
			throw NotConstant("sizeof and alignof are evaluated over a "
			                         "type-id");
		}
		if (node.children[0]->kind != AstKind::TypeId)
		{
			// 5.3.3p1: the operand is an *expression* wherever the grammar
			// allows one, however much a parenthesized name written in it
			// looks like a type-id, and 5.19 asks for its size and not for its
			// value - so the operand is the unevaluated one the expression
			// layer already reads, and this is the same question asked from a
			// constant expression.
			if (checking_ > 0 || dependent_reading(*ctx.scope))
			{
				// 14.6p8: how large the operand is, an argument list is what
				// says, so the reading stands one value in its place and looks
				// up the names 3.4p1 answers where the definition stands.
				check_expression_names(*node.children[0], ctx);
				++stood_in_;
				Constant stood;
				stood.type = types_.fundamental(FT_UNSIGNED_LONG_INT);
				stood.bits = 1;
				return stood;
			}
			DumpNode scratch;
			Constant read;
			read.type = types_.fundamental(FT_UNSIGNED_LONG_INT);
			read.bits = probe_expression(node, ctx, scratch).value;
			return read;
		}
		const TypeId type = type_id_type(*node.children[0], ctx);
		Constant out;
		out.type = types_.fundamental(FT_UNSIGNED_LONG_INT);
		out.bits = node.kind == AstKind::SizeofExpression
			? size_of(type)
			: types_.object_align(type);
		return out;
	}

	default:
		throw NotConstant("a constant expression holds a construct PA11 "
		                         "does not evaluate");
	}
}

unsigned long long SemaAnalyzer::size_of(TypeId type)
{
	if (checking_ > 0 && types_.is_dependent(type))
	{
		// 14.6p8 and 5.3.3p1: how large a type that depends on a template
		// parameter is, an argument list is what says - so a reading of the
		// definition stands one value in its place, and nothing it declares is
		// laid out or written out.
		++stood_in_;
		return 1;
	}
	require_complete_type(type);
	if (types_.is_incomplete(type))
	{
		// 5.3.3p1: `sizeof` shall not be applied to an incomplete type.
		throw std::runtime_error("sizeof names an incomplete type");
	}
	return types_.object_size(type);
}

unsigned long long SemaAnalyzer::array_bound(const AstNode& node,
                                             const Context& ctx)
{
	const Constant value = evaluate(node, ctx);
	if (is_signed(value.type) && (value.bits >> 63) != 0)
	{
		throw std::runtime_error("an array bound is negative");
	}
	if (value.bits == 0)
	{
		if (checking_ > 0)
		{
			// 14.6p8 over 8.3.4p1: how many elements the array has, an argument
			// list is what says wherever the bound was computed from a type
			// that depends on a template parameter - `size_of` stood one value
			// in for that type's size, so the quotient a reading arrives at is
			// no bound at all.  The reading stands one element in its place and
			// the specialization computes the bound its arguments make, which
			// is where 8.3.4p1 is asked.
			return 1;
		}
		// 8.3.4p1: the bound shall be greater than zero.
		throw std::runtime_error("an array bound is zero");
	}
	return value.bits;
}

TypeId SemaAnalyzer::decltype_type(const AstNode& node, const Context& ctx)
{
	const AstNode* expression = node.children[0];
	if (expression->kind == AstKind::CarriedExpression)
	{
		// 7.1.6.2p1: the specifier stands before `::`, so the expression it
		// holds is carried beside the name rather than written under it - the
		// dump describes the name the grammar left spelled, and this is the one
		// thing that spelling cannot hold.
		expression = expression->children[0];
	}
	bool parenthesized = false;
	while (expression->kind == AstKind::ParenthesizedExpression &&
	       !expression->children.empty())
	{
		// 7.1.6.2p4: a parenthesized id-expression is an unparenthesized
		// expression again, and its value category decides the type.
		parenthesized = true;
		expression = expression->children[0];
	}
	if (expression->kind != AstKind::IdExpression)
	{
		if (!semantics() && checking_ == 0)
		{
			throw std::runtime_error("decltype names an expression PA11 does "
			                         "not type");
		}
		if (checking_ > 0 || dependent_reading(*ctx.scope))
		{
			// 14.6.2.2p1: a decltype-specifier over an expression that depends
			// on a template parameter names a type only an instantiation can
			// name - whether it stands in a definition being read for what it
			// says or in the declarator of the template-declaration itself.
			// 3.4p1 still looks up the names the expression writes, and what
			// the specifier stands for until then is a type of its own, which
			// 14.7.1p1 answers by reading the expression again.
			check_expression_names(*expression, ctx);
			return dependent_expression_type(node, ctx);
		}
		// 7.1.6.2p4: for an expression that is not an id-expression, the type
		// is the expression's, with a reference added for an lvalue or an
		// xvalue.  PA12 types every expression of its subset, so the question
		// is the same one the expression layer already answers.
		DumpNode scratch;
		const Value value =
			SemaAnalyzer::probe_expression(*expression, ctx, scratch);
		require_complete_value(value);
		if (!parenthesized && expression->kind == AstKind::MemberExpression &&
		    value.entity != nullptr)
		{
			// 7.1.6.2p4: an unparenthesized class member access names an entity
			// as much as an id-expression does, so what the specifier stands for
			// is the type that entity was *declared* with rather than what the
			// access is worth.  `decltype(c.v)` is the member's own type - not a
			// reference to it, and not qualified by the object holding it - where
			// `decltype((c.v))` is 5.2.5p4's lvalue and gets one; and a member
			// declared of reference type keeps the reference the access took off.
			return value.entity->type;
		}
		if (value.category == ValueCategory::LValue)
		{
			return types_.reference_to(value.type, false);
		}
		if (value.category == ValueCategory::XValue)
		{
			return types_.reference_to(value.type, true);
		}
		return value.type;
	}
	SemaEntity& entity =
		require(resolve(expression->text, ctx, LookupKind::Any), expression->text);
	switch (entity.kind)
	{
	case SemaKind::Variable:
	case SemaKind::Parameter:
	case SemaKind::Function:
		// 3.10p1: an id-expression naming an object or a function is an lvalue.
		return parenthesized ? types_.reference_to(entity.type, false)
		                     : entity.type;

	case SemaKind::Enumerator:
		// 3.10p1: an enumerator is a prvalue, parenthesized or not.
		return entity.type;

	default:
		break;
	}
	throw std::runtime_error("decltype names " + expression->text +
	                         ", which is not an object, function or enumerator");
}
