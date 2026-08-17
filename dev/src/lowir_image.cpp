#include "lowir_lower.h"

#include <sstream>

#include "lowir_abi.h"
#include "sema_scope.h"
#include "token_model.h"

// 3.6.2: what an object with static storage duration holds before the program
// starts.
//
// One owner for the whole of it: the fold of an initializer written outside
// every body, the items a structured image is laid out as, and the constant the
// analysis already folded the object to.  Nothing here writes an instruction -
// 3.6.2p2's other half, the initialization the program *runs*, belongs to
// `lowir_lower.cpp` beside the definition that asked for it.

namespace {

using lowir_model::LowType;

// 4.8p1: `value` as an object of `type` holds it, which for the narrower of
// 3.9.1p8's floating types is a rounding of it.  A fold over a chain of
// conversions has to round at each of them, because 4.8's conversions do not
// compose into the widest one: `(double)(float)16777217.0` is not `16777217.0`.
long double held_at(TypeTable& types, TypeId type, long double value)
{
	const TypeId bare = types.strip_cv(type);
	if (types.kind(bare) != TypeKind::Fundamental)
	{
		return value;
	}
	switch (types.fundamental_type(bare))
	{
	case FT_FLOAT: return static_cast<float>(value);
	case FT_DOUBLE: return static_cast<double>(value);
	default: return value;
	}
}

LowType low(const std::string& text)
{
	LowType type;
	type.text = text;
	return type;
}

}  // namespace

// 3.9.1p1 and 4.7p2: the bits an object of `type` holds when it is given
// `bits`, which is all of them at its own width and nothing above it.
unsigned long long LowirUnitLowering::narrowed(TypeId type,
                                               unsigned long long bits)
{
	const TypeId bare = types_.strip_cv(type);
	if (types_.kind(bare) == TypeKind::Fundamental &&
	    types_.fundamental_type(bare) == FT_BOOL)
	{
		// 4.12p1: a value converted to `bool` is `false` where it is zero and
		// `true` for every other one, which keeps no low bits at all - so the
		// storage holds one and not whatever the source's low byte was.
		return bits != 0 ? 1 : 0;
	}
	const unsigned long long size = width(type);
	if (size == 0 || size >= 8)
	{
		return bits;
	}
	const unsigned shift = static_cast<unsigned>(64 - 8 * size);
	return is_signed(type)
		? static_cast<unsigned long long>(
			  static_cast<long long>(bits << shift) >> shift)
		: (bits << shift) >> shift;
}

std::string LowirUnitLowering::spell_value(TypeId type,
                                           unsigned long long bits)
{
	std::ostringstream text;
	const unsigned long long value = narrowed(type, bits);
	if (is_signed(type))
	{
		text << static_cast<long long>(value);
	}
	else
	{
		text << value;
	}
	return text.str();
}

// 2.14.4 and `lowir.md`: one floating value spelled at the width of the object
// that holds it.  The digits are the ones the program wrote - a floating value
// is not one this translation computes with - and the suffix is the one the
// storage asks for, which is what says at which width those bytes are laid
// down.  A spelling that carries the suffix of another width would be a value
// of that width, so the written one is dropped before this one is added.
std::string LowirUnitLowering::spell_floating(TypeId type,
                                              const std::string& written)
{
	std::string digits = written;
	if (digits.size() > 1)
	{
		// 2.14.4p1: a floating-literal carries at most one floating-suffix, and
		// it follows the digit-sequence or the `.` that ends the literal
		// itself.  What stands here is as often a value this lowering spelled
		// as a literal the program wrote, and a value 3.9.1p8 leaves outside
		// the finite ones is spelled `inf` - whose `f` follows a letter and is
		// no suffix at all.
		const char last = digits[digits.size() - 1];
		const char before = digits[digits.size() - 2];
		if ((last == 'f' || last == 'F' || last == 'l' || last == 'L') &&
		    ((before >= '0' && before <= '9') || before == '.'))
		{
			digits.erase(digits.size() - 1);
		}
	}
	if (digits.empty())
	{
		digits = "0";
	}
	const std::string low = low_type(type).text;
	return low == "f32" ? digits + "f" : low == "f80" ? digits + "L" : digits;
}

// 2.14.4: the digits of the floating constant this initializer is worth, which
// the analysis kept from what the program wrote because no integer of the
// translation holds one.  An integer constant written for a floating object is
// the one value that reaches here through the fold.  False for anything else,
// which leaves 3.6.2p2's dynamic initialization to write it.
bool LowirUnitLowering::floating_image(const DumpNode& node, std::string& text)
{
	const SemaFact& fact = node.fact;
	if (fact.kind == FactKind::Literal && !fact.spelling.empty())
	{
		text = fact.spelling;
		return true;
	}
	if (node.children.size() == 1 &&
	    (fact.kind == FactKind::Cast || fact.kind == FactKind::BracedInitList))
	{
		// 5.19 over 4.7p2 and 8.5.4: what the cast, and what the one clause of
		// a braced-init-list, is worth is what stands under it.
		return floating_image(*node.children[0], text);
	}
	if (fact.kind == FactKind::BracedInitList && node.children.empty())
	{
		// 8.5.4p3: `{}` value-initializes the object, which for a floating type
		// is its zero.
		text = "0";
		return true;
	}
	if (fact.kind == FactKind::Unary && node.children.size() == 1 &&
	    (fact.op == OP_PLUS || fact.op == OP_MINUS))
	{
		std::string operand;
		if (!floating_image(*node.children[0], operand))
		{
			return false;
		}
		text = fact.op == OP_MINUS ? "-" + operand : operand;
		return true;
	}
	unsigned long long bits = 0;
	if (!folded(node, bits))
	{
		return false;
	}
	// 4.9p2: an integer constant initializing an object of floating type is
	// converted to it, and the value it converts to is the one it names.
	text = spell_value(fact.type, bits);
	return true;
}

