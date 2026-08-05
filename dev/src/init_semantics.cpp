#include "init_semantics.h"

namespace
{

// An integral value held the way `ConstValue` holds one: truncated to the
// width of its type and then extended to 64 bits by its signedness, so that
// the next conversion only has to do the same again.
unsigned long long normalize(EFundamentalType type, unsigned long long value)
{
	const std::size_t bits = fundamental_type_size(type) * 8;
	if (bits >= 64)
	{
		return value;
	}
	const unsigned long long mask = (1ULL << bits) - 1;
	value &= mask;
	if (fundamental_type_is_signed(type) && (value >> (bits - 1)) != 0)
	{
		value |= ~mask;
	}
	return value;
}

// 8.5.2p1: which array element types a string literal of `given` may
// initialize.  A narrow literal initializes any of the three character types;
// every other literal initializes only its own.
bool initializes_array(EFundamentalType element, EFundamentalType given)
{
	if (given == FT_CHAR)
	{
		return element == FT_CHAR || element == FT_SIGNED_CHAR ||
			element == FT_UNSIGNED_CHAR;
	}
	return element == given;
}

}

InitSemantics::InitSemantics(TypeTable& types, ProgramImage& image)
	: types_(types)
	, image_(image)
{}

bool InitSemantics::is_arithmetic(TypeId type) const
{
	if (types_.kind(type) != TypeKind::Fundamental)
	{
		return false;
	}
	return fundamental_type_class(types_.fundamental_type(type)) !=
		FundamentalTypeClass::NonArithmetic;
}

ConstValue InitSemantics::read_object(const ExprValue& source)
{
	// 5.19p2: only an object the translation is allowed to read, and only when
	// the lvalue designates the whole of it.
	if (source.value.kind != ValueKind::Address || source.value.addend != 0)
	{
		return ConstValue();
	}
	const Symbol& symbol = image_.at(source.value.symbol);
	if (!symbol.readable || !symbol.initialized || symbol.from_string)
	{
		return ConstValue();
	}
	return symbol.value;
}

ExprValue InitSemantics::to_prvalue(const ExprValue& source)
{
	if (!source.glvalue())
	{
		return source;
	}

	ExprValue out = source;
	out.category = ValueCategory::PRValue;
	out.null_constant = false;
	switch (types_.kind(source.type))
	{
	case TypeKind::Array:
		// 4.2: the value is the address of the first element, which is the
		// address the lvalue already carries.
		out.type = types_.pointer_to(types_.target(source.type));
		return out;

	case TypeKind::Function:
		// 4.3.
		out.type = types_.pointer_to(source.type);
		return out;

	default:
		break;
	}

	// 4.1.
	out.type = types_.strip_cv(source.type);
	out.value = read_object(source);
	out.string_literal = false;
	return out;
}

bool InitSemantics::converts_pointer(TypeId destination, TypeId from)
{
	if (types_.kind(destination) != TypeKind::Pointer ||
	    types_.kind(from) != TypeKind::Pointer)
	{
		return false;
	}

	// 4.4p4: at every level the source's qualifiers must be among the
	// destination's, and once a level differs every level nearer the top must
	// be const.  4.10p2 lets a pointer to any object type reach `void*` at any
	// level the rest of the chain agrees on.
	bool const_above = true;
	for (;;)
	{
		const TypeId reached = types_.target(destination);
		const TypeId given = types_.target(from);
		const unsigned wanted_cv = types_.cv(reached);
		const unsigned given_cv = types_.cv(given);
		if ((given_cv & ~wanted_cv) != 0)
		{
			return false;
		}
		if (given_cv != wanted_cv && !const_above)
		{
			return false;
		}
		if (types_.is_void(reached))
		{
			return types_.kind(given) != TypeKind::Function;
		}
		if (types_.strip_cv(reached) == types_.strip_cv(given))
		{
			return true;
		}
		if (types_.kind(reached) != TypeKind::Pointer ||
		    types_.kind(given) != TypeKind::Pointer)
		{
			return false;
		}
		const_above = const_above && (wanted_cv & kCvConst) != 0;
		destination = reached;
		from = given;
	}
}

ConstValue InitSemantics::convert_pointer(TypeId destination, const ExprValue& source)
{
	// 4.10p1: a null pointer constant becomes the null pointer value of the
	// destination type, whatever the two types are.
	const TypeId from = types_.strip_cv(source.type);
	if (source.null_constant || (types_.kind(from) == TypeKind::Fundamental &&
	                             types_.fundamental_type(from) == FT_NULLPTR_T))
	{
		return ConstValue::null();
	}
	if (!converts_pointer(destination, from))
	{
		throw SemanticError("an initializer does not convert to the pointer declared");
	}
	return source.value;
}

