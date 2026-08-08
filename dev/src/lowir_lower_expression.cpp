#include "lowir_lower.h"

#include <sstream>
#include <stdexcept>

#include "sema_scope.h"
#include "token_model.h"

// The expression layer of the PA15 lowering: one resolved expression, read
// bottom up into the operands its operands already produced.
//
// What every function here answers is the same question in one shape or
// another - what is this expression worth, and where does what it is worth
// stand.  A value stands in a temporary the instruction that made it named; an
// lvalue stands in storage a name reaches; and 12.2p1's object of class type
// stands in storage the place asking for it named before the expression ran,
// which is what lets the expression create its object there rather than be
// copied into it.

namespace {

using lowir_model::Instruction;
using lowir_model::Operand;

std::string decimal(unsigned long long value)
{
	std::ostringstream out;
	out << value;
	return out.str();
}

Operand named_operand(Operand::Kind kind, const std::string& text)
{
	Operand operand;
	operand.kind = kind;
	operand.text = text;
	return operand;
}

}  // namespace

LowValue LowirFunctionLowering::expression(const DumpNode& node,
                                           bool as_object)
{
	TypeTable& types = unit_.types();
	if (!selected_arms_.empty() && &selected(node) != &node)
	{
		// 5.16p3: this initialization is being written over the arms of a
		// conditional, and on this path the conditional is worth the arm
		// control reached - so every reader of it reads that one.
		return expression(selected(node), as_object);
	}
	if (walks_this_array(node))
	{
		// 12.8p15 and p28: this line stands inside one step of an array
		// member's transfer, and what it names - the object being written into,
		// the object being read from - is the element the walk has reached.
		return walked_element(node);
	}
	if (as_object && stands_in_no_storage(node))
	{
		// 12.2p1: what is needed here is the object the expression is worth,
		// and a prvalue of class type a call handed back stands in no storage
		// at all - so the function gives it some, and gives it before the call
		// runs, because a call that returns its object indirectly creates it
		// there and one that hands back the bytes is copied into it.
		return class_object_slot(node, types.strip_cv(node.fact.type), "tmpobj");
	}
	switch (node.fact.kind)
	{
	case FactKind::Literal:
	case FactKind::Sizeof:
		return literal(node);

	case FactKind::Id:
		return id_expression(node);

	case FactKind::Member:
		return member_expression(node);

	case FactKind::Call:
		return call_expression(node);

	case FactKind::Unary:
		return node.fact.op == OP_INC || node.fact.op == OP_DEC
			? increment_expression(node, false)
			: unary_expression(node);

	case FactKind::Postfix:
		return increment_expression(node, true);

	case FactKind::Binary:
		return binary_expression(node);

	case FactKind::Assignment:
		return assignment_expression(node);

	case FactKind::Conditional:
		return conditional_expression(node, as_object);

	case FactKind::Subscript:
		return subscript_expression(node);

	case FactKind::Cast:
		return cast_expression(node, as_object);

	case FactKind::BaseConversion:
		return base_conversion(node);

	case FactKind::TemporaryObject:
		return temporary_object(node);

	case FactKind::NewExpression:
		return new_expression(node);

	case FactKind::DeleteExpression:
		return delete_expression(node);

	case FactKind::BracedInitList:
	{
		// 8.5.4 over a scalar: the value is what its one clause says, and an
		// empty list is the zero of the type it initializes.
		if (!node.children.empty())
		{
			return expression(*node.children[0], as_object);
		}
		LowValue value;
		value.type = node.fact.type;
		value.constant = true;
		value.operand = zero_operand(value.type);
		return value;
	}

	default:
		break;
	}
	throw std::runtime_error("an expression is outside the PA15 lowering subset: "
	                         + node.text);
}

LowValue LowirFunctionLowering::literal(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	LowValue value;
	value.type = node.fact.type;
	if (!node.fact.spelling.empty() &&
	    types.kind(types.strip_cv(value.type)) == TypeKind::Array)
	{
		// 2.14.5p8: the literal is an array object of static storage duration,
		// which the program holds under a name of its own.
		value.lvalue = true;
		value.unnamed = true;
		value.operand.kind = Operand::OP_GLOBAL;
		value.operand.text =
			unit_.string_literal(node.fact.spelling, value.type);
		return value;
	}
	if (types.strip_cv(value.type) == types.fundamental(FT_NULLPTR_T) &&
	    !node.fact.constant)
	{
		// 2.14.7 and 4.10p1: `nullptr` is a value of its own type, which every
		// pointer type converts from and which LowIR spells as it is written.
		// 4.10p1's *other* null pointer constant is an integral constant
		// expression of value zero, which is the integer the program wrote and
		// keeps that spelling wherever it converts to - the two are the same
		// value and not the same token, and the constant is what says which.
		value.operand = named_operand(Operand::OP_INTEGER, "nullptr");
		return value;
	}
	if (!node.fact.constant)
	{
		throw std::runtime_error("a literal is outside the PA15 lowering subset");
	}
	value.constant = true;
	value.value = node.fact.value;
	if (!node.fact.spelling.empty())
	{
		// 2.14.4: the value is a floating one, which the program spelled
		// exactly and which no integer of the translation holds.
		value.constant = false;
		value.operand.kind = Operand::OP_FLOAT;
		value.operand.text = node.fact.spelling;
		return value;
	}
	if (node.fact.kind == FactKind::Sizeof)
	{
		// 5.3.3p6: the size is a value the translation computed rather than one
		// the program wrote, and LowIR names such a value with `const`.
		Instruction instruction;
		instruction.kind = Instruction::IK_CONST;
		instruction.type = unit_.low_type(value.type);
		instruction.first = literal_operand(value.type, node.fact.value);
		value.operand = emit(instruction);
		value.constant = false;
		return value;
	}
	// 8.5p7: a zero the initialization is rather than a literal the program
	// wrote is a value of the object's own type, which for a pointer is 4.10p1's
	// null pointer value and not the integer a null pointer constant is.
	value.operand = node.fact.zero_initialized
		? zero_operand(value.type)
		: literal_operand(value.type, node.fact.value);
	return value;
}

LowValue LowirFunctionLowering::id_expression(const DumpNode& node)
{
	SemaEntity& entity = *node.fact.entity;
	if (entity.kind == SemaKind::Function)
	{
		// 4.3: a function name used as a value is a pointer to it, and the
		// address of a function is the symbol itself.
		unit_.declare_entity(entity);
		LowValue value;
		value.type = entity.type;
		value.lvalue = true;
		value.operand =
			named_operand(Operand::OP_GLOBAL, unit_.function_symbol(entity));
		return value;
	}
	return storage_of(entity);
}