// 3.6.2p2 over an object of floating type whose storage holds one value rather
// than a run of items: what that object holds, as the value itself.
//
// The digits above are what the program wrote for one *clause*, and a clause is
// all an aggregate's items are described by.  A scalar object is different: its
// initializer is a full expression, so what the storage holds is what that
// expression came to - `constexpr float v = 16777217.0;` holds the `float`
// nearest those digits and not the `double` they name, and `constexpr double g
// = third(9.0);` holds a value no part of the program ever spelled.  So this is
// the fold, and 5.19 already worked out every operand it stands on.
bool LowirUnitLowering::folded_real(const DumpNode& node, long double& value)
{
	const SemaFact& fact = node.fact;
	const bool real =
		fact.type != kNoType && types_.is_floating(types_.strip_cv(fact.type));
	if (real && fact.constant)
	{
		// A floating literal, and a call the analysis folded: each carries what
		// it is worth beside the line that spells it.
		value = fact.real;
		return true;
	}
	if (real && fact.kind == FactKind::Id && fact.entity != nullptr &&
	    fact.entity->constant)
	{
		value = fact.entity->real;
		return true;
	}
	if (node.children.size() == 1 &&
	    (fact.kind == FactKind::Cast || fact.kind == FactKind::BracedInitList))
	{
		// 4.8 over 5.4p4 and 8.5.4: what a conversion, and what the one clause
		// of a braced-init-list, is worth is what stands under it - brought to
		// this node's own width, which is where 8.5.4p7's narrowing was already
		// checked and where a wider value is rounded.
		if (!folded_real(*node.children[0], value))
		{
			return false;
		}
		value = real ? held_at(types_, fact.type, value) : value;
		return true;
	}
	if (fact.kind == FactKind::BracedInitList && node.children.empty())
	{
		// 8.5.4p3: `{}` value-initializes the object, whose zero this is.
		value = 0;
		return true;
	}
	if (real && fact.kind == FactKind::Unary && node.children.size() == 1 &&
	    (fact.op == OP_PLUS || fact.op == OP_MINUS))
	{
		if (!folded_real(*node.children[0], value))
		{
			return false;
		}
		value = fact.op == OP_MINUS ? -value : value;
		return true;
	}
	if (fact.kind == FactKind::Conditional && node.children.size() == 3)
	{
		// 5.16p1: the condition chooses which of the two the value is, and a
		// constant condition chooses at translation time.
		unsigned long long chosen = 0;
		if (!folded(*node.children[0], chosen))
		{
			return false;
		}
		return folded_real(*node.children[chosen != 0 ? 1 : 2], value);
	}
	if (real && fact.kind == FactKind::Binary && node.children.size() == 2 &&
	    types_.is_floating(types_.strip_cv(fact.operands)))
	{
		// 5.6 and 5.7 over the type 5p9 brought both operands to, which is
		// where 4.8's rounding happens - a `float` sum is a `float` before the
		// operator above it reads it.
		long double left = 0;
		long double right = 0;
		if (!folded_real(*node.children[0], left) ||
		    !folded_real(*node.children[1], right))
		{
			return false;
		}
		left = held_at(types_, fact.operands, left);
		right = held_at(types_, fact.operands, right);
		switch (fact.op)
		{
		case OP_PLUS: value = left + right; break;
		case OP_MINUS: value = left - right; break;
		case OP_STAR: value = left * right; break;
		case OP_DIV:
			if (right == 0)
			{
				return false;
			}
			value = left / right;
			break;
		default: return false;
		}
		value = held_at(types_, fact.operands, value);
		return true;
	}
	unsigned long long bits = 0;
	if (!folded(node, bits))
	{
		return false;
	}
	// 4.9p2: an integer constant initializing an object of floating type is
	// converted to it, and the value it converts to is the one it names.
	value = is_signed(fact.type)
		? static_cast<long double>(static_cast<long long>(bits))
		: static_cast<long double>(bits);
	return true;
}

// 3.6.2p2: the constant an item of the program image holds, spelled at the type
// the storage has.  An integral value is the one 5.19's fold works out; a
// floating one is the spelling 2.14.4 gave it, because there is no integer of
// this translation that is that value.
bool LowirUnitLowering::image_value(const DumpNode& node, TypeId type,
                                    std::string& text)
{
	if (types_.is_reference(type))
	{
		// 8.5.3p5 and 12.2p1: a reference's storage holds the address of an
		// object, and where the initializer was a value there is no object there
		// at all - 3.10p1 makes a prvalue a value, so what the reference binds is
		// a temporary the program gives storage to, which is an address no image
		// can spell and an initialization the program runs.  The value itself is
		// no image for it: an integer written into a reference's storage is an
		// address the program never named.
		return false;
	}
	if (node.fact.zero_initialized &&
	    types_.kind(types_.strip_cv(type)) == TypeKind::Pointer)
	{
		// 8.5p7 and 4.10p1: the zero is the value the *object* was initialized
		// with rather than a literal the program wrote, so a pointer holds the
		// null pointer value - which the image spells the way a body does.
		text = "nullptr";
		return true;
	}
	if (types_.is_floating(types_.strip_cv(type)))
	{
		std::string written;
		if (!floating_image(node, written))
		{
			return false;
		}
		text = spell_floating(type, written);
		return true;
	}
	if (node.fact.kind == FactKind::Literal && !node.fact.constant &&
	    types_.strip_cv(node.fact.type) == types_.fundamental(FT_NULLPTR_T))
	{
		// 2.14.7: the item holds the null pointer value, which the program wrote
		// as `nullptr` and which the image spells the same way a body does.
		text = "nullptr";
		return true;
	}
	unsigned long long bits = 0;
	if (!folded(node, bits))
	{
		return false;
	}
	text = spell_value(type, bits);
	return true;
}

