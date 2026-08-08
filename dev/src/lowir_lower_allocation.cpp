#include "lowir_lower.h"

#include <sstream>
#include <stdexcept>

#include "sema_scope.h"

// 5.3.4 and 5.3.5 in a function body: where the storage an object stands in
// came from, and who gives it back.
//
// This is the lowering half of the seam `sema_allocation.cpp` already owns in
// the analysis: a new-expression and a delete-expression are ordinary
// construction and destruction actions over storage the expression names, and
// not a second object model.  What is written here is only the storage - the
// call of the allocation function, 5.3.4p6's count in front of an array of
// class type, 8.5p7's zero over the extent it obtained, 15.2p2's cleanup that
// gives the storage back when an element's constructor throws, and 5.3.5p9's
// deallocation - with every object built or ended through the same
// `constructor_call` and `destruction_step` a declaration reaches.

namespace {

using lowir_model::Instruction;
using lowir_model::Operand;
using lowir_model::Parameter;

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

// 5.3.4p8 and 5.3.4p12: the allocation function is called for the bytes the
// object needs, and the object is created at what it returned.  Every other
// object of class type this lowering writes stands in storage a name reaches -
// a slot, a global, a subobject of one - and this one stands at a value, so the
// address the call produced is passed to the same construction rather than an
// address being taken around a name.
LowValue LowirFunctionLowering::new_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	if (node.fact.array_form)
	{
		return array_new_expression(node);
	}
	const LowValue allocated = expression(*node.children[0]);
	LowValue value;
	value.type = node.fact.type;
	value.operand = allocated.operand;
	if (node.children.size() < 2)
	{
		// 8.5p16: the new-initializer was left out for an object of no class
		// type, which 8.5p6 default-initializes by performing no
		// initialization at all.
		return value;
	}
	const DumpNode& initialization = *node.children[1];
	// 5.3.4p15: an allocation function that says it obtained no storage by
	// handing back null leaves the object at that address uninitialized, which
	// the analysis settled once where it chose the function.
	const bool guarded = node.fact.may_fail;
	std::string done;
	if (guarded)
	{
		const std::string live = reserve_block("new_init");
		done = reserve_block("new_end");
		Instruction test;
		test.kind = Instruction::IK_CMP;
		test.op = "ne";
		test.type.text = "ptr";
		test.first = allocated.operand;
		test.second = named_operand(Operand::OP_INTEGER, "0");
		branch(emit(test), live, done);
		open_block(live);
	}
	if (initialization.fact.kind == FactKind::ConstructorAction)
	{
		// 12.1p5: the object's lifetime begins with the call of its
		// constructor, which is written even where the constructor does
		// nothing, because the call is the only mark that it has.
		constructor_call(allocated.operand, initialization, true, kNoType, true);
	}
	else if (types.is_class(types.strip_cv(types.target(node.fact.type))))
	{
		// 12.8p31: the initializer creates its own object, and 5.3.4p12's
		// storage is what this expression owns - so it creates it there, the
		// same hand-off a declaration makes to the initializer written on it.
		place_class_object(allocated.operand,
		                   types.strip_cv(types.target(node.fact.type)),
		                   initialization);
	}
	else
	{
		const TypeId created = types.target(node.fact.type);
		const LowValue held = expression(initialization);
		store(converted(held, created), allocated.operand, created);
	}
	if (guarded)
	{
		jump(done);
		open_block(done);
	}
	return value;
}