// 5.2.5p1 and 9.2p13: the member of the object the operand denotes, which is
// that object's storage advanced by the place its class gave the member.  The
// operand is a pointer where `->` or an implicit `this` wrote one and the
// object itself where `.` did, and its own type is what says which.
// 9.2p13: where one member of an object begins, which is what the layout of its
// class settled.  A member of reference type holds a pointer, so this is the
// storage of that pointer rather than of the object it names, and the
// projection says which of the two the address is being taken for: `bound` is
// the initialization writing the pointer into the member's own storage, and
// anything else is a use that reads the object through it.
Operand LowirFunctionLowering::member_storage(const DumpNode& object,
                                              const SemaEntity& member,
                                              bool bound)
{
	TypeTable& types = unit_.types();
	// 9.5p1: an anonymous class declared an object no name reaches, and its
	// members are reached through it - so the access the analysis wrote holds
	// one step for that object and one for the member.  Nothing reads the
	// object itself, so where the member stands is one offset from where the
	// enclosing object stands, and it is written as the one step it is.
	const DumpNode* at = &object;
	unsigned long long offset = member.offset;
	for (const SemaEntity* held = member.storage;
	     held != nullptr && held->object_member && !at->children.empty();
	     held = held->storage)
	{
		offset += held->offset;
		at = at->children[0];
	}
	const LowValue held = expression(*at, true);
	const Operand base =
		types.kind(types.strip_cv(held.type)) == TypeKind::Pointer
			? rvalue(held)
			: address_of(held);
	Instruction step;
	step.kind = Instruction::IK_INDEX;
	step.type.text = "i8";
	step.index_projection = types.is_reference(member.type) && !bound
		? lowir_model::IPK_REFERENCE_FIELD
		: lowir_model::IPK_FIELD;
	step.first = base;
	step.second = named_operand(Operand::OP_INTEGER, decimal(offset));
	return emit(step);
}

// 12.8p15 and p28: one step of a value transfer the standard defines that
// carries storage rather than naming a subobject.  A step with no type is the
// leading run of members a copy of the bytes carries exactly, which is one
// `copyobj` of the span it covers; a step with one is 9.6p2's storage unit,
// which the bit-fields sharing it are carried in with one read and one write.
void LowirFunctionLowering::storage_transfer(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const unsigned long long offset = node.fact.value;
	if (node.fact.type == kNoType)
	{
		const Operand into = address_of(expression(*node.children[0], true));
		const Operand from = address_of(expression(*node.children[1], true));
		Instruction copy;
		copy.kind = Instruction::IK_COPYOBJ;
		copy.byte_count = static_cast<std::size_t>(node.fact.elements);
		// 3.11p1: the run stands `offset` bytes into an object of the class, so
		// what is known of its address is the class's own alignment cut down to
		// the one that many bytes still allow - which for a run beginning where
		// the object does is the class's.
		unsigned long long known =
			types.object_align(types.strip_cv(node.children[0]->fact.type));
		while (offset % known != 0)
		{
			known /= 2;
		}
		copy.byte_alignment = static_cast<std::size_t>(known);
		copy.first = at_offset(from, offset);
		copy.second = at_offset(into, offset);
		emit_void(copy);
		return;
	}
	// 5.17p1: the value is read out of the object it is read from before the
	// object it is written into is named, which is the order every other
	// assignment this lowering writes has.  The unit is a member of the object
	// rather than the object itself, so where it stands is written as the step
	// down to it that every other member access writes.
	const Operand from = address_of(expression(*node.children[1], true));
	const Operand held = load(field_at(from, offset), node.fact.type);
	const Operand into = address_of(expression(*node.children[0], true));
	store(held, field_at(into, offset), node.fact.type);
}

// 9.2p13: the storage `offset` bytes into the object standing at `at`, which is
// where a member the layout put there begins.  An offset of zero is the object
// itself, so nothing is written for it.
Operand LowirFunctionLowering::at_offset(const Operand& at,
                                         unsigned long long offset)
{
	if (offset == 0)
	{
		return at;
	}
	return field_at(at, offset);
}

// The same step written whether or not the member begins where the object does,
// which is what a member access is: the storage this names is the member's and
// not the object's, however far into it the layout put it.
Operand LowirFunctionLowering::field_at(const Operand& at,
                                        unsigned long long offset)
{
	Instruction step;
	step.kind = Instruction::IK_INDEX;
	step.type.text = "i8";
	step.index_projection = lowir_model::IPK_FIELD;
	step.first = at;
	step.second = named_operand(Operand::OP_INTEGER, decimal(offset));
	return emit(step);
}

// 4.10p3 and 10p1: the base class subobject of the object the operand denotes,
// which is that object's storage at the place its class gave the base.  The
// operand is a pointer where a pointer converted and the object itself where an
// lvalue did, and the node's own type says which of the two the result is.
LowValue LowirFunctionLowering::base_conversion(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	// A conversion of a *pointer* reads the pointer value its operand holds,
	// and one of an object reads the storage the operand names.  5.16p4 makes
	// `c ? p : q` an lvalue of pointer type, so asking for the object where a
	// pointer converted would name the slot the conditional chose rather than
	// the pointer standing in it.
	const bool converts_pointer =
		types.kind(types.strip_cv(node.fact.type)) == TypeKind::Pointer;
	const LowValue held = expression(*node.children[0], !converts_pointer);
	const Operand from =
		types.kind(types.strip_cv(held.type)) == TypeKind::Pointer
			? rvalue(held)
			: address_of(held);
	LowValue value;
	value.type = node.fact.type;
	value.named = true;
	value.lvalue =
		types.kind(types.strip_cv(value.type)) != TypeKind::Pointer;
	value.operand = node.fact.null_preserving && node.fact.value != 0
		? null_preserving_base_step(from, node.fact.value)
		: base_step(from, node.fact.value);
	return value;
}

// 10p1: the address of the base subobject of the object `from` points at, which
// is that address moved on by the place the derived class gave its base.
Operand LowirFunctionLowering::base_step(const Operand& from,
                                         unsigned long long offset)
{
	Instruction step;
	step.kind = Instruction::IK_INDEX;
	step.type.text = "i8";
	step.index_projection = lowir_model::IPK_BASE_SUBOBJECT;
	step.first = from;
	step.second = named_operand(Operand::OP_INTEGER, decimal(offset));
	return emit(step);
}

