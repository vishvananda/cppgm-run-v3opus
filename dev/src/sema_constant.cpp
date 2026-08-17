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
	if (types_.is_floating(arithmetic))
	{
		// 3.9.1p8: a floating type has no unsigned counterpart, so every value
		// of one carries a sign - which is what the readings that ask this
		// about the *bits* of an integral value have to be told before they
		// take a floating one for an unsigned number.
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

// 3.9.1p8 and 7.2p9: the arithmetic type a value of `type` is read as, which
// for an enumeration is its underlying type and for anything else is itself.
TypeId SemaAnalyzer::arithmetic_type(TypeId type) const
{
	TypeId current = type;
	if (types_.kind(current) == TypeKind::Enum)
	{
		current = types_.target(current);
	}
	if (types_.kind(current) != TypeKind::Fundamental ||
	    !fundamental_type_is_arithmetic(types_.fundamental_type(current)))
	{
		return kNoType;
	}
	return current;
}

// The same question where the place asks for an integral value rather than for
// any arithmetic one: 14.1p4's non-type template parameter, 4.5's promotion,
// and every reading that goes on to hold what it gets as bits.
TypeId SemaAnalyzer::integral_type(TypeId type) const
{
	const TypeId arithmetic = arithmetic_type(type);
	return arithmetic == kNoType || types_.is_floating(arithmetic) ? kNoType
	                                                               : arithmetic;
}

std::string SemaAnalyzer::spell_value(TypeId type, unsigned long long bits,
                                      long double real) const
{
	const TypeId arithmetic = arithmetic_type(type);
	if (arithmetic != kNoType && types_.is_floating(arithmetic))
	{
		// 2.14.4: a floating value is spelled as the digits it is worth, which
		// is what the reader of this line has to arrive back at it from - the
		// bits beside it are the value of no floating constant.
		return spell_floating_value(types_.fundamental_type(arithmetic), real);
	}
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

SemaAnalyzer::Constant SemaAnalyzer::convert(const Constant& given, TypeId type)
{
	// 5.19p3: a constant that stands for an object of class type holds a list
	// identifier and no number, so what it is worth here is what 12.3.2p1's
	// conversion function hands back.  Every other constant is itself.
	{
		// 4.2p1, 4.3p1 and 4.10p1: the conversions that reach a place of pointer
		// type, which are an address and no arithmetic at all - so they are asked
		// before 5.19p3's, which would read an array designating an object as a
		// value of the object's own width, and refused where the operand is no
		// address.
		Constant pointer;
		if (ConstexprReading(*this).at_pointer_place(given, type, pointer))
		{
			return pointer;
		}
	}
	const Constant value =
		ConstexprReading(*this).at_arithmetic_place(given, arithmetic_type(type));
	const TypeId from = arithmetic_type(value.type);
	const TypeId to = arithmetic_type(type);
	const bool from_real = from != kNoType && types_.is_floating(from);
	if (ConstexprReading(*this).holds_address(value) &&
	    !(types_.kind(type) == TypeKind::Fundamental &&
	      types_.fundamental_type(type) == FT_BOOL))
	{
		// 4.12p1 is the one conversion of a pointer to an arithmetic type there
		// is - whether it designates anything at all - and 3.9.2p1's value is an
		// address, so any other destination reading its bits would read the
		// identifier of the object it designates as a number.
		throw NotConstant("a constant expression converts an address to " +
		                  types_.description(type) +
		                  ", which no conversion of a pointer reaches");
	}
	if (to != kNoType && from == kNoType &&
	    types_.kind(types_.strip_cv(value.type)) == TypeKind::Array)
	{
		// 4.2p1: an array reaches a value place as a pointer to its first
		// element, and 5.19p2 leaves this reading no address at all - so the
		// list identifier the constant holds is a number no conversion of it
		// may read.  `at_arithmetic_place` above has already spent 5.19p3's
		// conversion function on an object of class type; nothing answers here.
		throw NotConstant("a constant expression reads an array where a value "
		                  "of arithmetic type belongs", false);
	}
	if (to == kNoType && types_.strip_cv(value.type) != types_.strip_cv(type))
	{
		// 4.7-4.9 are the conversions between 3.9.1p8's arithmetic types, and a
		// destination that is none of them is 8.5's initialization of an object
		// instead - which `object_of` and `array_of` own and this reading may
		// not stand in for.  Handing back the operand's bits under the object's
		// type would make a number the identifier of the interned list a
		// subobject is read out of, which is the one thing those bits are.
		throw NotConstant("a constant expression converts a value to " +
		                  types_.description(type) +
		                  ", which no conversion between arithmetic types "
		                  "reaches", false);
	}
	Constant out;
	out.type = type;
	if (types_.kind(type) == TypeKind::Fundamental &&
	    types_.fundamental_type(type) == FT_BOOL)
	{
		// 4.12p1: a value converted to `bool` is `false` where it is zero and
		// `true` for every other one, which keeps no low bits at all - so
		// 14.3.2p5 makes `A<3>` and `A<true>` one specialization of
		// `template<bool>` and `int a[(bool)5]` one element.
		out.bits = (from_real ? value.real != 0 : value.bits != 0) ? 1 : 0;
		return out;
	}
	if (to != kNoType && types_.is_floating(to))
	{
		// 4.8p1 and 4.9p2: a floating value converted to a floating type is
		// that value rounded to the new one's precision, and an integral value
		// converted to one is the value it names.  The rounding is the target
		// machine's own, which for these three widths is this machine's.
		const long double held = from_real
			? value.real
			: (is_signed(value.type)
			       ? static_cast<long double>(
			             static_cast<long long>(value.bits))
			       : static_cast<long double>(value.bits));
		switch (types_.fundamental_type(to))
		{
		case FT_FLOAT: out.real = static_cast<float>(held); break;
		case FT_DOUBLE: out.real = static_cast<double>(held); break;
		default: out.real = held; break;
		}
		return out;
	}
	if (from_real)
	{
		// 4.9p1: a floating value converted to an integral type keeps the part
		// before the point, and the conversion stands only where the integral
		// type can represent it - which is what makes an out-of-range one no
		// constant expression rather than a value of its own.
		if (!floating_fits_integral(value.real))
		{
			throw NotConstant("a constant expression converts a floating value "
			                  "no integral type of this translation holds");
		}
		out.bits = integral_of_floating(value.real);
		return convert(out, type);
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
SemaAnalyzer::Constant SemaAnalyzer::promote(const Constant& given)
{
	const Constant value =
		ConstexprReading(*this).at_arithmetic_place(given, kNoType);
	const TypeId arithmetic = arithmetic_type(value.type);
	if (arithmetic == kNoType)
	{
		throw NotConstant("a constant expression has a type that is not "
		                         "arithmetic", false);
	}
	if (types_.is_floating(arithmetic) || width_of(arithmetic) >= 32)
	{
		// 4.5 promotes no floating type: 5p10 is what brings a `float` to the
		// width of the other operand, and it is asked where an operator has
		// two of them rather than here.
		Constant out = value;
		out.type = arithmetic;
		return out;
	}
	return convert(value, types_.fundamental(FT_INT));
}

// 5p10: the usual arithmetic conversions.  A floating operand decides for both,
// at the widest of the floating types either of them is; otherwise the two are
// brought to one integral type by the rank and signedness rules below.
TypeId SemaAnalyzer::common_type(TypeId left, TypeId right)
{
	if (left == right)
	{
		return left;
	}
	const TypeId left_arithmetic = arithmetic_type(left);
	const TypeId right_arithmetic = arithmetic_type(right);
	const bool left_real =
		left_arithmetic != kNoType && types_.is_floating(left_arithmetic);
	const bool right_real =
		right_arithmetic != kNoType && types_.is_floating(right_arithmetic);
	if (left_real || right_real)
	{
		if (!left_real)
		{
			return right;
		}
		if (!right_real)
		{
			return left;
		}
		return width_of(left) >= width_of(right) ? left : right;
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
	// 5.19p2: which of 2.14p1's literals this one is is the same question the
	// expression layer asks - so it is asked of the same owner, and a character
	// literal that holds a `"` is a character literal here too.  A string
	// literal is the one whose value is an array rather than a number, and
	// 5.19p2 reaches its elements through a subscript and not through this.
	PostToken token;
	scan_literal(spelling, token);
	if (token.kind != PostTokenKind::Literal ||
	    !fundamental_type_is_arithmetic(token.type))
	{
		throw NotConstant("a constant expression holds a literal that has "
		                         "no arithmetic value", false);
	}

	Constant out;
	out.type = types_.fundamental(token.type);
	if (fundamental_type_is_floating(token.type))
	{
		// 2.14.4p1: the value is the one the literal was scanned to, already
		// held at the width its floating-suffix gave it.
		out.real = token.real_value();
		return out;
	}
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
		                  "not a string literal", false);
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
	{
		// 2.14.5p8: a string literal is an *object* of static storage duration
		// and no value at all, which is what a name of an array is - so it is
		// read as one, and 5.19p2's address constant and 5.2.1p1's element are
		// each the reading of that object they are anywhere else.
		Constant object;
		if (ConstexprReading(*this).literal_object(node.text, object))
		{
			return object;
		}
		return literal_constant(node.text);
	}

	case AstKind::KeywordLiteral:
	{
		if (node.token == KW_THIS)
		{
			// 9.3.2p1: `this` is no literal - the grammar reads it where a
			// primary-expression is written, and its value is the address of
			// the object the member function was called on, which is the object
			// the fold bound the call to.
			return ConstexprReading(*this).this_constant(ctx);
		}
		if (node.token == KW_NULLPTR)
		{
			// 2.14.7p1 and 4.10p1: `nullptr` is the null pointer value, of type
			// `std::nullptr_t`, which designates no object.
			Constant out;
			out.type = types_.fundamental(FT_NULLPTR_T);
			return out;
		}
		if (node.token != KW_TRUE && node.token != KW_FALSE)
		{
			throw NotConstant("a constant expression names no value", false);
		}
		Constant out;
		out.type = types_.fundamental(FT_BOOL);
		out.bits = node.token == KW_TRUE ? 1 : 0;
		return out;
	}

	case AstKind::ParenthesizedExpression:
		return evaluate(*node.children[0], ctx);

	case AstKind::SubscriptExpression:
		return ConstexprReading(*this).subscript_constant(node, ctx);

	case AstKind::IdExpression:
		return ConstexprReading(*this).id_constant(node, ctx);

	case AstKind::UnaryExpression:
		return ConstexprReading(*this).unary_constant(node, ctx);

	case AstKind::BinaryExpression:
		return ConstexprReading(*this).binary_constant(node, ctx);

	case AstKind::AssignmentExpression:
		// 5.17p1: a write-back to an object the evaluation itself declared,
		// which the statements of a constexpr body are what reach.
		return ConstexprReading(*this).assignment_constant(node, ctx);

	case AstKind::PostfixExpression:
		if (node.token != OP_INC && node.token != OP_DEC)
		{
			throw NotConstant("a constant expression holds an operator this "
			                  "implementation does not evaluate", false);
		}
		return ConstexprReading(*this).increment_constant(node, ctx, false);

	case AstKind::ConditionalExpression:
	{
		// 5.16p1: the first operand is contextually converted to `bool`, which
		// is 4p3's reading and not a look at the operand's bits.
		const bool condition =
			ConstexprReading(*this).truth(evaluate(*node.children[0], ctx));
		return evaluate(*node.children[condition ? 1 : 2], ctx);
	}

	case AstKind::CastExpression:
		return ConstexprReading(*this).cast_constant(node, ctx);

	case AstKind::CallExpression:
		// 5.2.3p1 and 5.2.2p1: one shape the grammar could not tell
		// apart - a cast written in functional notation, an object of
		// literal class type, and a call of a constexpr function - which
		// `sema_constexpr.h` reads because the last of them walks a body.
		return ConstexprReading(*this).call_or_cast(node, ctx);

	case AstKind::MemberExpression:
		// 5.2.5p1: a member of the object 5.2.3p2/p3 built, which is the one
		// object a constant expression here holds - so it is read where that
		// object is owned and not here.
		return ConstexprReading(*this).member_constant(node, ctx);

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
		if (node.kind == AstKind::TypeTraitExpression &&
		    node.token == KW_NOEXCEPT && !node.children.empty())
		{
			// 5.3.7p2: a constant of type `bool`, which is the same reading the
			// expression layer takes its line from - so a `static_assert` over
			// the operator and a template argument written with it get the one
			// answer 15.4 settled and not a second reading of it.
			Constant answer;
			answer.type = types_.fundamental(FT_BOOL);
			answer.bits =
				ConstexprReading(*this).nonthrowing_operand(*node.children[0],
				                                            ctx)
					? 1 : 0;
			return answer;
		}
		if (node.kind == AstKind::TypeTraitExpression && node.token != KW_ALIGNOF)
		{
			throw NotConstant("a constant expression holds an operator "
			                         "PA11 does not evaluate", false);
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
		const TypeId type = types_.measured_type(type_id_type(*node.children[0], ctx));
		Constant out;
		out.type = types_.fundamental(FT_UNSIGNED_LONG_INT);
		out.bits = node.kind == AstKind::SizeofExpression
			? size_of(type)
			: align_of(type);
		return out;
	}

	default:
		throw NotConstant("a constant expression holds a construct PA11 "
		                         "does not evaluate", false);
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
	// The demand is `require_settled_type`'s and not `require_complete_type`'s,
	// because a reading that asked for nothing is what writes an operand here:
	// 14.6p8's reading of a template definition names a specialization its own
	// arguments already settled, and `sema_value_expression.cpp` probes a
	// type-id in a reading of its own and keeps the answer.  Neither left the
	// mark the narrower demand reads, and the clause requires the type complete
	// however the reading that named it stood.
	require_settled_type(type);
	if (types_.is_incomplete(type))
	{
		// 5.3.3p1: `sizeof` shall not be applied to an incomplete type.
		throw std::runtime_error("sizeof names an incomplete type");
	}
	return types_.object_size(type);
}

// 5.3.6p1 and p3 as `size_of`'s twin, asked in the same order and for the same
// reason: the clause writes the same two sentences 5.3.3p1 does about an
// operand an argument list has yet to settle and about one whose type shall be
// complete, and `TypeTable::object_align` answers neither - an incomplete class
// has an alignment of zero there, which is a number a reader would write out.
// Three readings write this operator - the expression layer, 5.19's tree
// reading and its spelling reading - and a clause one of them holds is a clause
// the other two are wrong about.
unsigned long long SemaAnalyzer::align_of(TypeId type)
{
	if (checking_ > 0 && types_.is_dependent(type))
	{
		// 14.6p8 and 5.3.6p1: how a type that depends on a template parameter
		// is aligned, an argument list is what says.
		++stood_in_;
		return 1;
	}
	require_settled_type(type);
	if (types_.is_incomplete(type))
	{
		// 5.3.6p3: the type shall be complete, which is what has an alignment.
		throw std::runtime_error("alignof names an incomplete type");
	}
	return types_.object_align(type);
}

unsigned long long SemaAnalyzer::array_bound(const AstNode& node,
                                             const Context& ctx)
{
	// 8.3.4p1: the bound is a converted constant expression of type
	// `std::size_t`, which 5.19p3 leaves its user-defined conversions - and
	// which counts elements, so a floating value is no bound at all.
	Constant value;
	unsigned long long bound = 0;
	if (!ConstexprReading(*this).counted_where(node, ctx, value, bound))
	{
		// 14.6p8 over 8.3.4p1: the bound names something an argument list has
		// yet to settle, so what the reading came out as - a value, or running
		// out on the stand-in - is no bound at all.  `char check[sizeof(T) == 4
		// ? 1 : -1]` is the pattern's whole point, and the arm a stood-in value
		// chose says nothing about the specialization.  One element stands in
		// until the instantiation reads the bound its arguments make, which is
		// where 8.3.4p1 is asked.
		return 1;
	}
	if (is_signed(value.type) && (bound >> 63) != 0)
	{
		throw std::runtime_error("an array bound is negative");
	}
	if (bound == 0)
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
		//
		// 7.1.6.2p4 leaves that expression unevaluated, which is the same door
		// 5.3.3p1 opens for `sizeof`: 5.1.1p13's third bullet lets an
		// id-expression name a non-static data member with no object there, so
		// `decltype(S::keys + 0)` is read where `decltype(S::keys)` above is.
		const ReadingDepth measuring(unevaluated_);
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
	SemaEntity* const found = resolve(expression->text, ctx, LookupKind::Any);
	if (found != nullptr && types_.dependent_owner(found->type) != kNoType)
	{
		// 14.6.2.2p1 at the other kind of operand: the id-expression is a
		// member of a class no argument list has named yet, so what it *names*
		// is a question this reading cannot ask - 3.10p1 has no answer for a
		// name that may turn out to be an object, a function, an enumerator or
		// a type at all.  The specifier stands for a type of its own until
		// 14.7.1p1 reads the same id-expression against the class the arguments
		// made, which is the one door every dependent operand comes back
		// through.
		return dependent_expression_type(node, ctx);
	}
	SemaEntity& entity = require(found, expression->text);
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