// 5.3.4p1's array form: one call of the allocation function for every element
// at once, the count 5.3.5p2 will need written in front of them, and 12.6p1's
// construction given to each of them by one loop.
//
// The bytes the call asked for are the one thing about the allocation the
// expression still needs after it has returned - the count is derived from
// them, and 8.5p7's zero covers exactly them - so where they are not a number
// the translation knows, the function keeps them in an object of its own.
LowValue LowirFunctionLowering::array_new_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const TypeId counter = types.fundamental(FT_LONG_INT);
	const TypeId created = types.strip_cv(types.target(node.fact.type));
	std::vector<TypeId> dimensions;
	std::vector<unsigned long long> bounds;
	array_dimensions(created, dimensions, bounds);
	const TypeId element = dimensions.empty()
		? created
		: types.strip_cv(types.target(dimensions.back()));
	const unsigned long long stride = types.object_size(element);
	// The ABI writes the count in front of an array of class type and nothing
	// else, which is what the analysis asked the allocation function for.
	const bool cookie = types.is_class(element);
	const DumpNode* action = nullptr;
	const DumpNode* release = nullptr;
	for (std::size_t index = 1; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		if (child.fact.kind == FactKind::ConstructorAction)
		{
			action = &child;
		}
		else if (child.fact.kind == FactKind::Callee)
		{
			release = &child;
		}
	}
	const bool keeps_bytes =
		!node.fact.counted && (cookie || node.fact.zero_initialized);
	// 5.3.4p15: the address is tested before anything is written at it, so what
	// the expression is worth has to be settled on both edges of that test.
	// Where the count stands in front of the elements the value is a step past
	// the address the call handed back, and that step is under the test - so it
	// is held in an object of its own and left null where the call obtained
	// nothing.  Where there is no count the value *is* what the call returned,
	// which needs no object to survive in.
	const std::string result = node.fact.may_fail && cookie
		? add_generated_slot("array_new_result", unit_.low_type(node.fact.type))
		: std::string();
	const bool guarded = node.fact.may_fail;
	std::string kept;
	const LowValue allocated =
		call_expression(*node.children[0], nullptr, keeps_bytes ? &kept : nullptr);
	LowValue value;
	value.type = node.fact.type;
	value.operand = allocated.operand;
	std::string done;
	if (guarded)
	{
		if (!result.empty())
		{
			store(named_operand(Operand::OP_INTEGER, "0"),
			      named_operand(Operand::OP_SLOT, result), node.fact.type);
		}
		const std::string live = reserve_block("new_init");
		done = reserve_block("new_end");
		Instruction test;
		test.kind = Instruction::IK_CMP;
		test.op = "ne";
		test.type.text = "ptr";
		test.first = allocated.operand;
		test.second = named_operand(Operand::OP_INTEGER, "0");
		branch(emit(test), live, done);
		open_block(live);
	}
	Operand bytes;
	if (!kept.empty())
	{
		bytes = load(named_operand(Operand::OP_SLOT, kept), counter);
	}
	if (cookie)
	{
		Instruction move;
		move.kind = Instruction::IK_INDEX;
		move.type.text = "i8";
		move.first = allocated.operand;
		move.second =
			named_operand(Operand::OP_INTEGER, decimal(kArrayCookieBytes));
		value.operand = emit(move);
		store(array_element_count(node, bytes, stride), allocated.operand,
		      counter);
	}
	if (!result.empty())
	{
		store(value.operand, named_operand(Operand::OP_SLOT, result),
		      node.fact.type);
	}
	if (node.fact.zero_initialized)
	{
		// 8.5p7: the elements are value-initialized, which begins with the zero
		// of the storage they stand in - the bytes the allocation obtained,
		// less the count 5.3.4p1 wrote in front of them.
		zero_new_elements(node, value.operand, bytes, stride, element,
		                  cookie != 0);
	}
	if (action != nullptr)
	{
		construct_array_new_run(node, *action, release, allocated.operand,
		                        value.operand,
		                        array_element_count(node, bytes, stride), stride,
		                        element);
	}
	if (guarded)
	{
		jump(done);
		open_block(done);
		if (!result.empty())
		{
			value.operand = load(named_operand(Operand::OP_SLOT, result),
			                     node.fact.type);
		}
	}
	return value;
}

// 8.5p7 over the elements a new-expression's array form created: the zero of
// the storage they stand in.
//
// An object of class type is zeroed the way every other object of one is, by
// the spans `zero_object` writes over what the type occupies, so a class array
// costs the same zero one object of it does times its count.  An array of no
// class type is 8.5p7's zero of storage and nothing about any object, which
// LowIR names with one `zeroinit` over the extent.  Either way an extent the
// translation does not know is a loop over the bytes the call was asked for,
// which is the one place that number survives it.
void LowirFunctionLowering::zero_new_elements(const DumpNode& node,
                                              const Operand& data,
                                              const Operand& bytes,
                                              unsigned long long stride,
                                              TypeId element, bool cookie)
{
	TypeTable& types = unit_.types();
	const TypeId counter = types.fundamental(FT_LONG_INT);
	if (!node.fact.counted)
	{
		if (!cookie)
		{
			zero_storage_loop(data, bytes);
			return;
		}
		Instruction without;
		without.kind = Instruction::IK_BINARY;
		without.op = "sub";
		without.type = unit_.low_type(counter);
		without.first = bytes;
		without.second =
			named_operand(Operand::OP_INTEGER, decimal(kArrayCookieBytes));
		zero_storage_loop(data, emit(without));
		return;
	}
	const unsigned long long span = node.fact.elements * stride;
	if (span == 0)
	{
		return;
	}
	if (types.is_class(element))
	{
		zero_span(data, span, types.object_align(element));
		return;
	}
	Instruction zero;
	zero.kind = Instruction::IK_ZEROINIT;
	zero.byte_count = static_cast<std::size_t>(span);
	zero.byte_alignment = static_cast<std::size_t>(types.object_align(element));
	zero.first = data;
	emit_void(zero);
}