// 4.10p3: the same conversion of a pointer the program could have written a
// null into.  A null pointer converts to the null pointer value of the base's
// type, so where the base subobject does not begin where the object does the
// conversion is a test and not an address: moving a null on by the offset would
// hand back a pointer to storage no object stands in.  Only a nonzero offset
// reaches here, so no program pays for the branch that PA17's layout gave every
// base subobject.
Operand LowirFunctionLowering::null_preserving_base_step(
	const Operand& from, unsigned long long offset)
{
	lowir_model::LowType pointer;
	pointer.text = "ptr";
	const std::string slot = add_generated_slot("basecast", pointer);
	const Operand storage = named_operand(Operand::OP_SLOT, slot);
	const std::string null_label = reserve_block("basecast_null");
	const std::string adjust_label = reserve_block("basecast_adjust");
	const std::string end_label = reserve_block("basecast_end");
	Instruction test;
	test.kind = Instruction::IK_CMP;
	test.op = "eq";
	test.type.text = "ptr";
	test.first = from;
	test.second = named_operand(Operand::OP_INTEGER, "0");
	branch(emit(test), null_label, adjust_label);

	open_block(null_label);
	store_pointer(named_operand(Operand::OP_INTEGER, "0"), storage);
	jump(end_label);

	open_block(adjust_label);
	store_pointer(base_step(from, offset), storage);
	jump(end_label);

	open_block(end_label);
	Instruction read;
	read.kind = Instruction::IK_LOAD;
	read.type.text = "ptr";
	read.first = storage;
	return emit(read);
}

// One `store` of a pointer, which the two arms above write into the one slot
// the conversion's value is read back out of.
void LowirFunctionLowering::store_pointer(const Operand& value,
                                          const Operand& storage)
{
	Instruction instruction;
	instruction.kind = Instruction::IK_STORE;
	instruction.type.text = "ptr";
	instruction.first = value;
	instruction.second = storage;
	emit_void(instruction);
}

LowValue LowirFunctionLowering::member_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const SemaEntity& member = *node.fact.entity;
	LowValue value;
	value.type = node.fact.type;
	value.lvalue = true;
	value.named = true;
	value.operand = member_storage(*node.children[0], member);
	if (member.bit_field)
	{
		// 9.6p2: the member names a run of bits inside the unit just addressed,
		// so what the lvalue is worth and what a write puts back are the field's
		// own question rather than the unit's.
		value.field = &member;
		return value;
	}
	if (types.is_reference(member.type))
	{
		// 8.3.2p5: a member of reference type names what it is bound to, so the
		// object the member holds is reached through the pointer it stores.
		value.operand = load(value.operand, member.type);
	}
	return value;
}

LowValue LowirFunctionLowering::cast_expression(const DumpNode& node,
                                                bool as_object)
{
	TypeTable& types = unit_.types();
	LowValue value;
	value.type = node.fact.type;
	if (node.children.empty())
	{
		value.constant = true;
		value.operand = literal_operand(value.type, node.fact.value);
		return value;
	}
	const bool to_void = types.is_void(types.strip_cv(value.type));
	if (to_void && discarded_class_object(*node.children[0]))
	{
		return value;
	}
	// 5.2.9p4 and 12.8p31: the object standing under the cast is the object the
	// cast direct-initialized, whatever node the prvalue was written as - a
	// `temporary-object` the program wrote and a call handing back an object of
	// this class alike - so the cast is worth that object and reads it as one.
	const bool creates_under = types.is_class(types.strip_cv(value.type)) &&
		creates_its_object(node, types);
	const bool object_result = as_object || to_void || creates_under ||
		node.fact.category != ValueCategory::PRValue;
	const LowValue source = expression(*node.children[0], object_result);
	if (node.fact.category != ValueCategory::PRValue)
	{
		if (node.fact.binds_temporary &&
		    !types.is_class(types.strip_cv(value.type)))
		{
			// 8.5.3p5: what this reference binds is a temporary holding the
			// *conversion* of the operand and not the storage the operand
			// named, which is an object of this function like the one an
			// argument bound to a reference parameter already materializes.
			// Which of 5.4p4's two readings this is, is the fact the analysis
			// wrote: the types below say nothing, because 5.2.10p11's
			// reinterpretation spells exactly the same two.
			//
			// A class referenced type is not one of these: there the conversion
			// made an object the analysis already named storage for, and the
			// operand's line is that object.
			const TypeId wanted = types.strip_cv(value.type);
			const std::string slot = add_generated_slot("refcast", wanted);
			const Operand storage = named_operand(Operand::OP_SLOT, slot);
			store(convert_scalar(rvalue(source), types.strip_cv(source.type),
			                     wanted),
			      storage, wanted);
			value.lvalue = true;
			value.operand = storage;
			return value;
		}
		// 5.2.9p1 and 5.2.11: a cast to a reference names the object the
		// operand named, so what it produces is that storage.
		value.lvalue = true;
		value.operand = source.operand;
		return value;
	}
	if (to_void)
	{
		// 5.2.9p4 and 5p11: the operand of a cast to `void` is evaluated and
		// its value discarded.  An object of class type has no value the
		// discarding reads, so nothing loads it out of the storage it stands in.
		const TypeId bare = types.strip_cv(source.type);
		if (!types.is_class(bare) && types.kind(bare) != TypeKind::Array)
		{
			rvalue(source);
			return value;
		}
		// An object of class or array type has no value the discarding reads,
		// so the storage it stands in is what the operand comes to.
		address_of(source);
		return value;
	}
	if (types.kind(types.strip_cv(value.type)) == TypeKind::Pointer &&
	    types.is_integral(types.strip_cv(source.type)))
	{
		if (node.fact.op != 0 && source.constant && source.value == 0 &&
		    types.kind(types.strip_cv(source.type)) != TypeKind::Enum)
		{
			// 5.4p4 and 4.10p1: a cast the program wrote whose operand is a
			// null pointer constant - a constant expression of *integer* type
			// that evaluates to zero, which an enumerator is not - is 4.10p1's
			// conversion and not 5.2.10p5's reading of an address.  What it
			// produces is the null pointer value, and nothing computes that.
			// A conversion the program did not write is one 8.5.3p5 may have to
			// hold a temporary for, so it keeps the value an instruction names.
			value.constant = true;
			value.operand = named_operand(Operand::OP_INTEGER, "0");
			return value;
		}
		// 5.2.10p5: the cast reads the integer as an address, which is the same
		// bits under another type.
		Instruction instruction;
		instruction.kind = Instruction::IK_COPY;
		instruction.type.text = "ptr";
		instruction.first = rvalue(source);
		value.operand = emit(instruction);
		return value;
	}
	if (source.constant && types.is_integral(types.strip_cv(value.type)) &&
	    !(types.kind(types.strip_cv(value.type)) == TypeKind::Fundamental &&
	      types.fundamental_type(types.strip_cv(value.type)) == FT_BOOL) &&
	    types.is_integral(types.strip_cv(source.type)))
	{
		// 5.2.9 over a constant: the value the cast produces is a value the
		// translation knows, so it is written as the immediate it is - and it
		// is the immediate an object of the cast's own type holds, which is
		// what a conversion above it then widens.  4.12's conversion to `bool`
		// is not one of those: what it produces is whether the operand differs
		// from zero, which is not the same bits read at another width.
		value.constant = true;
		value.value = unit_.narrowed(value.type, source.value);
		value.operand = literal_operand(value.type, value.value);
		return value;
	}
	if (types.is_class(types.strip_cv(value.type)))
	{
		if (creates_under ||
		    (node.children[0]->fact.kind == FactKind::TemporaryObject &&
		     types.strip_cv(node.children[0]->fact.type) ==
		         types.strip_cv(value.type)))
		{
			// 5.2.9p4 and 12.8p31: the operand is the object the cast
			// direct-initialized, so the cast is worth that object and no copy
			// of it stands between the two.
			return source;
		}
		// 5.2.9p4 and 12.2p1: a cast to a class type direct-initializes a
		// temporary of it, which is an object the function holds.  The cast is
		// worth that object, so whoever reads it reads storage rather than a
		// value of the class.
		LowValue held;
		held.type = types.strip_cv(value.type);
		held.lvalue = true;
		held.named = true;
		held.operand = named_operand(
			Operand::OP_SLOT,
			add_generated_slot("tmpobj", types.strip_cv(value.type)));
		const Operand into = address_of(held);
		copy_class_object(into, class_copy_source(source),
		                  types.strip_cv(value.type));
		held.operand = into;
		return held;
	}
	value.operand = converted(source, value.type);
	return value;
}