ConstValue InitSemantics::convert_arithmetic(TypeId destination, const ExprValue& source)
{
	const EFundamentalType wanted = types_.fundamental_type(destination);
	if (!source.value.known())
	{
		return ConstValue();
	}
	const EFundamentalType given = types_.fundamental_type(types_.strip_cv(source.type));

	long double real = source.value.floating;
	if (source.value.kind == ValueKind::Integral)
	{
		real = fundamental_type_is_signed(given)
			? static_cast<long double>(static_cast<long long>(source.value.integral))
			: static_cast<long double>(source.value.integral);
	}

	// 4.6 and 4.8: a floating value takes the precision of its new type.
	if (fundamental_type_class(wanted) == FundamentalTypeClass::Floating)
	{
		if (wanted == FT_FLOAT)
		{
			return ConstValue::real(static_cast<float>(real));
		}
		if (wanted == FT_DOUBLE)
		{
			return ConstValue::real(static_cast<double>(real));
		}
		return ConstValue::real(real);
	}

	// 4.9p1: a floating value converted to an integral type is truncated.
	unsigned long long raw = source.value.integral;
	if (source.value.kind == ValueKind::Floating)
	{
		raw = fundamental_type_is_signed(wanted)
			? static_cast<unsigned long long>(static_cast<long long>(real))
			: static_cast<unsigned long long>(real);
	}
	return ConstValue::integer(normalize(wanted, raw));
}

ConstValue InitSemantics::convert(TypeId destination, const ExprValue& source)
{
	const TypeId from = types_.strip_cv(source.type);
	if (from == destination)
	{
		return source.value;
	}
	if (types_.kind(destination) == TypeKind::Pointer)
	{
		return convert_pointer(destination, source);
	}
	if (types_.kind(destination) != TypeKind::Fundamental)
	{
		throw SemanticError("an initializer does not convert to the type declared");
	}

	const EFundamentalType wanted = types_.fundamental_type(destination);
	if (wanted == FT_NULLPTR_T || wanted == FT_VOID)
	{
		throw SemanticError("an initializer does not convert to the type declared");
	}

	// 4.12: every scalar type reaches bool, and a value reaches it as "is it
	// something other than zero".
	if (wanted == FT_BOOL)
	{
		const bool scalar = is_arithmetic(from) || types_.kind(from) == TypeKind::Pointer ||
			(types_.kind(from) == TypeKind::Fundamental &&
			 types_.fundamental_type(from) == FT_NULLPTR_T);
		if (!scalar)
		{
			throw SemanticError("an initializer does not convert to bool");
		}
		switch (source.value.kind)
		{
		case ValueKind::Integral:
			return ConstValue::integer(source.value.integral != 0 ? 1 : 0);
		case ValueKind::Floating:
			return ConstValue::integer(source.value.floating != 0 ? 1 : 0);
		case ValueKind::Address:
			return ConstValue::integer(1);
		case ValueKind::Null:
			return ConstValue::integer(0);
		default:
			return ConstValue();
		}
	}

	if (!is_arithmetic(from))
	{
		throw SemanticError("an initializer does not convert to the arithmetic type declared");
	}
	return convert_arithmetic(destination, source);
}

Initialization InitSemantics::initialize_scalar(TypeId destination, const ExprValue& source)
{
	Initialization result;
	result.type = destination;
	const ExprValue value = to_prvalue(source);
	result.value = convert(types_.strip_cv(destination), value);
	result.constant = result.value.known();
	return result;
}

Initialization InitSemantics::initialize_array(TypeId destination, const ExprValue& source)
{
	Initialization result;
	result.type = destination;
	if (!source.string_literal || source.value.kind != ValueKind::Address)
	{
		throw SemanticError("an array is initialized by something other than a string literal");
	}

	const Symbol& literal = image_.at(source.value.symbol);
	const TypeId element = types_.target(destination);
	const TypeId bare = types_.strip_cv(element);
	const TypeId given = types_.strip_cv(types_.target(literal.type));
	if (types_.kind(bare) != TypeKind::Fundamental ||
	    !initializes_array(types_.fundamental_type(bare), types_.fundamental_type(given)))
	{
		throw SemanticError("a string literal initializes an array of another type");
	}

	const std::size_t width = fundamental_type_size(types_.fundamental_type(given));
	const unsigned long long elements = literal.bytes.size() / width;
	if (!types_.bounded(destination))
	{
		// 8.5.2p1: an array of unknown bound takes the length of the literal,
		// counting its terminating null character.
		result.type = types_.array_of(element, true, elements);
	}
	else if (types_.bound(destination) < elements)
	{
		throw SemanticError("a string literal is longer than the array it initializes");
	}
	result.bytes = literal.bytes;
	result.from_string = true;
	result.constant = true;
	return result;
}