// 5.3.4p1 and the ABI: how many elements the array holds.  A count the
// translation knows is that number; one it does not is what is left of the
// bytes the allocation function was asked for once the count in front of the
// elements is taken off, which is the one place that number survives the call.
Operand LowirFunctionLowering::array_element_count(const DumpNode& node,
                                                   const Operand& bytes,
                                                   unsigned long long stride)
{
	TypeTable& types = unit_.types();
	const TypeId counter = types.fundamental(FT_LONG_INT);
	if (node.fact.counted)
	{
		// 5.3.3p6: the number is one the translation computed rather than one
		// the program wrote, and LowIR names such a value with `const`.
		Instruction known;
		known.kind = Instruction::IK_CONST;
		known.type = unit_.low_type(counter);
		known.first = literal_operand(counter, node.fact.elements);
		return emit(known);
	}
	Instruction without;
	without.kind = Instruction::IK_BINARY;
	without.op = "sub";
	without.type = unit_.low_type(counter);
	without.first = bytes;
	without.second =
		named_operand(Operand::OP_INTEGER, decimal(kArrayCookieBytes));
	Instruction divided;
	divided.kind = Instruction::IK_BINARY;
	divided.op = "udiv";
	divided.type = unit_.low_type(counter);
	divided.first = emit(without);
	divided.second = named_operand(Operand::OP_INTEGER, decimal(stride));
	return emit(divided);
}

// 8.5p7 over storage a value says the size of: every byte of it set to zero.
void LowirFunctionLowering::zero_storage_loop(const Operand& data,
                                              const Operand& bytes)
{
	TypeTable& types = unit_.types();
	const TypeId counter = types.fundamental(FT_LONG_INT);
	const TypeId byte = types.fundamental(FT_SIGNED_CHAR);
	const std::string cond = reserve_block("zeroinit_cond");
	const std::string body = reserve_block("zeroinit_body");
	const std::string end = reserve_block("zeroinit_end");
	const std::string index =
		add_generated_slot("zeroinit_offset", unit_.low_type(counter));
	const Operand cursor = named_operand(Operand::OP_SLOT, index);
	store(named_operand(Operand::OP_INTEGER, "0"), cursor, counter);
	jump(cond);
	open_block(cond);
	const Operand at = load(cursor, counter);
	Instruction test;
	test.kind = Instruction::IK_CMP;
	test.op = "ult";
	test.type = unit_.low_type(counter);
	test.first = at;
	test.second = bytes;
	branch(emit(test), body, end);
	open_block(body);
	Instruction move;
	move.kind = Instruction::IK_INDEX;
	move.type.text = "i8";
	move.first = data;
	move.second = at;
	store(literal_operand(byte, 0), emit(move), byte);
	Instruction next;
	next.kind = Instruction::IK_BINARY;
	next.op = "add";
	next.type = unit_.low_type(counter);
	next.first = at;
	next.second = named_operand(Operand::OP_INTEGER, "1");
	store(emit(next), cursor, counter);
	jump(cond);
	open_block(end);
}