LowValue LowirFunctionLowering::call_expression(const DumpNode& node,
                                                const Operand* into,
                                                std::string* keep)
{
	TypeTable& types = unit_.types();
	Instruction call;
	call.kind = Instruction::IK_CALL;
	const DumpNode& callee = *node.children[0];
	const bool direct = callee.fact.kind == FactKind::Callee;
	// 15.4p1: a call of a function that says it throws nothing is not a place
	// an exception leaves this step by; every other call is, including one
	// through a pointer, whose declaration says nothing about the function.
	const bool throwing =
		!direct || callee.fact.entity == nullptr ||
		!callee.fact.entity->nonthrowing;
	// 15.2p2: the step this call belongs to, which a call written as one of its
	// operands stands inside rather than opening one of its own.
	const bool step = mark_call_step();
	if (throwing)
	{
		++pending_calls_;
	}

	// What the call boundary is, is known from the callee's resolved type
	// before anything is emitted, which is what lets the arguments be lowered
	// where the source wrote them: before the callee they are passed to.
	const TypeId written = types.strip_cv(
		direct ? callee.fact.entity->type : callee.fact.type);
	const TypeId function = types.kind(written) == TypeKind::Function
		? written
		: types.target(written);
	const std::vector<TypeId>& parameters = types.parameters(function);
	const TypeId result = types.target(function);
	const bool indirect = types.returns_indirectly(result);
	// The value the call hands back is stored where the function can name it
	// again, wherever it stands under a handler: a block a handler reaches is
	// not one another block's temporaries are named in.  The storage is named
	// before the operands are lowered, because whether it is needed is a fact
	// of the call and not of the order the operands took.
	const bool has_value = !indirect && !types.is_void(types.strip_cv(result));
	const std::string spilled = throwing && has_value && guarded_call(node)
		? add_generated_slot("call", unit_.low_type(result))
		: std::string();
	Operand destination;
	if (indirect)
	{
		// 6.6.3p2: the returned object stands in storage the caller names, and
		// that name is the first thing the call is about - so it is written
		// before the arguments, whether the place asking for the object owned
		// the storage already or the function is giving it some of its own.
		destination = into != nullptr
			? *into
			: open_object_slot(types.strip_cv(result),
			                   node.fact.spelling.empty()
			                       ? "tmpobj"
			                       : node.fact.spelling.c_str());
		call.args.push_back(destination);
	}
	for (std::size_t index = 1; index < node.children.size(); ++index)
	{
		const std::size_t at = index - 1;
		const TypeId parameter = at < parameters.size() ? parameters[at] : kNoType;
		if (parameter != kNoType && !types.is_reference(parameter) &&
		    types.is_class(types.strip_cv(parameter)))
		{
			// 5.2.2p4: the parameter is an object of class type, whose storage
			// is what the call passes - so the storage is named before the
			// argument runs, because the argument creates its object in it.
			call.args.push_back(class_argument(*node.children[index],
			                                   types.strip_cv(parameter)));
			continue;
		}
		const bool bound = parameter != kNoType && types.is_reference(parameter);
		const LowValue argument = expression(*node.children[index], bound);
		call.args.push_back(
			passed_operand(*node.children[index], argument, parameters, at));
		if (keep != nullptr && at == 0)
		{
			// 5.3.4p1: the array form reads this argument again once the call
			// has returned, so the function keeps it where it was computed.
			*keep = add_generated_slot("array_new_size", parameter);
			store(call.args.back(), named_operand(Operand::OP_SLOT, *keep),
			      parameter);
		}
	}
	if (direct && callee.fact.dispatches)
	{
		// 10.3p12: the call runs the final overrider the object's own class has
		// for this slot, which the object says and the declaration does not.
		// The declaration is still what the boundary is written from: 10.3p7
		// lets an override hand back a pointer to a class derived from the one
		// this call was resolved against, and what the caller reads is the type
		// it named.
		SemaEntity& entity = *callee.fact.entity;
		call.has_call_signature = true;
		call.first = dispatch_slot(call.args[indirect ? 1 : 0],
		                           entity.vtable_index);
	}
	else if (direct)
	{
		SemaEntity& entity = *callee.fact.entity;
		unit_.declare_entity(entity);
		call.first =
			named_operand(Operand::OP_GLOBAL, unit_.function_symbol(entity));
	}
	else
	{
		// 5.2.2p1: a call through a pointer calls what it points to, and the
		// boundary the callee operand does not describe is written out.
		const LowValue target = expression(callee);
		call.has_call_signature = true;
		call.first = types.kind(types.strip_cv(target.type)) == TypeKind::Function
			? decay(target)
			: rvalue(target);
	}
	if (call.has_call_signature)
	{
		unit_.open_signature(result, call.call_params, call.call_return_type);
		for (std::size_t index = 0; index < parameters.size(); ++index)
		{
			lowir_model::Parameter parameter;
			parameter.name = "arg" + decimal(index);
			unit_.describe_parameter(parameters[index], parameter);
			call.call_params.push_back(parameter);
		}
		if (types.variadic(function))
		{
			call.call_boundary.arity = lowir_model::CAM_VARIADIC;
		}
	}
	LowValue value;
	value.type = types.is_reference(result) ? types.target(result) : result;
	note_call(throwing);
	if (throwing)
	{
		--pending_calls_;
	}
	if (indirect)
	{
		// 6.6.3p2: the call wrote its object into the storage the destination
		// names, so what the call is worth is that object.
		call.type.text = "void";
		emit_void(call);
		release_call_step(step);
		value.lvalue = true;
		value.operand = destination;
		return value;
	}
	call.type = unit_.low_type(result);
	if (types.is_void(types.strip_cv(result)))
	{
		emit_void(call);
		release_call_step(step);
		return value;
	}
	Operand produced = emit(call);
	if (!spilled.empty())
	{
		const Operand slot = named_operand(Operand::OP_SLOT, spilled);
		store(produced, slot, result);
		produced = load(slot, result);
	}
	release_call_step(step);
	if (types.is_reference(result))
	{
		// 5.2.2p10: a call of a function returning a reference is an lvalue,
		// which the address it returned names.
		value.lvalue = true;
	}
	value.operand = produced;
	return value;
}