// 5.19 over the resolved tree.  Every operand a namespace-scope initializer of
// the PA15 subset is written from is a value the analysis already knows or an
// operator over ones that are, so the fold is one walk of what is written and
// asks the syntax nothing.
bool LowirUnitLowering::folded(const DumpNode& node, unsigned long long& bits)
{
	const SemaFact& fact = node.fact;
	if (fact.type != kNoType && types_.is_floating(types_.strip_cv(fact.type)))
	{
		// 2.14.4: no integer of this translation holds a floating value - the
		// program spelled it and the analysis kept the spelling - so a fold
		// that answered here would answer with the value's zero and not with
		// the value.  `image_value` is what a floating constant reaches the
		// image through.
		return false;
	}
	if (fact.type != kNoType && fact.constant && fact.value != 0 &&
	    types_.kind(types_.strip_cv(fact.type)) == TypeKind::Pointer)
	{
		// 5.19p2: a constant of pointer type that designates an object holds the
		// identifier that object was interned under and not an address a
		// spelling of this image could carry - `global_address` writes that one
		// as `addr` beside the symbol.  4.10p1's null pointer value is the one
		// such constant that is a number, and it is the number it holds.
		return false;
	}
	if (fact.constant)
	{
		bits = fact.value;
		return true;
	}
	if (types_.strip_cv(fact.type) == types_.fundamental(FT_NULLPTR_T))
	{
		// 4.10p1: `nullptr` is the null pointer value, which holds no address.
		bits = 0;
		return true;
	}
	if (fact.kind == FactKind::Id && fact.entity != nullptr &&
	    fact.entity->constant &&
	    !types_.is_class(types_.strip_cv(fact.entity->type)) &&
	    types_.kind(types_.strip_cv(fact.entity->type)) != TypeKind::Array &&
	    (fact.entity->value == 0 ||
	     types_.kind(types_.strip_cv(fact.entity->type)) != TypeKind::Pointer))
	{
		// A constant of class or array type holds the identifier of the list its
		// subobjects came to, and one of pointer type that designates an object
		// holds the identifier of that object - neither is a value of the
		// object's own width, so neither is an operand a fold of this
		// initializer may stand.  4.10p1's null pointer value is.
		bits = fact.entity->value;
		return true;
	}
	if (fact.kind == FactKind::Cast && node.children.size() == 1)
	{
		if (types_.is_floating(
			    types_.strip_cv(node.children[0]->fact.type)))
		{
			// 4.9p1: a floating value converted to an integral type keeps the
			// part before the point, and 5.19 leaves a conversion that no
			// integral type holds outside a constant expression.
			long double held = 0;
			if (!folded_real(*node.children[0], held))
			{
				return false;
			}
			if (types_.kind(types_.strip_cv(fact.type)) ==
			        TypeKind::Fundamental &&
			    types_.fundamental_type(types_.strip_cv(fact.type)) == FT_BOOL)
			{
				// 4.12p1: the conversion to `bool` asks only whether the value
				// is zero, so the part before the point is no part of it.
				bits = held != 0 ? 1 : 0;
				return true;
			}
			if (!floating_fits_integral(held))
			{
				return false;
			}
			bits = integral_of_floating(held);
			if (types_.is_integral(types_.strip_cv(fact.type)) ||
			    types_.kind(types_.strip_cv(fact.type)) == TypeKind::Enum)
			{
				bits = narrowed(fact.type, bits);
			}
			return true;
		}
		if (!folded(*node.children[0], bits))
		{
			return false;
		}
		// 5.19 over 4.7p2: what the cast is worth is what an object of the type
		// it names holds, which is what every operator above it then reads.
		if (types_.is_integral(types_.strip_cv(fact.type)) ||
		    types_.kind(types_.strip_cv(fact.type)) == TypeKind::Enum)
		{
			bits = narrowed(fact.type, bits);
		}
		return true;
	}
	if (fact.kind == FactKind::BracedInitList)
	{
		bits = 0;
		return node.children.empty() ? true : folded(*node.children[0], bits);
	}
	if (fact.kind == FactKind::Conditional && node.children.size() == 3)
	{
		// 5.16p1: the condition chooses which of the two the value is, and a
		// constant condition chooses at translation time.
		unsigned long long chosen = 0;
		if (!folded(*node.children[0], chosen))
		{
			return false;
		}
		return folded(*node.children[chosen != 0 ? 1 : 2], bits);
	}
	if (fact.kind == FactKind::Unary && node.children.size() == 1)
	{
		unsigned long long operand = 0;
		if (!folded(*node.children[0], operand))
		{
			return false;
		}
		switch (fact.op)
		{
		case OP_PLUS: bits = operand; return true;
		case OP_MINUS: bits = 0ull - operand; return true;
		case OP_COMPL: bits = ~operand; return true;
		case OP_LNOT: bits = operand == 0 ? 1 : 0; return true;
		default: return false;
		}
	}
	if (fact.kind != FactKind::Binary || node.children.size() != 2)
	{
		return false;
	}
	if (types_.is_floating(types_.strip_cv(fact.operands)))
	{
		// 5.9p1 and 5.10p1: a comparison of two floating operands is a `bool`,
		// so the node itself is integral and only its operands are not.
		long double left_real = 0;
		long double right_real = 0;
		if (!folded_real(*node.children[0], left_real) ||
		    !folded_real(*node.children[1], right_real))
		{
			return false;
		}
		left_real = held_at(types_, fact.operands, left_real);
		right_real = held_at(types_, fact.operands, right_real);
		switch (fact.op)
		{
		case OP_LT: bits = left_real < right_real ? 1 : 0; return true;
		case OP_GT: bits = left_real > right_real ? 1 : 0; return true;
		case OP_LE: bits = left_real <= right_real ? 1 : 0; return true;
		case OP_GE: bits = left_real >= right_real ? 1 : 0; return true;
		case OP_EQ: bits = left_real == right_real ? 1 : 0; return true;
		case OP_NE: bits = left_real != right_real ? 1 : 0; return true;
		default: return false;
		}
	}
	unsigned long long left = 0;
	unsigned long long right = 0;
	if (!folded(*node.children[0], left) || !folded(*node.children[1], right))
	{
		return false;
	}
	const bool sign = is_signed(fact.operands);
	switch (fact.op)
	{
	case OP_PLUS: bits = left + right; return true;
	case OP_MINUS: bits = left - right; return true;
	case OP_STAR: bits = left * right; return true;
	case OP_DIV:
		if (right == 0)
		{
			return false;
		}
		bits = sign ? static_cast<unsigned long long>(
		                  static_cast<long long>(left) /
		                  static_cast<long long>(right))
		            : left / right;
		return true;
	case OP_AMP: bits = left & right; return true;
	case OP_BOR: bits = left | right; return true;
	case OP_XOR: bits = left ^ right; return true;
	case OP_LSHIFT: bits = left << (right & 63); return true;
	case OP_RSHIFT: bits = left >> (right & 63); return true;
	default: return false;
	}
}