// 12.6p1 over the elements a new-expression created: the one constructor every
// element is given, run over the count the allocation carries.
//
// 15.2p2 is what the handler is for: an exception out of one element leaves the
// ones before it standing, so the loop's own index says how many those are, and
// 5.3.4p18 gives the storage they stood in back through the deallocation
// function that pairs with the allocation this expression made.
void LowirFunctionLowering::construct_array_new_run(const DumpNode& node,
                                                    const DumpNode& action,
                                                    const DumpNode* release,
                                                    const Operand& storage,
                                                    const Operand& data,
                                                    const Operand& count,
                                                    unsigned long long stride,
                                                    TypeId element)
{
	TypeTable& types = unit_.types();
	const TypeId counter = types.fundamental(FT_LONG_INT);
	const DumpNode& call = *action.children[0];
	const SemaEntity& constructor = *call.children[0]->fact.entity;
	const SemaEntity* const destructor = subobject_destructor(constructor);
	const std::string cond = reserve_block("array_new_ctor_cond");
	const std::string body = reserve_block("array_new_ctor_body");
	const std::string end = reserve_block("array_new_ctor_end");
	const std::string cleanup = reserve_block("array_new_ctor_cleanup");
	const std::string past = reserve_block("array_new_ctor_cont");
	const std::string index =
		add_generated_slot("array_new_index", unit_.low_type(counter));
	const Operand cursor = named_operand(Operand::OP_SLOT, index);
	store(named_operand(Operand::OP_INTEGER, "0"), cursor, counter);
	jump(cond);
	open_block(cond);
	const Operand at = load(cursor, counter);
	Instruction test;
	test.kind = Instruction::IK_CMP;
	test.op = "ult";
	test.type = unit_.low_type(counter);
	test.first = at;
	test.second = count;
	branch(emit(test), body, end);
	open_block(body);
	// 8.5p7's zero, where the element was value-initialized, is one element's
	// worth of bytes and not the array's - which is what `element` says.
	const Operand address = element_at_value(data, at, stride);
	emit_handler(false, cleanup);
	constructor_call(address, action, false, element);
	emit_handler_end();
	Instruction next;
	next.kind = Instruction::IK_BINARY;
	next.op = "add";
	next.type = unit_.low_type(counter);
	next.first = at;
	next.second = named_operand(Operand::OP_INTEGER, "1");
	store(emit(next), cursor, counter);
	jump(cond);
	open_block(end);
	jump(past);
	open_block(cleanup);
	const Operand built = load(cursor, counter);
	if (destructor != nullptr)
	{
		destroy_array_loop(data, built, stride, *destructor, false, false,
		                   nullptr);
	}
	if (release != nullptr)
	{
		deallocation_call(*release, storage, types.object_size(element));
	}
	for (std::size_t live = unwind_live_.size(); live-- > 0;)
	{
		// 15.2p2: this block is the whole of what an exception out of an
		// element's constructor leaves by, so what it owes is the elements this
		// expression built and 5.3.4p18's storage *and* every object standing in
		// the function around it - in the reverse of the order they began, which
		// is what the handler 12.6p1's array of a subobject writes owes too.
		replay_unwind(unwind_live_[live]);
	}
	emit_resume();
	unwind_dispatch_.clear();
	open_block(past);
	(void)node;
}