// 15.2p2: whether this call stands under a handler, which is what says the
// value it hands back has to be named by something a block the handler reaches
// can name too.  It does where an object's lifetime is already open, and where
// one this call's own operands begin will be open by the time it runs.
bool LowirFunctionLowering::guarded_call(const DumpNode& node)
{
	if (!unwind_live_.empty())
	{
		return true;
	}
	for (std::size_t index = 1; index < node.children.size(); ++index)
	{
		if (begins_lifetime(*node.children[index]))
		{
			return true;
		}
	}
	return !node.children.empty() && begins_lifetime(*node.children[0]);
}

// 3.8p1: whether lowering `node` begins the lifetime of an object something
// after it has to end.  The analysis wrote the answer on the node that produces
// the object, so this is one walk of the operands and never a second reading of
// what they mean.
bool LowirFunctionLowering::begins_lifetime(const DumpNode& node)
{
	const std::unordered_map<const DumpNode*, bool>::const_iterator found =
		begins_lifetime_.find(&node);
	if (found != begins_lifetime_.end())
	{
		return found->second;
	}
	bool answer = node.fact.destruction != nullptr;
	for (std::size_t index = 0; !answer && index < node.children.size(); ++index)
	{
		answer = begins_lifetime(*node.children[index]);
	}
	begins_lifetime_[&node] = answer;
	return answer;
}

LowValue LowirFunctionLowering::unary_expression(const DumpNode& node)
{
	const LowValue operand =
		expression(*node.children[0], node.fact.op == OP_AMP);
	LowValue value;
	value.type = node.fact.type;
	switch (node.fact.op)
	{
	case OP_AMP:
		// 5.3.1p3: the pointer to the object or function the operand names.
		value.operand = address_of(operand);
		return value;

	case OP_STAR:
	{
		// 5.3.1p1: the lvalue the pointer points to, which is that address.
		value.lvalue = true;
		value.operand = rvalue(operand);
		return value;
	}

	case OP_LNOT:
	{
		// 5.3.1p9 and 4.12: the operand is read as a value first, which is
		// where 4.2 and 4.3 make an array or a function the pointer that is
		// then compared with zero.
		const LowValue held = as_value(operand);
		Instruction instruction;
		instruction.kind = Instruction::IK_CMP;
		instruction.op = "eq";
		instruction.type = unit_.low_type(held.type);
		instruction.first = held.operand;
		instruction.second = literal_operand(held.type, 0);
		value.operand = emit(instruction);
		return value;
	}

	case OP_PLUS:
		// 5.3.1p7: unary `+` is its promoted operand.
		value.operand = converted(operand, value.type);
		return value;

	case OP_MINUS:
	case OP_COMPL:
	{
		Instruction instruction;
		instruction.kind = Instruction::IK_UNARY;
		instruction.op = node.fact.op == OP_MINUS ? "neg" : "bitnot";
		instruction.type = unit_.low_type(value.type);
		instruction.first = converted(operand, value.type);
		value.operand = emit(instruction);
		return value;
	}

	default:
		break;
	}
	throw std::runtime_error("a unary operator is outside the PA15 lowering "
	                         "subset");
}

LowValue LowirFunctionLowering::increment_expression(const DumpNode& node,
                                                     bool postfix)
{
	TypeTable& types = unit_.types();
	const LowValue operand = expression(*node.children[0], true);
	// 5.3.2p1 and 4.5p3: `++x` adds one to the value the operand holds, which
	// for a bit-field is a value of the type its width promotes it to.
	const TypeId bare = operand.field != nullptr
		? operand.field->bit_access
		: types.strip_cv(operand.type);
	const Operand before = rvalue(operand);
	Operand after;
	if (types.is_object_pointer(bare))
	{
		// 5.7p1: `++p` advances the pointer by one element, which is the
		// element's own size in bytes.
		LowValue one;
		one.type = types.fundamental(FT_INT);
		one.constant = true;
		one.value = 1;
		one.operand = literal_operand(one.type, 1);
		after = scaled_advance(before, one, types.target(bare),
		                       node.fact.op == OP_INC);
	}
	else
	{
		Instruction instruction;
		instruction.kind = Instruction::IK_BINARY;
		instruction.op = node.fact.op == OP_INC ? "add" : "sub";
		instruction.type = unit_.low_type(bare);
		instruction.first = before;
		instruction.second = literal_operand(bare, 1);
		after = emit(instruction);
	}
	if (operand.field != nullptr)
	{
		// 5.2.6p1 and 5.3.2p1: what the operand named is where the new value
		// goes.  A postfix `++` has already produced its own value and writes
		// back into the storage it read; a prefix one is the object itself, so
		// the object is named again for the write, which is what the
		// references write.
		if (postfix)
		{
			assign_bit_field(operand, after, bare);
			LowValue held;
			held.type = bare;
			held.operand = before;
			return held;
		}
		const LowValue again = expression(*node.children[0], true);
		assign_bit_field(again, after, bare);
		LowValue value = again;
		value.has_held = true;
		value.held = after;
		return value;
	}
	store(after, operand.operand, bare);
	LowValue value;
	if (postfix)
	{
		// 5.2.6p1: the value is the one the operand held before.
		value.type = bare;
		value.operand = before;
		return value;
	}
	value = operand;
	value.has_held = true;
	value.held = after;
	return value;
}