Initialization InitSemantics::bind_reference(TypeId destination, const ExprValue& source)
{
	Initialization result;
	result.type = destination;
	const TypeId referent = types_.target(destination);
	const bool rvalue_reference = types_.kind(destination) == TypeKind::RValueReference;
	const bool related = types_.strip_cv(referent) == types_.strip_cv(source.type);

	if (source.glvalue() && related)
	{
		// 8.5.3p5: an rvalue reference shall not bind to an lvalue of a
		// reference-related type, and a reference binds directly only when it
		// keeps every qualifier the object already has.
		if (rvalue_reference && types_.kind(source.type) != TypeKind::Function)
		{
			throw SemanticError("an rvalue reference is bound to an lvalue");
		}
		if ((types_.cv(source.type) & ~types_.cv(referent)) != 0)
		{
			throw SemanticError("a reference drops a qualifier of what it binds to");
		}
		result.value = source.value;
		result.constant = source.value.known();
		return result;
	}

	// 8.5.3p5: otherwise the reference binds to a temporary, so it has to be
	// an rvalue reference or an lvalue reference to a non-volatile const type.
	if (!rvalue_reference && ((types_.cv(referent) & kCvConst) == 0 ||
	                          (types_.cv(referent) & kCvVolatile) != 0))
	{
		throw SemanticError("a reference to a type that is not const binds to a temporary");
	}
	if (types_.kind(referent) == TypeKind::Array ||
	    types_.kind(referent) == TypeKind::Function)
	{
		throw SemanticError("a reference does not bind to the initializer given");
	}

	const Initialization value = initialize_scalar(referent, source);
	Symbol& temporary = image_.add_temporary(referent);
	temporary.value = value.value;
	temporary.initialized = value.constant;
	temporary.defined = true;
	result.value = ConstValue::address(temporary.id, 0);
	result.constant = true;
	return result;
}

Initialization InitSemantics::initialize(TypeId destination, const ExprValue& source)
{
	if (types_.is_reference(destination))
	{
		return bind_reference(destination, source);
	}
	if (types_.kind(destination) == TypeKind::Array)
	{
		return initialize_array(destination, source);
	}
	return initialize_scalar(destination, source);
}

bool InitSemantics::to_bool(const ExprValue& source)
{
	const ExprValue value = to_prvalue(source);
	const TypeId type = types_.strip_cv(value.type);
	const bool scalar = is_arithmetic(type) || types_.kind(type) == TypeKind::Pointer ||
		(types_.kind(type) == TypeKind::Fundamental &&
		 types_.fundamental_type(type) == FT_NULLPTR_T);
	if (!scalar)
	{
		throw SemanticError("a condition is not of a type that converts to bool");
	}

	switch (value.value.kind)
	{
	case ValueKind::Integral:
		return value.value.integral != 0;
	case ValueKind::Floating:
		return value.value.floating != 0;
	case ValueKind::Address:
		// The address of an object of the program is never a null pointer.
		return true;
	case ValueKind::Null:
		return false;
	default:
		throw SemanticError("a condition is not a constant expression");
	}
}

unsigned long long InitSemantics::to_array_bound(const ExprValue& source)
{
	const ExprValue value = to_prvalue(source);
	const TypeId type = types_.strip_cv(value.type);
	if (!is_arithmetic(type) || !fundamental_type_is_integral(types_.fundamental_type(type)))
	{
		throw SemanticError("an array bound is not of an integral type");
	}
	if (value.value.kind != ValueKind::Integral)
	{
		throw SemanticError("an array bound is not a constant expression");
	}

	// 8.3.4p1: the bound is a converted constant expression of type
	// `std::size_t` whose value is greater than zero.
	const unsigned long long bound = value.value.integral;
	if (bound == 0 || (fundamental_type_is_signed(types_.fundamental_type(type)) &&
	                   static_cast<long long>(bound) < 0))
	{
		throw SemanticError("an array bound is not greater than zero");
	}
	return bound;
}