// 3.6.2: what the image holds for an object with static storage duration before
// the program starts, and the initialization left over for the program to run -
// which is null where 3.6.2p1's zero and the constant the initializer came to
// are the whole of it.
//
// Both the objects a namespace declares and 3.7.1p3's objects a block declares
// `static` are laid out this way, and neither is asked twice: the clauses are
// read once into the definition and what is left of them is handed back.
const DumpNode* LowirUnitLowering::global_image(
	lowir_model::GlobalDefinition& global, const DumpNode& node, TypeId type)
{
	const DumpNode* written = node.children.empty() ? nullptr : node.children[0];
	if (written != nullptr && written->fact.kind == FactKind::BracedInitList &&
	    types_.kind(types_.strip_cv(type)) != TypeKind::Array)
	{
		// 8.5.1p2: a scalar initialized from a list holds what its one clause
		// says, or zero when the list is empty.
		written = written->children.empty() ? nullptr : written->children[0];
	}
	const DumpNode* dynamic = nullptr;
	const TypeId bare = types_.strip_cv(type);
	// 3.6.2p2 with 5.19: whether the object's value is one the analysis worked
	// out.  That is what says the fold went through the definition of the
	// constructor the initialization named, against storage that merely holds
	// 3.6.2p1's zero and went through nothing.
	const bool folded_object =
		node.fact.entity != nullptr && node.fact.entity->constant;
	if (written == nullptr && folded_object &&
	    (types_.is_class(bare) || types_.kind(bare) == TypeKind::Array))
	{
		// 9.4.2p3: the definition of a static data member written outside its
		// class writes no initializer, because the class already wrote one - so
		// the image this definition lays out is what that initializer came to,
		// which is a fact of the member rather than of either declaration's
		// syntax.  A scalar member reaches the image as the literal the analysis
		// wrote under this line; an object of class or array type is a list its
		// subobjects hold and no line of the dump spells, so the storage is laid
		// out from the constant itself.
		global.structured = true;
		unsigned long long laid = 0;
		if (constant_image(global, type, node.fact.entity->value,
		                   node.fact.entity->real, 0, laid))
		{
			add_zero_item(global, types_.object_size(bare) - laid);
			return nullptr;
		}
		global.data_items.clear();
	}
	if (written != nullptr &&
	    written->fact.kind == FactKind::AggregateInitialization)
	{
		// 8.5.1 and 3.6.2p1: the subobjects the clauses reached are what the
		// object's storage holds before the program runs.
		global.structured = true;
		if (!global_aggregate_initializer(global, *written, type))
		{
			lowir_model::GlobalDefinition::DataItem item;
			item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ZERO;
			item.zero_bytes = static_cast<std::size_t>(
				types_.object_size(types_.strip_cv(type)));
			global.data_items.push_back(item);
			dynamic = written;
		}
		written = nullptr;
	}
	else if (written != nullptr &&
	         written->fact.kind == FactKind::ConstructorAction)
	{
		// 3.6.2p2 and 12.1p5: an object of class type starts as zero and is
		// constructed before the program runs, so its storage is data and its
		// constructor is an action.
		global.structured = true;
		lowir_model::GlobalDefinition::DataItem item;
		item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ZERO;
		item.zero_bytes = static_cast<std::size_t>(
			types_.object_size(types_.strip_cv(type)));
		global.data_items.push_back(item);
		// 12.6p1: an array of class type is constructed one element at a time,
		// so where the constructor of an element does nothing there is no
		// action at all - not one that runs before the program and writes
		// nothing.  8.5p7's zero is the same zero 3.6.2p1 already gave the
		// storage, so where that zero is the whole initialization there is
		// nothing left for the startup body to do either, however many elements
		// it would have written it into.
		// 9p6: an object of an empty class holds nothing, so the address the
		// action would name reaches no storage a constructor could write - and
		// where the constructor writes nothing either, the initialization is
		// the image and there is no action for the program to run before it.
		// 12.1p11: an object whose whole storage is the vpointer holds what the
		// standard's own definition of its default constructor writes, which
		// the image can spell as the address of the table - so that
		// initialization is data too, and the program runs nothing before it.
		// 8.5p8 again: holding nothing is a fact of the whole object rather than
		// of the class the declaration named, because a class whose one member is
		// of an empty class is not itself empty and still has no byte a trivial
		// constructor could put anything in.  The vpointer is asked about first,
		// because a class that dispatches accounts for none of its storage by a
		// subobject and the pointer is still there to be written.
		const SemaEntity& built = *written->children[0]->children[0]->fact.entity;
		const bool trivial = built.trivial;
		const bool vpointer = vpointer_image(built, type);
		const bool nothing_to_do = trivial && !vpointer &&
			(written->fact.zero_initialized ||
			 types_.kind(types_.strip_cv(type)) == TypeKind::Array ||
			 !types_.has_zeroed_storage(type));
		if (vpointer)
		{
			global.data_items.clear();
			lowir_model::GlobalDefinition::DataItem address;
			address.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ADDR;
			address.type = low("ptr");
			address.symbol = vtable_symbol(*built.region->owner);
			address.addr_addend = static_cast<long long>(kVtablePrefixBytes);
			global.data_items.push_back(address);
			// 3.2p2: the initialization named the constructor, whose definition
			// the program still needs however little of it the image kept.
			demand_definition(built);
		}
		else if (nothing_to_do)
		{
			// 3.2p2 and 8.4.2p1: the initialization still named the constructor,
			// so what this unit then owes is the one question asked of every
			// image a constructor call stands for.
			owe_folded_construction(built, folded_object);
		}
		else if (types_.kind(types_.strip_cv(type)) == TypeKind::Class &&
		         folded_object && !written->fact.elided_prvalue)
		{
			// 3.6.2p2: where the call of the constructor is itself a constant
			// expression - which is what the analysis says by having folded the
			// object to a value at all - the object's storage *is* what that
			// call leaves behind, and the program runs nothing before it.  The
			// same fold already answers a member of class type inside an
			// aggregate, so a whole object is asked the one question its
			// subobjects are asked.  An object with no initializer, or one
			// whose initializer the analysis could not fold, is not this: 3.6.2
			// leaves its constructor to run before the program does.
			global.data_items.clear();
			unsigned long long laid = 0;
			if (global_constructed(global, *written, 0, laid))
			{
				add_zero_item(global,
				              types_.object_size(types_.strip_cv(type)) - laid);
				return nullptr;
			}
			global.data_items.clear();
			global.data_items.push_back(item);
		}
		dynamic = nothing_to_do || vpointer ? nullptr : written;
		written = nullptr;
	}
	else if (written != nullptr &&
	         types_.kind(types_.strip_cv(type)) == TypeKind::Class)
	{
		// 3.6.2p2 and 8.5p14: the initializer of an object of class type that
		// 12.8p31 left standing is an expression - a call that creates the
		// object where the initialization names storage for it, a name of an
		// object it is copied from - and neither is a clause the image can
		// hold.  So the storage starts as zero and the initialization runs
		// before the program does, at the address the object has, which is the
		// same hand-off a declaration inside a function reaches.  It is not a
		// list of clauses over elements: a class has no element type, and
		// reading one out of it counts a bound no array wrote.
		global.structured = true;
		add_zero_item(global, types_.object_size(types_.strip_cv(type)));
		dynamic = written;
	}
	else if (types_.kind(types_.strip_cv(type)) == TypeKind::Array ||
	         types_.kind(types_.strip_cv(type)) == TypeKind::Class)
	{
		global.structured = true;
		if (!global_array_initializer(global, written, type))
		{
			// 3.6.2p2: a clause names a value this translation does not know,
			// so the whole object starts as zero and is given what it holds
			// before the program runs.
			global.data_items.clear();
			add_zero_item(global, types_.object_size(types_.strip_cv(type)));
			dynamic = written;
		}
	}
	else if (written != nullptr && !global_initializer(global, *written, type))
	{
		if (folded_object && valued_type(type))
		{
			// 3.6.2p2 with 5.19: which constant an initializer is is the
			// *analysis's* answer, and it already made it - `SemaEntity::value`
			// and `::real` are what the object came to.  The walk above is a
			// second fold over the lines the dump spells, and there are values
			// it cannot reach by reading them again: a member of an object a
			// call handed back, an element of an array, what 12.3.2p1's
			// conversion function of a class prvalue returns.  None of those is
			// a line with a value on it, and all of them are this object's
			// value - so where the second fold stops, the first one's answer
			// stands rather than a startup body being written for it.
			global.init_kind = lowir_model::GlobalDefinition::INIT_INTEGER;
			global.init_operand.kind = lowir_model::Operand::OP_INTEGER;
			global.init_operand.text = constant_text(
				type, node.fact.entity->value, node.fact.entity->real);
			return nullptr;
		}
		// 3.6.2p2: an object whose initializer is not a constant starts as
		// zero and is given its value before the program runs.
		global.init_kind = lowir_model::GlobalDefinition::INIT_ZERO;
		dynamic = written;
	}
	return dynamic;
}


void LowirUnitLowering::add_zero_item(lowir_model::GlobalDefinition& global,
                                      unsigned long long bytes)
{
	if (bytes == 0)
	{
		return;
	}
	lowir_model::GlobalDefinition::DataItem item;
	item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ZERO;
	item.zero_bytes = static_cast<std::size_t>(bytes);
	global.data_items.push_back(item);
}