LowValue LowirFunctionLowering::binary_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const unsigned op = node.fact.op;
	if (op == OP_LAND || op == OP_LOR)
	{
		return logical_expression(node);
	}
	if (op == OP_COMMA)
	{
		// 5.18p1: the left operand is evaluated and discarded.
		expression(*node.children[0]);
		return expression(*node.children[1]);
	}
	const TypeId common = node.fact.operands;
	const LowValue left = expression(*node.children[0]);
	LowValue value;
	value.type = node.fact.type;
	if (types.is_object_pointer(types.strip_cv(common)) &&
	    (op == OP_PLUS || op == OP_MINUS))
	{
		// 5p4: the left operand is read where it is written, so what it is
		// worth is taken before the right operand runs - which is a difference
		// only where the right operand is something that runs at all, a call of
		// 12.3.2's conversion function among them.
		const LowValue held = as_value(left);
		const LowValue right = expression(*node.children[1]);
		value.operand = pointer_operation(op, held, right, common, value.type);
		return value;
	}
	// 5p4: each operand is read where it is written, and the conversions the
	// operator asks for are what it does with the two values it then has.
	const LowValue held = as_value(left);
	const LowValue right = as_value(expression(*node.children[1]));
	value.operand = binary_operation(op, held, right, common);
	return value;
}

unsigned LowirFunctionLowering::written_operator(unsigned op)
{
	switch (op)
	{
	case OP_PLUSASS: return OP_PLUS;
	case OP_MINUSASS: return OP_MINUS;
	case OP_STARASS: return OP_STAR;
	case OP_DIVASS: return OP_DIV;
	case OP_MODASS: return OP_MOD;
	case OP_XORASS: return OP_XOR;
	case OP_BANDASS: return OP_AMP;
	case OP_BORASS: return OP_BOR;
	case OP_LSHIFTASS: return OP_LSHIFT;
	case OP_RSHIFTASS: return OP_RSHIFT;
	default: break;
	}
	throw std::runtime_error("an assignment operator is outside the PA15 "
	                         "lowering subset");
}

Operand LowirFunctionLowering::scaled_advance(const Operand& address,
                                              const LowValue& count,
                                              TypeId element, bool forward)
{
	TypeTable& types = unit_.types();
	const TypeId wide = types.fundamental(FT_LONG_INT);
	const unsigned long long stride = types.object_size(element);
	Operand offset = converted(count, wide);
	if (stride != 1)
	{
		// 5.7p5: the distance is in elements, and LowIR indexes the bytes the
		// elements occupy.  An element that is one byte needs no scale.
		Instruction scale;
		scale.kind = Instruction::IK_BINARY;
		scale.op = "mul";
		scale.type = unit_.low_type(wide);
		scale.first = offset;
		scale.second = named_operand(Operand::OP_INTEGER, decimal(stride));
		offset = emit(scale);
	}
	if (!forward)
	{
		Instruction back;
		back.kind = Instruction::IK_BINARY;
		back.op = "sub";
		back.type = unit_.low_type(wide);
		back.first = named_operand(Operand::OP_INTEGER, "0");
		back.second = offset;
		offset = emit(back);
	}
	Instruction instruction;
	instruction.kind = Instruction::IK_INDEX;
	instruction.type.text = "i8";
	instruction.first = address;
	instruction.second = offset;
	return emit(instruction);
}

Operand LowirFunctionLowering::pointer_operation(unsigned op,
                                                 const LowValue& left,
                                                 const LowValue& right,
                                                 TypeId common, TypeId result)
{
	TypeTable& types = unit_.types();
	const TypeId pointer = types.strip_cv(common);
	const TypeId element = types.target(pointer);
	const bool left_addresses =
		types.is_object_pointer(types.strip_cv(left.type)) ||
		types.kind(types.strip_cv(left.type)) == TypeKind::Array;
	const bool right_addresses =
		types.is_object_pointer(types.strip_cv(right.type)) ||
		types.kind(types.strip_cv(right.type)) == TypeKind::Array;
	if (op == OP_MINUS && left_addresses && right_addresses)
	{
		// 5.7p6: the distance between two pointers, in elements.  It is the one
		// reading where *both* operands address an object and the operator is
		// `-`: 5.7p5 adds an integral operand to a pointer whichever side it
		// was written on, so `n + p` is that addition and not a subtraction of
		// the pointer from something.
		Instruction difference;
		difference.kind = Instruction::IK_BINARY;
		difference.op = "sub";
		difference.type.text = "ptr";
		difference.first = converted(left, common);
		difference.second = converted(right, common);
		const Operand bytes = emit(difference);
		const unsigned long long stride = types.object_size(element);
		if (stride == 1)
		{
			// The distance in elements of an element one byte wide is the
			// distance in bytes, which is what the subtraction already is.
			return bytes;
		}
		Instruction scale;
		scale.kind = Instruction::IK_BINARY;
		scale.op = "div";
		scale.type = unit_.low_type(result);
		scale.first = bytes;
		scale.second = named_operand(Operand::OP_INTEGER, decimal(stride));
		return emit(scale);
	}
	const LowValue& address = left_addresses ? left : right;
	const LowValue& count = left_addresses ? right : left;
	return scaled_advance(converted(address, common), count, element,
	                      op == OP_PLUS);
}

Operand LowirFunctionLowering::binary_operation(unsigned op,
                                                const LowValue& left,
                                                const LowValue& right,
                                                TypeId common)
{
	Instruction instruction;
	instruction.type = unit_.low_type(common);
	instruction.first = converted(left, common);
	instruction.second = converted(right, common);
	// 5.9 and 5.10: a floating comparison is the ordered one, which LowIR
	// spells with the same predicates a signed integer comparison uses.
	const bool sign = unit_.is_signed(common) ||
		unit_.types().is_floating(unit_.types().strip_cv(common));
	switch (op)
	{
	case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
		instruction.kind = Instruction::IK_CMP;
		instruction.op = op == OP_EQ ? "eq" : op == OP_NE ? "ne"
			: op == OP_LT ? (sign ? "lt" : "ult")
			: op == OP_GT ? (sign ? "gt" : "ugt")
			: op == OP_LE ? (sign ? "le" : "ule")
			: (sign ? "ge" : "uge");
		break;

	case OP_PLUS: instruction.kind = Instruction::IK_BINARY; instruction.op = "add"; break;
	case OP_MINUS: instruction.kind = Instruction::IK_BINARY; instruction.op = "sub"; break;
	case OP_STAR: instruction.kind = Instruction::IK_BINARY; instruction.op = "mul"; break;
	case OP_DIV:
		instruction.kind = Instruction::IK_BINARY;
		instruction.op = sign ? "div" : "udiv";
		break;
	case OP_MOD:
		instruction.kind = Instruction::IK_BINARY;
		instruction.op = sign ? "mod" : "umod";
		break;
	case OP_AMP: instruction.kind = Instruction::IK_BINARY; instruction.op = "and"; break;
	case OP_BOR: instruction.kind = Instruction::IK_BINARY; instruction.op = "or"; break;
	case OP_XOR: instruction.kind = Instruction::IK_BINARY; instruction.op = "xor"; break;
	case OP_LSHIFT: instruction.kind = Instruction::IK_BINARY; instruction.op = "shl"; break;
	case OP_RSHIFT:
		instruction.kind = Instruction::IK_BINARY;
		instruction.op = sign ? "shr" : "ushr";
		break;

	default:
		throw std::runtime_error("a binary operator is outside the PA15 "
		                         "lowering subset");
	}
	return emit(instruction);
}

