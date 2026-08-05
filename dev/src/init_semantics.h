#pragma once

#include <string>

#include "program_model.h"
#include "type_model.h"
#include "value_model.h"

// What copy-initializing one object means: which conversions apply, whether
// they are allowed, and what the object then holds.
struct Initialization
{
	Initialization()
		: type(kNoType)
		, from_string(false)
		, constant(false)
	{}

	// The destination type, with an array bound 8.5.2p2 took from the
	// initializing string literal.
	TypeId type;
	ConstValue value;   // scalar and reference objects
	std::string bytes;  // 8.5.2 character arrays
	bool from_string;
	// 3.6.2: the initializer is a constant expression, so the object is
	// constant initialized rather than zero initialized.
	bool constant;
};

// The standard conversions of clause 4, the initialization of 8.5, and the
// constant evaluation of 5.19.
//
// One place answers "may this initialization happen, and what does the object
// then hold", because every caller - a variable's initializer, an array bound,
// a `static_assert` - asks the same question of the same rules and differs only
// in what it does with the answer.  Initialization creates objects: 8.5.3p5
// binds a reference to a temporary that the program image has to hold, which is
// why this reaches the image rather than only the type table.
class InitSemantics
{
public:
	InitSemantics(TypeTable& types, ProgramImage& image);

	// 8.5: copy-initialization of an object of type `destination`.  Throws
	// SemanticError when no conversion sequence exists.
	Initialization initialize(TypeId destination, const ExprValue& source);

	// 4p1: the prvalue an expression gives up, after the lvalue-to-rvalue,
	// array-to-pointer and function-to-pointer conversions of 4.1 to 4.3.
	ExprValue to_prvalue(const ExprValue& source);

	// 5.19 and 4.12: an expression contextually converted to bool, which
	// `static_assert` needs to be a constant expression.
	bool to_bool(const ExprValue& source);

	// 8.3.4p1: an array bound, which is a converted constant expression of
	// type `std::size_t` whose value is greater than zero.
	unsigned long long to_array_bound(const ExprValue& source);

private:
	Initialization bind_reference(TypeId destination, const ExprValue& source);
	Initialization initialize_array(TypeId destination, const ExprValue& source);
	Initialization initialize_scalar(TypeId destination, const ExprValue& source);

	// 5.19p2: the value an lvalue-to-rvalue conversion reads from the object
	// `source` designates, or nothing when the object may not be read during
	// translation.
	ConstValue read_object(const ExprValue& source);
	// Clause 4: `source`, already a prvalue, as a value of type `destination`.
	ConstValue convert(TypeId destination, const ExprValue& source);
	ConstValue convert_arithmetic(TypeId destination, const ExprValue& source);
	ConstValue convert_pointer(TypeId destination, const ExprValue& source);
	// 4.4 and 4.10p2: whether a pointer prvalue of type `from` converts to
	// `destination`.
	bool converts_pointer(TypeId destination, TypeId from);
	bool is_arithmetic(TypeId type) const;

	TypeTable& types_;
	ProgramImage& image_;
};
