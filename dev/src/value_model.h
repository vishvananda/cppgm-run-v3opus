#pragma once

#include <cstdint>

#include "type_model.h"

// What a translation knows about a value.
//
// 5.19 lets a value be completely known during translation, known only as a
// place in the program image, or not known at all.  A pointer to an object is
// the middle case: which object it points at is decided while parsing, but the
// address it holds is only decided once the image is laid out, so it is kept as
// the object and a byte offset into it until then.  A value that is not known
// makes the expression it belongs to not a constant expression, and leaves the
// object it initializes zero initialized (3.6.2).
enum class ValueKind
{
	Unknown,
	Integral,
	Floating,
	Address,
	Null
};

struct ConstValue
{
	ConstValue()
		: kind(ValueKind::Unknown)
		, integral(0)
		, floating(0)
		, symbol(0)
		, addend(0)
	{}

	static ConstValue integer(unsigned long long value)
	{
		ConstValue result;
		result.kind = ValueKind::Integral;
		result.integral = value;
		return result;
	}

	static ConstValue real(long double value)
	{
		ConstValue result;
		result.kind = ValueKind::Floating;
		result.floating = value;
		return result;
	}

	static ConstValue address(std::uint32_t symbol, unsigned long long addend)
	{
		ConstValue result;
		result.kind = ValueKind::Address;
		result.symbol = symbol;
		result.addend = addend;
		return result;
	}

	static ConstValue null()
	{
		ConstValue result;
		result.kind = ValueKind::Null;
		return result;
	}

	bool known() const { return kind != ValueKind::Unknown; }

	ValueKind kind;
	// Integral: the value, sign extended to 64 bits when its type is signed
	// and zero extended when it is not, so that a later conversion only has to
	// truncate and extend again.
	unsigned long long integral;
	long double floating;       // Floating
	std::uint32_t symbol;       // Address: the object pointed into
	unsigned long long addend;  // Address: the byte offset within it
};

// The value categories of 3.10.  PA8 cannot reach an xvalue, but every category
// is accounted for because a later assignment can.
enum class ValueCategory
{
	LValue,
	XValue,
	PRValue
};

// One analysed expression: 3.10 its type and category, 5.19 what is known of
// its value, and the two properties 4.10 and 8.5.2 ask about a source that its
// type alone does not answer.
//
// `type` never names a reference: 3.10p2 makes an id-expression that names a
// reference an lvalue of the referenced type, so a reference is dereferenced
// where the expression is analysed rather than at every use of it.  For a
// glvalue `value` is the address of the object designated, which is what an
// lvalue-to-rvalue conversion reads and what a reference binds to.
struct ExprValue
{
	ExprValue()
		: type(kNoType)
		, category(ValueCategory::PRValue)
		, null_constant(false)
		, string_literal(false)
	{}

	bool glvalue() const { return category != ValueCategory::PRValue; }

	TypeId type;
	ValueCategory category;
	ConstValue value;
	bool null_constant;  // 4.10p1: an integer literal zero, or `nullptr`
	bool string_literal;
};