// 5.3.5: the end of the lifetime of the object the operand points to, and the
// return of the storage it stood in.
//
// 5.3.5p2 says nothing happens where the pointer is null, and for an object of
// class type there is something for that to be about: the destructor runs on
// the object, and 5.3.4p1's count stands in front of an array of one.  For
// every other type the deallocation function is the whole of what the
// expression does, and 3.7.4.2p3 already says a null pointer leaves it nothing
// to do - so the test is written where there is something to guard and nowhere
// else.
LowValue LowirFunctionLowering::delete_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const TypeId counter = types.fundamental(FT_LONG_INT);
	// 5.3.5p2: what this expression is about is the object it destroys, whose
	// type the node carries as the one it was written over.
	const TypeId destroyed = types.strip_cv(node.fact.spelled);
	// 5.3.5p5: a class the unit never completed says neither what one object of
	// it occupies nor what ending its lifetime comes to, so there is nothing
	// under the test and the storage goes back the way any other does.
	const bool incomplete = types.is_incomplete(destroyed);
	const bool complete = types.is_class(destroyed) && !incomplete;
	const unsigned long long stride =
		incomplete ? 0 : types.object_size(destroyed);
	const DumpNode* release = nullptr;
	const DumpNode* ends = nullptr;
	for (std::size_t index = 1; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		if (child.fact.kind == FactKind::DestructorAction)
		{
			ends = &child;
		}
		else if (child.fact.kind == FactKind::Callee)
		{
			release = &child;
		}
	}
	const LowValue operand = expression(*node.children[0]);
	const Operand pointer = rvalue(operand);
	LowValue value;
	value.type = node.fact.type;
	if (!complete)
	{
		if (release != nullptr)
		{
			deallocation_call(*release, pointer, stride);
		}
		return value;
	}
	const char* const guard = node.fact.array_form ? "array_delete_nonnull"
	                                               : "delete_nonnull";
	const char* const after =
		node.fact.array_form ? "array_delete_end" : "delete_end";
	const std::string live = reserve_block(guard);
	const std::string done = reserve_block(after);
	Instruction test;
	test.kind = Instruction::IK_CMP;
	test.op = "ne";
	test.type.text = "ptr";
	test.first = pointer;
	test.second = named_operand(Operand::OP_INTEGER, "0");
	branch(emit(test), live, done);
	open_block(live);
	Operand storage = pointer;
	if (node.fact.array_form)
	{
		// 5.3.5p2 and the ABI: the operand points at the first element, and the
		// storage the allocation handed back begins at the count written in
		// front of it - which is also how many lifetimes this expression ends.
		Instruction back;
		back.kind = Instruction::IK_INDEX;
		back.type.text = "i8";
		back.first = pointer;
		back.second = named_operand(Operand::OP_INTEGER,
		                            "-" + decimal(kArrayCookieBytes));
		storage = emit(back);
		const Operand held = load(storage, counter);
		if (ends != nullptr)
		{
			destroy_array_loop(pointer, held, stride, *ends->fact.entity, false,
			                   false, nullptr, "array_delete_dtor",
			                   "array_delete_index");
		}
	}
	else if (ends != nullptr && ends->fact.entity->virtual_function)
	{
		// 3.2p2 and 5.3.5p8: the expression names the destructor whether or not
		// the call written for it is the one the table holds, so the definition
		// it names is one this unit owes exactly as a direct call would be.
		unit_.declare_call_target(*ends->fact.entity, false);
		// 5.3.5p3: the object's dynamic type may be a class derived from the one
		// the operand points to, and only that class knows what ends its own
		// lifetime and which deallocation function 5.3.5p9 chose in it.  Both
		// stand behind one entry of its table, so this expression is that one
		// indirect call and nothing else.
		Instruction out;
		out.kind = Instruction::IK_CALL;
		out.type.text = "void";
		out.has_call_signature = true;
		out.first =
			dispatch_slot(pointer, ends->fact.entity->vtable_index + 1);
		out.args.push_back(pointer);
		Parameter self;
		self.name = "arg0";
		self.type.text = "ptr";
		out.call_params.push_back(self);
		out.call_return_type.text = "void";
		emit_void(out);
	}
	else if (ends != nullptr)
	{
		unit_.declare_call_target(*ends->fact.entity, false);
		Instruction out;
		out.kind = Instruction::IK_CALL;
		out.type.text = "void";
		out.first = named_operand(
			Operand::OP_GLOBAL, unit_.function_symbol(*ends->fact.entity, false));
		out.args.push_back(pointer);
		emit_void(out);
	}
	if (release != nullptr &&
	    !(ends != nullptr && !node.fact.array_form &&
	      ends->fact.entity->virtual_function))
	{
		deallocation_call(*release, storage, stride);
	}
	jump(done);
	open_block(done);
	return value;
}

// 3.7.4.2p2 and 12.5p4: the call that gives the storage back.  The one the
// analysis chose takes the storage alone or the storage and the size of the
// object that stood in it, and which of the two it is, is what its declaration
// says rather than a second fact this expression carries.
void LowirFunctionLowering::deallocation_call(const DumpNode& callee,
                                              const Operand& storage,
                                              unsigned long long bytes)
{
	deallocation_call(*callee.fact.entity, storage, bytes);
}

void LowirFunctionLowering::deallocation_call(const SemaEntity& release,
                                              const Operand& storage,
                                              unsigned long long bytes)
{
	TypeTable& types = unit_.types();
	const SemaEntity& entity = release;
	unit_.declare_entity(entity);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type.text = "void";
	out.first = named_operand(Operand::OP_GLOBAL, unit_.function_symbol(entity));
	out.args.push_back(storage);
	const std::vector<TypeId>& parameters = types.parameters(entity.type);
	if (parameters.size() > 1)
	{
		out.args.push_back(literal_operand(parameters[1], bytes));
	}
	emit_void(out);
}