bool LowirUnitLowering::global_subobjects(lowir_model::GlobalDefinition& global,
                                          const DumpNode& node,
                                          unsigned long long base,
                                          unsigned long long& at)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		const unsigned long long stride = types_.object_size(child.fact.type);
		const unsigned long long offset = base +
			(child.fact.entity != nullptr ? child.fact.entity->offset
			                              : child.fact.value * stride);
		if (child.fact.entity != nullptr && child.fact.entity->bit_field)
		{
			// 9.6p2: a bit-field owns a run of bits inside a storage unit that
			// the members beside it own the rest of, and a data item names a
			// whole object rather than a share of one.  A field a clause gave a
			// value to is written by the code that joins the unit together, so
			// the object is given its value before the program runs; a field no
			// clause reached is the zero of its unit, which the first field of
			// the unit writes for all of them.
			if (!child.children.empty())
			{
				return false;
			}
			if (child.fact.entity->bit_width == 0 || offset < at)
			{
				continue;
			}
			add_zero_item(global, offset - at);
			lowir_model::GlobalDefinition::DataItem zero;
			zero.type = low_type(child.fact.type);
			zero.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
			zero.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
			zero.literal_operand.text = "0";
			global.data_items.push_back(zero);
			at = offset + stride;
			continue;
		}
		if (child.fact.op != 0)
		{
			// 8.5.1p7: the elements from here to the end of the array are zero.
			const unsigned long long count =
				types_.bound(types_.strip_cv(child.fact.spelled)) -
				child.fact.value;
			add_zero_item(global, offset - at);
			add_zero_item(global, stride * count);
			at = offset + stride * count;
			continue;
		}
		if (!child.children.empty() &&
		    child.children[0]->fact.kind == FactKind::SubobjectInitialization)
		{
			if (!global_subobjects(global, child, offset, at))
			{
				return false;
			}
			continue;
		}
		if (types_.is_empty_class(types_.strip_cv(child.fact.type)) &&
		    (child.children.empty() ||
		     (child.children[0]->fact.kind == FactKind::ConstructorAction &&
		      (child.children[0]->fact.elided_prvalue ||
		       child.children[0]->children[0]->children[0]->fact.entity->trivial))))
		{
			// 9p6 and 3.6.2p2: a subobject of a class that holds nothing has no
			// bytes for the image to carry, and neither 12.1p5's trivial
			// constructor nor the prvalue 12.8p31 elided into it puts anything
			// there.  What it occupies is padding, which the zero the next item
			// is preceded by covers - so the fold goes on rather than handing
			// the whole object to a startup body.
			continue;
		}
		add_zero_item(global, offset - at);
		lowir_model::GlobalDefinition::DataItem item;
		item.type = low_type(child.fact.type);
		if (child.children.empty())
		{
			item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
			item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
			item.literal_operand.text =
				types_.is_floating(types_.strip_cv(child.fact.type))
					? spell_floating(child.fact.type, "0")
					: "0";
		}
		else
		{
			std::string symbol;
			long long addend = 0;
			if (global_address(*child.children[0], symbol, addend))
			{
				item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ADDR;
				item.symbol = symbol;
				item.addr_addend = addend;
			}
			else if (!runs_a_call(*child.children[0]) &&
			         image_value(*child.children[0], child.fact.type,
			                     item.literal_operand.text))
			{
				item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
				item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
			}
			else
			{
				// 3.6.2p2: the value is not one the translation knows, so the
				// whole object is given its value before the program runs.
				return false;
			}
		}
		null_pointer_item(item, child.fact.type, stride);
		global.data_items.push_back(item);
		at = offset + stride;
	}
	return true;
}

// 4.10p1: a null pointer subobject holds no address at all, which its storage
// says by being zero.  An item names a value of the type the storage has, and
// the null pointer value is not the integer 4.10p1 converts from - which is the
// same answer an element of an array of pointers already takes and the same one
// the scalar image spells `nullptr`.
void LowirUnitLowering::null_pointer_item(
	lowir_model::GlobalDefinition::DataItem& item, TypeId type,
	unsigned long long size)
{
	if (item.kind != lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER ||
	    types_.kind(types_.strip_cv(type)) != TypeKind::Pointer)
	{
		return;
	}
	item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ZERO;
	item.zero_bytes = static_cast<std::size_t>(size);
}

// 3.6.2p2: the value an object with static storage duration holds before the
// program runs, where a constructor is what initializes it.  A constructor
// whose whole definition is 12.6.2's member initializations, each of a value
// the translation knows once the parameters hold the arguments the call passed,
// leaves the object holding one image - so the object holds it and no startup
// body writes it.  The definition is read once per call, and only the calls a
// constant initializer holds ever ask.
// 10p1 and 9.2p13: where the class a constructor belongs to put the direct base
// subobject of type `base`, which is the byte its own base-specifier recorded.
// False where the constructor names no such base, which leaves the image to the
// program to build.
bool LowirUnitLowering::base_subobject_offset(const SemaEntity& constructor,
                                              TypeId base,
                                              unsigned long long& out) const
{
	const SemaEntity* const owner =
		constructor.region == nullptr ? nullptr : constructor.region->owner;
	const TypeId wanted = types_.strip_cv(base);
	for (std::size_t index = 0; owner != nullptr && index < owner->bases.size();
	     ++index)
	{
		if (types_.strip_cv(owner->bases[index].entity->type) == wanted)
		{
			out = owner->bases[index].offset;
			return true;
		}
	}
	return false;
}