LowValue LowirFunctionLowering::logical_expression(const DumpNode& node)
{
	// 5.14 and 5.15 where the value is named: the result is one canonical
	// integer, so a slot holds what each side decided and the uses of the
	// expression read it once.
	const bool conjunction = node.fact.op == OP_LAND;
	unsigned long long decided = 0;
	if (decided_logical(node, decided))
	{
		// 5.14p1: the right operand is not evaluated, so nothing it names is
		// part of the program.
		LowValue value;
		value.type = node.fact.type;
		value.constant = true;
		value.value = decided;
		value.operand = literal_operand(value.type, decided);
		return value;
	}
	lowir_model::LowType held;
	held.text = "i64";
	const std::string slot =
		add_generated_slot(conjunction ? "land" : "lor", held);
	const Operand storage = named_operand(Operand::OP_SLOT, slot);
	const std::string rhs_label = reserve_block(conjunction ? "land_rhs" : "lor_rhs");
	const std::string short_label =
		reserve_block(conjunction ? "land_short" : "lor_short");
	const std::string end_label =
		reserve_block(conjunction ? "land_end" : "lor_end");
	const LowValue left = expression(*node.children[0]);
	branch(truth_for_branch(left), conjunction ? rhs_label : short_label,
	       conjunction ? short_label : rhs_label);

	open_block(rhs_label);
	const LowValue right = expression(*node.children[1]);
	LowValue truth;
	truth.type = node.fact.type;
	truth.operand = truth_value(right);
	Instruction rhs_store;
	rhs_store.kind = Instruction::IK_STORE;
	rhs_store.type = held;
	rhs_store.first = truth.operand;
	rhs_store.second = storage;
	emit_void(rhs_store);
	// 12.2p3: a temporary the right operand created exists only on this path,
	// so this is where its lifetime ends - the short-circuit edge reaches an
	// object that was never built.
	leave_blocks(node);
	jump(end_label);

	open_block(short_label);
	Instruction short_store;
	short_store.kind = Instruction::IK_STORE;
	short_store.type = held;
	short_store.first = named_operand(Operand::OP_INTEGER, conjunction ? "0" : "1");
	short_store.second = storage;
	emit_void(short_store);
	jump(end_label);

	open_block(end_label);
	Instruction read;
	read.kind = Instruction::IK_LOAD;
	read.type = held;
	read.first = storage;
	LowValue value;
	value.type = node.fact.type;
	value.operand = emit(read);
	return value;
}

LowValue LowirFunctionLowering::assignment_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	if (node.fact.op == OP_ASS)
	{
		// 5.17p1: the value is converted to the type of the object it is
		// written into, and the result is that object.  The value is what the
		// assignment computes, so it is computed before the object it is
		// written into is named.
		const LowValue right = as_value(expression(*node.children[1]));
		const TypeId spelled = types.strip_cv(node.children[0]->fact.type);
		const TypeId written_type = types.is_reference(spelled)
			? types.strip_cv(types.target(spelled))
			: spelled;
		if (types.is_class(written_type))
		{
			// 12.8p15 over 5.17: the object written into holds what the right
			// operand's object holds, which is one copy of its bytes into the
			// storage that operand already names.
			const LowValue into = expression(*node.children[0], true);
			copy_class_object(address_of(into), address_of(right),
			                  written_type);
			return into;
		}
		// 5.17p1: the value is converted where it is computed, which is before
		// the object it is written into is named - the type it is converted to
		// is what the left operand was declared with, and reading it needs
		// nothing of the storage that operand will name.
		const Operand written = converted(right, written_type);
		const LowValue target = expression(*node.children[0], true);
		if (target.field != nullptr)
		{
			// 9.6p2: the object written into is a run of bits, and the bits
			// beside it in its storage unit are not this assignment's to change.
			// What is written is a value of the member's own type; the unit it
			// goes into is read and put back at the unit's own.
			assign_bit_field(target, written, target.field->bit_access);
			LowValue assigned = target;
			assigned.has_held = true;
			assigned.held = written;
			return assigned;
		}
		store(written, target.operand, written_type);
		LowValue assigned = target;
		assigned.has_held = true;
		assigned.held = written;
		return assigned;
	}
	const LowValue left = expression(*node.children[0], true);
	// 5.17p7: the operator acts on the value the left operand holds, which for
	// a bit-field is a value of the type the member was declared with - the
	// unit it was read out of is as wide as that type, so nothing is written to
	// read what the load produced as one.
	const TypeId bare = types.strip_cv(left.type);
	LowValue value = left;
	value.has_held = true;
	// 5.17p7: a compound assignment is the operator it names followed by an
	// assignment, with the left operand read once.
	const TypeId common = node.fact.operands;
	// 5p4: the left operand is read where it is written, which is before the
	// right operand runs.
	LowValue held;
	held.type = bare;
	held.operand = rvalue(left);
	const LowValue right = expression(*node.children[1]);
	const unsigned written_op = written_operator(node.fact.op);
	const Operand result =
		types.is_object_pointer(bare)
			? pointer_operation(written_op, held, right, bare, bare)
			: binary_operation(written_op, held, right, common);
	const Operand written = types.strip_cv(common) == bare ||
			types.is_object_pointer(bare)
		? result
		: convert_scalar(result, common, bare);
	if (left.field != nullptr)
	{
		// 9.6p2: what the operator computed is a value of the member's type,
		// and the unit it goes back into is read and put back at the unit's.
		assign_bit_field(left, written, left.field->bit_access);
		value.held = written;
		return value;
	}
	store(written, left.operand, bare);
	value.held = written;
	return value;
}