bool LowirUnitLowering::global_constructed(
	lowir_model::GlobalDefinition& global, const DumpNode& action,
	unsigned long long base, unsigned long long& at,
	const BoundArguments* outer)
{
	const DumpNode& call = *action.children[0];
	const SemaEntity& constructor = *call.children[0]->fact.entity;
	const std::unordered_map<std::uint32_t, const DumpNode*>::const_iterator
		found = bodies_.find(constructor.id);
	if (found == bodies_.end())
	{
		// 3.2p2: this unit holds no body for the constructor, so what it does
		// is not something the translation knows.
		return false;
	}
	const DumpNode& definition = *found->second;
	// 8.3.6p1 and 5.2.2p4: the arguments stand where the parameters do, in the
	// order both were written, so each parameter is bound to one argument node
	// once and every value read below is one probe of that map.  Whether that
	// argument is work the program runs is a fact of the argument and is read
	// with it: a constructor carrying one place into each of its n members would
	// otherwise walk that one expression n times, which is n * its size for a
	// declaration the program wrote once.
	BoundArguments bound;
	std::size_t argument = 2;
	bool self = true;
	for (std::size_t index = 0; index < definition.children.size(); ++index)
	{
		const DumpNode& child = *definition.children[index];
		if (child.fact.kind != FactKind::Parameter)
		{
			continue;
		}
		if (self)
		{
			// 9.3.1p3's object parameter is the storage this is folding into.
			self = false;
			continue;
		}
		if (argument >= call.children.size() || child.fact.entity == nullptr)
		{
			return false;
		}
		BoundArgument& place = bound[child.fact.entity->id];
		place.value = call.children[argument];
		++argument;
		const BoundArguments::const_iterator above =
			outer == nullptr || place.value->fact.entity == nullptr
				? BoundArguments::const_iterator()
				: outer->find(place.value->fact.entity->id);
		if (outer != nullptr && place.value->fact.entity != nullptr &&
		    above != outer->end())
		{
			// 12.6.2p2: the argument names a parameter of the constructor this
			// one is a mem-initializer of, so what it is worth is what the call
			// one level up passed for that place - read there once and carried
			// down rather than walked again here.
			place.value = above->second.value;
			place.runs_a_call = above->second.runs_a_call;
			continue;
		}
		place.runs_a_call = runs_a_call(*place.value);
	}
	for (std::size_t index = 0; index < definition.children.size(); ++index)
	{
		const DumpNode& child = *definition.children[index];
		if (child.fact.kind == FactKind::Parameter)
		{
			continue;
		}
		if (child.fact.kind == FactKind::Compound)
		{
			// 12.6.2p10: the body runs after the members are initialized, and
			// one that does anything at all is something the translation has to
			// run rather than fold.
			if (!child.children.empty())
			{
				return false;
			}
			continue;
		}
		if (child.fact.kind == FactKind::ConstructorAction &&
		    child.fact.base_subobject)
		{
			// 12.6.2p10: the base class subobjects are constructed before the
			// members and at the byte the class laid each of them out at, and
			// what constructing one comes to is this same walk one level down -
			// so a hierarchy costs one walk per class on it.
			unsigned long long offset = 0;
			if (!base_subobject_offset(constructor, child.fact.type, offset) ||
			    !global_constructed(global, child, base + offset, at, &bound))
			{
				return false;
			}
			continue;
		}
		if (child.fact.kind != FactKind::MemberInitialization ||
		    child.fact.entity == nullptr || child.children.size() < 2)
		{
			// An array member's per-element construction and a member left
			// default-initialized are each a shape this does not fold.
			return false;
		}
		const SemaEntity& member = *child.fact.entity;
		const TypeId type = child.fact.type;
		if (member.bit_field || types_.is_reference(type) ||
		    types_.kind(types_.strip_cv(type)) == TypeKind::Array ||
		    types_.is_class(types_.strip_cv(type)))
		{
			return false;
		}
		const DumpNode* value = child.children[1];
		BoundArgument* place = nullptr;
		if (value->fact.entity != nullptr)
		{
			const BoundArguments::iterator passed =
				bound.find(value->fact.entity->id);
			if (passed != bound.end())
			{
				place = &passed->second;
				value = place->value;
			}
		}
		const bool runs = place != nullptr ? place->runs_a_call
		                                   : runs_a_call(*value);
		const unsigned long long offset = base + member.offset;
		if (offset < at)
		{
			return false;
		}
		add_zero_item(global, offset - at);
		lowir_model::GlobalDefinition::DataItem item;
		item.type = low_type(type);
		std::string symbol;
		long long addend = 0;
		if (global_address(*value, symbol, addend))
		{
			item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ADDR;
			item.symbol = symbol;
			item.addr_addend = addend;
		}
		else
		{
			// 3.6.2p2: what the argument is worth at the type of the subobject
			// it initializes.  A place carried into several members is one
			// expression, and reading it again per member is its size times
			// their number - so the answer is kept beside the place, one per
			// type it is read at.
			BoundArgument::Image alone;
			BoundArgument::Image& image =
				place != nullptr ? place->images[type] : alone;
			if (!runs && !image.held)
			{
				image.held = true;
				image.known = image_value(*value, type, image.text);
			}
			if (runs || !image.known)
			{
				// 3.6.2p2: the value is not one the translation knows - or is
				// one a call produces, which the program runs - so what the
				// constructor does is what the program runs.
				return false;
			}
			item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
			item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
			item.literal_operand.text = image.text;
		}
		null_pointer_item(item, type, types_.object_size(types_.strip_cv(type)));
		global.data_items.push_back(item);
		at = offset + types_.object_size(types_.strip_cv(type));
	}
	// 3.2p2: the initialization named the constructor the program wrote, which
	// odr-uses it however the translation then works out what it leaves the
	// object holding.  The one 8.5.1 gave an aggregate is named by nothing the
	// program wrote, so folding its call leaves no use of it at all - which is
	// the question `owe_folded_construction` asks of every constructor an image
	// stands for.  The definition was *read* to lay this image out, which is
	// what says this unit holds it however the object was declared.
	owe_folded_construction(constructor, true);
	return true;
}

// 3.6.2p2 with 5.2.2p1: an object of class or array type is *built* - by a
// constructor the program calls, or by the clauses that reach its subobjects -
// and what a call comes to is what running the program produces.  So a clause
// that holds a call is work the startup body does and no item the image lays
// out, however well 5.19 folded it: the reference draws the line in the same
// place, and `constexpr Point p(square(3));` starts as zero there and here.
//
// A scalar object is not this.  Its whole initializer is one value, and that
// value is the image - which is why `constexpr int n = square(3);` is `9` in
// both and reaches the image through `SemaEntity::value` rather than through a
// second fold of the lines below it.
//
// 5.3.3p1's operand is unevaluated, so a call written inside a `sizeof` is one
// nothing runs.
bool LowirUnitLowering::runs_a_call(const DumpNode& node) const
{
	if (node.fact.kind == FactKind::Call || node.fact.kind == FactKind::Callee)
	{
		return true;
	}
	if (node.fact.kind == FactKind::Sizeof)
	{
		return false;
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (runs_a_call(*node.children[index]))
		{
			return true;
		}
	}
	return false;
}

bool LowirUnitLowering::global_aggregate_initializer(
	lowir_model::GlobalDefinition& global, const DumpNode& node, TypeId type)
{
	unsigned long long at = 0;
	if (!global_subobjects(global, node, 0, at))
	{
		global.data_items.clear();
		return false;
	}
	// 9.2p13: the object is as large as its class says, whatever its last
	// member ends at.
	add_zero_item(global, types_.object_size(types_.strip_cv(type)) - at);
	return true;
}

std::string LowirUnitLowering::constant_text(TypeId type,
                                             unsigned long long bits,
                                             long double real)
{
	const TypeId bare = types_.strip_cv(type);
	return types_.is_floating(bare)
		? spell_floating(type,
		                 spell_floating_value(types_.fundamental_type(bare), real))
		: spell_value(type, bits);
}

// 3.9.1p8 and 3.9.4: the types whose objects a constant of this translation
// holds one *value* of, which is what the image can spell from the constant
// alone.  A pointer is not one of them: 4.10p1's value is an address, which the
// analysis's `bits` never carries.
bool LowirUnitLowering::valued_type(TypeId type) const
{
	const TypeId bare = types_.strip_cv(type);
	return types_.is_arithmetic(bare) || types_.kind(bare) == TypeKind::Enum;
}

void LowirUnitLowering::constant_item(lowir_model::GlobalDefinition& global,
                                      TypeId type, unsigned long long bits,
                                      long double real)
{
	lowir_model::GlobalDefinition::DataItem item;
	item.type = low_type(type);
	item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
	item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
	item.literal_operand.text = constant_text(type, bits, real);
	null_pointer_item(item, type, types_.object_size(types_.strip_cv(type)));
	global.data_items.push_back(item);
}

// 3.6.2p2 and 9.4.2p3: the value the analysis folded, laid out as the items the
// object's storage holds.  The constant is the object: for a class its bits name
// the interned list of what its members came to, for an array the list of what
// its elements came to, and for a scalar subobject the value itself - so the
// walk is one pass down the layout, and the padding between two subobjects is
// what their offsets say exactly as it is for an aggregate the program wrote.
bool LowirUnitLowering::constant_image(lowir_model::GlobalDefinition& global,
                                       TypeId type, unsigned long long bits,
                                       long double real,
                                       unsigned long long base,
                                       unsigned long long& at)
{
	const TypeId bare = types_.strip_cv(type);
	if (types_.kind(bare) == TypeKind::Array)
	{
		const TypeId element = types_.target(bare);
		const unsigned long long stride =
			types_.object_size(types_.strip_cv(element));
		const std::vector<TypeId>& held =
			types_.type_list_at(static_cast<std::uint32_t>(bits));
		for (std::size_t index = 0; index < held.size(); ++index)
		{
			const TypeId entry = held[index];
			if (!constant_image(global, element, types_.value_bits(entry),
			                    types_.is_floating(types_.strip_cv(element))
			                        ? types_.value_real(entry)
			                        : 0,
			                    base + stride * index, at))
			{
				return false;
			}
		}
		return true;
	}
	if (types_.is_class(bare))
	{
		const SemaEntity* const owner = types_.declaration(bare);
		if (owner == nullptr || owner->scope == nullptr)
		{
			return false;
		}
		const Scope& region = *owner->scope;
		const std::vector<TypeId>& held =
			types_.type_list_at(static_cast<std::uint32_t>(bits));
		std::size_t index = 0;
		for (std::size_t at_base = 0; at_base < owner->bases.size(); ++at_base)
		{
			// 12.6.2p10 and 10p1: the base class subobjects stand before the
			// members in the list the analysis interned, and where each of them
			// begins is what the class recorded when it laid the base out.
			const BaseClass& link = owner->bases[at_base];
			if (index >= held.size())
			{
				return false;
			}
			const TypeId entry = held[index++];
			if (!constant_image(global, link.entity->type,
			                    types_.value_bits(entry), 0, base + link.offset,
			                    at))
			{
				return false;
			}
		}
		for (std::size_t at_member = 0; at_member < region.declarations.size();
		     ++at_member)
		{
			SemaEntity& member = *region.declarations[at_member];
			if (!declares_subobject(member, region))
			{
				continue;
			}
			if (index >= held.size() || member.bit_field)
			{
				// 9.6p2: a bit-field owns a run of bits inside a unit the members
				// beside it own the rest of, which is no item of its own - so the
				// object is one the program builds rather than one the image holds.
				return false;
			}
			const TypeId entry = held[index++];
			if (!constant_image(global, member.type, types_.value_bits(entry),
			                    types_.is_floating(types_.strip_cv(member.type))
			                        ? types_.value_real(entry)
			                        : 0,
			                    base + member.offset, at))
			{
				return false;
			}
		}
		return index == held.size();
	}
	if (!valued_type(bare) || base < at)
	{
		// 8.5.3p5 and 4.10p1: a reference and a pointer each hold the address of
		// an object, which is no value this constant carries - so the object is
		// one the program initializes rather than one the image holds.
		return false;
	}
	add_zero_item(global, base - at);
	constant_item(global, type, bits, real);
	at = base + types_.object_size(bare);
	return true;
}

// 3.6.2p2 over the resolved tree: the address a constant initializer names.
// An entity is an address in itself, a cast leaves an address where it stood,
// and pointer arithmetic over a constant moves it by whole elements.
bool LowirUnitLowering::global_address(const DumpNode& node,
                                       std::string& symbol, long long& addend)
{
	const SemaFact& fact = node.fact;
	if (fact.kind == FactKind::Cast && node.children.size() == 1)
	{
		return global_address(*node.children[0], symbol, addend);
	}
	if (fact.kind == FactKind::Literal && !fact.spelling.empty() &&
	    types_.kind(types_.strip_cv(fact.type)) == TypeKind::Array)
	{
		symbol = string_literal(fact.spelling, fact.type);
		return true;
	}
	if (fact.kind == FactKind::Id && fact.entity != nullptr)
	{
		const SemaEntity& entity = *fact.entity;
		if (entity.kind == SemaKind::Function)
		{
			symbol = function_symbol(entity);
			declare_entity(entity);
			return true;
		}
		const TypeId bare = types_.strip_cv(entity.type);
		if (types_.kind(bare) != TypeKind::Array || entity.region == nullptr ||
		    entity.region->kind != ScopeKind::Namespace)
		{
			return false;
		}
		// 4.2: the array is the address of its first element.
		symbol = global_symbol(entity);
		declare_entity(entity);
		return true;
	}
	if (fact.kind == FactKind::Unary && fact.op == OP_AMP &&
	    node.children.size() == 1 && node.children[0]->fact.kind == FactKind::Id &&
	    node.children[0]->fact.entity != nullptr)
	{
		const SemaEntity& entity = *node.children[0]->fact.entity;
		symbol = entity.kind == SemaKind::Function ? function_symbol(entity)
		                                          : global_symbol(entity);
		declare_entity(entity);
		return true;
	}
	if (fact.kind != FactKind::Binary || node.children.size() != 2 ||
	    (fact.op != OP_PLUS && fact.op != OP_MINUS))
	{
		return false;
	}
	const TypeId pointer = types_.strip_cv(fact.operands);
	if (!types_.is_object_pointer(pointer))
	{
		return false;
	}
	unsigned long long count = 0;
	if (!global_address(*node.children[0], symbol, addend) ||
	    !folded(*node.children[1], count))
	{
		// 5.7p5: the operands of `+` are a pointer and an integer *in either
		// order*, so an image is owed for `1 + numbers` exactly as for
		// `numbers + 1`.  5.7p6's `-` takes the pointer on the left alone.
		if (fact.op != OP_PLUS)
		{
			return false;
		}
		symbol.clear();
		addend = 0;
		if (!global_address(*node.children[1], symbol, addend) ||
		    !folded(*node.children[0], count))
		{
			return false;
		}
	}
	const long long step =
		static_cast<long long>(count * types_.object_size(types_.target(pointer)));
	addend += fact.op == OP_PLUS ? step : -step;
	return true;
}