LowValue LowirFunctionLowering::conditional_expression(const DumpNode& node,
                                                       bool as_object)
{
	// 5.16: the two arms are alternatives, so each is lowered into the block
	// that reaches it and the result is read from the one slot both wrote.
	// Which of the two the slot holds - the value, or the object it names - is
	// what the use of the expression asks for.
	TypeTable& types = unit_.types();
	// 12.8p15: an object of class type is storage rather than a value a slot
	// can hold as one, so an arm of it is read as the object it names where the
	// conditional is an lvalue, and copied into an object of the conditional's
	// own where it is a prvalue.
	const bool class_typed = types.is_class(types.strip_cv(node.fact.type));
	const bool addressed =
		node.fact.category != ValueCategory::PRValue &&
		(as_object || class_typed ||
		 types.kind(types.strip_cv(node.fact.type)) == TypeKind::Array);
	if (class_typed && !addressed)
	{
		return conditional_object(node);
	}
	lowir_model::LowType held = addressed ? lowir_model::LowType()
	                                      : unit_.low_type(node.fact.type);
	if (addressed)
	{
		held.text = "ptr";
	}
	const std::string slot =
		add_generated_slot(addressed ? "condaddr" : "cond", held);
	const Operand storage = named_operand(Operand::OP_SLOT, slot);
	const std::string then_label =
		reserve_block(addressed ? "condaddr_then" : "cond_then");
	const std::string else_label =
		reserve_block(addressed ? "condaddr_else" : "cond_else");
	const std::string end_label =
		reserve_block(addressed ? "condaddr_end" : "cond_end");
	const LowValue condition = expression(*node.children[0]);
	branch(truth_for_branch(condition), then_label, else_label);

	for (unsigned arm = 0; arm < 2; ++arm)
	{
		open_block(arm == 0 ? then_label : else_label);
		const LowValue written = expression(*node.children[arm + 1], addressed);
		Instruction instruction;
		instruction.kind = Instruction::IK_STORE;
		instruction.type = held;
		instruction.first = addressed ? address_of(written)
		                              : converted(written, node.fact.type);
		instruction.second = storage;
		emit_void(instruction);
		end_conditional_arm(node, arm);
		jump(end_label);
	}

	open_block(end_label);
	Instruction read;
	read.kind = Instruction::IK_LOAD;
	read.type = held;
	read.first = storage;
	LowValue value;
	value.type = node.fact.type;
	value.operand = emit(read);
	value.lvalue = addressed;
	return value;
}

// 5.16 over a prvalue of class type: 12.2p1 makes the result an object, so the
// function holds one and each arm writes its own into it.  Only the arm control
// reached is written, which is what the two blocks say.
LowValue LowirFunctionLowering::conditional_object(const DumpNode& node,
                                                   const Operand* into)
{
	const TypeId type = unit_.types().strip_cv(node.fact.type);
	const std::string then_label = reserve_block("condobj_then");
	const std::string else_label = reserve_block("condobj_else");
	const std::string end_label = reserve_block("condobj_end");
	// 5.16p3 and 12.8p31: the object the conditional is worth is the result
	// object each of its operands initializes, so where a place asked for that
	// object both arms initialize it there and no object of the conditional's
	// own stands between them.  Where no place asked, the function gives it one
	// - named after whatever did ask for the object, which is the name the
	// analysis wrote on it, and after the conditional itself where nothing did.
	const Operand at = into != nullptr
		? *into
		: open_object_slot(type, node.fact.spelling.empty()
		                             ? "condobj"
		                             : node.fact.spelling.c_str());
	LowValue held;
	held.type = type;
	held.lvalue = true;
	held.operand = at;
	const LowValue condition = expression(*node.children[0]);
	branch(truth_for_branch(condition), then_label, else_label);
	for (unsigned arm = 0; arm < 2; ++arm)
	{
		open_block(arm == 0 ? then_label : else_label);
		place_class_object(at, type, *node.children[arm + 1]);
		end_conditional_arm(node, arm);
		jump(end_label);
	}
	open_block(end_label);
	return held;
}

// 12.2p3 and 5.16p1: the temporaries the arm control took created, destroyed
// where that arm ends.  The other arm never made them, so the end of the
// conditional is no place to write these: it is reached both ways.
void LowirFunctionLowering::end_conditional_arm(const DumpNode& node,
                                                unsigned arm)
{
	const FactKind wanted = arm == 0 ? FactKind::Then : FactKind::Else;
	for (std::size_t index = 3; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind == wanted)
		{
			leave_blocks(*node.children[index]);
			return;
		}
	}
}

void LowirFunctionLowering::discarded_conditional(const DumpNode& node)
{
	const std::string then_label = reserve_block("discard_cond_then");
	const std::string else_label = reserve_block("discard_cond_else");
	const std::string end_label = reserve_block("discard_cond_end");
	const LowValue condition = expression(*node.children[0]);
	branch(truth_for_branch(condition), then_label, else_label);
	for (unsigned arm = 0; arm < 2; ++arm)
	{
		open_block(arm == 0 ? then_label : else_label);
		expression(*node.children[arm + 1]);
		if (!terminated())
		{
			end_conditional_arm(node, arm);
			jump(end_label);
		}
	}
	open_block(end_label);
}

bool LowirFunctionLowering::decided_logical(const DumpNode& node,
                                            unsigned long long& value)
{
	if (node.children.empty() || !node.children[0]->fact.constant ||
	    !node.children[0]->fact.spelling.empty())
	{
		return false;
	}
	const bool conjunction = node.fact.op == OP_LAND;
	const bool truth = node.children[0]->fact.value != 0;
	if (conjunction == truth)
	{
		return false;
	}
	value = truth ? 1 : 0;
	return true;
}

LowValue LowirFunctionLowering::subscript_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	// 5.2.1p1: the pointer operand is the one the dump wrote first, and it is
	// brought to a pointer where it stands rather than after the subscript has
	// been read.
	const LowValue base = expression(*node.children[0], true);
	const Operand address =
		types.kind(types.strip_cv(base.type)) == TypeKind::Array ? decay(base)
		                                                         : rvalue(base);
	const LowValue offset = expression(*node.children[1]);
	Instruction instruction;
	instruction.kind = Instruction::IK_INDEX;
	instruction.index_projection = lowir_model::IPK_ARRAY_ELEMENT;
	instruction.type = unit_.low_type(node.fact.type);
	instruction.first = address;
	// 5.2.1p1: the subscript is one expression however the element is reached,
	// so it is read once here and the byte count below counts what it holds.
	instruction.second = rvalue(offset);
	if (instruction.type.text.compare(0, 4, "obj<") == 0)
	{
		// An element with no register form is not a width LowIR indexes by, so
		// the subscript counts the bytes 5.2.1p1 says the element occupies.
		// An element that is one byte is already counted in them.
		const unsigned long long stride =
			types.object_size(types.strip_cv(node.fact.type));
		instruction.type.text = "i8";
		if (stride != 1)
		{
			Instruction scale;
			scale.kind = Instruction::IK_BINARY;
			scale.op = "mul";
			scale.type.text = "i64";
			scale.first = instruction.second;
			scale.second =
				named_operand(Operand::OP_INTEGER, decimal(stride));
			instruction.second = emit(scale);
		}
	}
	LowValue value;
	value.type = node.fact.type;
	value.lvalue = true;
	value.named = true;
	value.operand = emit(instruction);
	return value;
}