bool LowirUnitLowering::global_initializer(lowir_model::GlobalDefinition& global,
                                           const DumpNode& node, TypeId type)
{
	// 3.6.2p2: a namespace-scope object with a constant initializer holds that
	// constant before any code runs, so the initializer is data rather than an
	// action.  PA15 lowers the two forms PA12 resolves to one: an integer, and
	// the address of another object or function.
	std::string symbol;
	long long addend = 0;
	if (global_address(node, symbol, addend))
	{
		global.init_kind = lowir_model::GlobalDefinition::INIT_ADDR;
		global.init_operand.kind = lowir_model::Operand::OP_GLOBAL;
		global.init_operand.text = symbol;
		global.addr_addend = addend;
		return true;
	}
	if (types_.is_floating(types_.strip_cv(type)))
	{
		// 3.6.2p2: the storage of a scalar object holds one value, and what the
		// initializer of an object of floating type came to is that value -
		// not the digits one of its operands was written with.
		long double held = 0;
		if (!folded_real(node, held))
		{
			return false;
		}
		global.init_kind = lowir_model::GlobalDefinition::INIT_INTEGER;
		global.init_operand.kind = lowir_model::Operand::OP_INTEGER;
		global.init_operand.text = spell_floating(
			type, spell_floating_value(
				      types_.fundamental_type(types_.strip_cv(type)), held));
		return true;
	}
	if (!image_value(node, type, global.init_operand.text))
	{
		return false;
	}
	global.init_kind = lowir_model::GlobalDefinition::INIT_INTEGER;
	global.init_operand.kind = lowir_model::Operand::OP_INTEGER;
	return true;
}


// 2.14.5p8: the object a string literal is, where 8.5.2p1 copied it into an
// *element* of an array.
//
// The literal has static storage duration whatever is done with it, and the
// copy leaves the array holding its own code units and nothing naming the
// literal - so the object would go unwritten unless the copy itself asks for
// it.  8.5.2p1 fills three kinds of place and this is the one that asks: the
// array that is the whole object and the array that is a member of a class
// each hold the units alone, which is what the reference lays out for them.
void LowirUnitLowering::kept_string_object(const DumpNode& node, TypeId element)
{
	if (node.fact.kind == FactKind::BracedInitList && !node.fact.spelling.empty())
	{
		string_literal(node.fact.spelling, element);
	}
}

bool LowirUnitLowering::global_array_initializer(
	lowir_model::GlobalDefinition& global, const DumpNode* node, TypeId type)
{
	const TypeId array = types_.strip_cv(type);
	const TypeId element = types_.target(array);
	const unsigned long long stride = types_.object_size(element);
	const unsigned long long bound = types_.bound(array);
	const std::size_t clauses = node == nullptr ? 0 : node->children.size();
	if (clauses > bound)
	{
		throw std::runtime_error("an array initializer has more clauses than "
		                         "the array has elements");
	}
	const bool addressed = types_.kind(types_.strip_cv(element)) == TypeKind::Pointer;
	const bool aggregate = types_.kind(types_.strip_cv(element)) == TypeKind::Array;
	// 8.5.1p7: the elements no clause reached may be one action rather than one
	// each, so how many elements the children account for is not how many
	// children there are.
	unsigned long long covered = 0;
	for (std::size_t index = 0; index < clauses; ++index)
	{
		const unsigned long long run = node->children[index]->fact.elements;
		if (run > 1)
		{
			// 3.6.2p2: every element of the run holds what that one constructor
			// leaves, and where that is the zero the image already holds the
			// whole run is one item.  Anything else would be one item per
			// element, which is the count the source wrote as a number, so the
			// array is given what it holds before the program runs instead.
			const unsigned long long base = covered * stride;
			unsigned long long at = base;
			const std::size_t before = global.data_items.size();
			if (!global_constructed(global, *node->children[index], base, at) ||
			    global.data_items.size() != before || at != base)
			{
				return false;
			}
			add_zero_item(global, run * stride);
			covered += run;
			continue;
		}
		covered += 1;
		if (aggregate)
		{
			// 8.5.1p3: an element that is itself an aggregate holds what its own
			// list says, so its items are this one's items in the same order.
			kept_string_object(*node->children[index], element);
			if (!global_array_initializer(global, node->children[index], element))
			{
				return false;
			}
			continue;
		}
		if (node->children[index]->fact.kind == FactKind::ConstructorAction)
		{
			// 3.6.2p2: the element is built by a constructor, and where what
			// that constructor does is one image the translation knows, the
			// element holds it rather than being built before the program runs.
			const unsigned long long base =
				static_cast<unsigned long long>(index) * stride;
			unsigned long long at = base;
			if (!global_constructed(global, *node->children[index], base, at))
			{
				return false;
			}
			add_zero_item(global, base + stride - at);
			continue;
		}
		if (node->children[index]->fact.kind ==
		    FactKind::AggregateInitialization)
		{
			// 8.5.1p1 and 3.6.2p2: an element of class type holds what the
			// subobjects its own clauses reached hold, at the offsets 9.2p13
			// gave them inside the element.  What the element does not fill is
			// the padding of one object rather than of the array.
			const unsigned long long base =
				static_cast<unsigned long long>(index) * stride;
			unsigned long long at = base;
			if (!global_subobjects(global, *node->children[index], base, at))
			{
				return false;
			}
			add_zero_item(global, base + stride - at);
			continue;
		}
		lowir_model::GlobalDefinition::DataItem item;
		item.type = low_type(element);
		std::string symbol;
		long long addend = 0;
		if (addressed && global_address(*node->children[index], symbol, addend))
		{
			item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ADDR;
			item.symbol = symbol;
			item.addr_addend = addend;
			global.data_items.push_back(item);
			continue;
		}
		if (runs_a_call(*node->children[index]) ||
		    !image_value(*node->children[index], element,
		                 item.literal_operand.text))
		{
			// 3.6.2p2: the element's value is not one the translation knows -
			// or is one a *call* produces, which 5.2.2p1 makes work the program
			// runs and no item an array of scalars may lay out any more than a
			// class's clause or a member initialization may - so the whole array
			// is given what it holds before the program runs.
			return false;
		}
		if (addressed)
		{
			// 4.10p1: a null pointer element holds no address at all, which
			// its storage says by being zero.
			item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ZERO;
			item.zero_bytes = static_cast<std::size_t>(stride);
			global.data_items.push_back(item);
			continue;
		}
		item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
		global.data_items.push_back(item);
	}
	// 8.5.1p7: the elements no clause reached are value-initialized, which for
	// every type this milestone lays out is the zero of them.
	add_zero_item(global, (bound - covered) * stride);
	return true;
}


// 3.6.2p2 and 12.1p11: whether the image of an object with static storage
// duration can hold what constructing it comes to.
//
// It can where the whole of the object is the vpointer: 9p6 leaves a class whose
// storage 10.3p1's pointer accounts for entirely no data member and no base
// subobject beside it, so what constructing it writes is that one pointer and
// 12.6.2p10 has nothing to build before it.
//
// 7.1.5p4 is what says the constructor writes no more than that: a constexpr
// constructor's body holds no statement that writes anything, and every
// constructor its ctor-initializer involves shall be a constexpr one too - so
// the whole initialization is the pointer whether the standard wrote the
// constructor or the program did.  One that is not constexpr may do anything at
// all, which is 3.6.2p2's dynamic initialization and not this image.
bool LowirUnitLowering::vpointer_image(const SemaEntity& built, TypeId type)
{
	const TypeId bare = types_.strip_cv(type);
	if (!built.constexpr_function || built.region == nullptr ||
	    built.region->owner == nullptr || !built.region->owner->polymorphic ||
	    types_.kind(bare) != TypeKind::Class)
	{
		return false;
	}
	return types_.object_size(bare) ==
		types_.object_size(types_.pointer_to(types_.fundamental(FT_VOID)));
}

