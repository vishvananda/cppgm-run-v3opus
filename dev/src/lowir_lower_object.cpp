#include "lowir_lower.h"

#include <sstream>
#include <stdexcept>

#include "sema_scope.h"

// The object model of the PA16 lowering: what one object of class type costs a
// function body.
//
// 12.1p5 and 12.4p3 make the beginning and the end of a lifetime a call, 12.2p1
// makes a prvalue of class type an object the function has to hold, 12.6.2
// makes one subobject of the object a constructor is initializing an action of
// its own, and 8.5.1 makes an aggregate the subobjects its clauses reached.
// All four name a subobject in the same words - the object it is part of, and
// the member or element it is - so they are written together and read the same
// `LowObject` to say where the storage is.
//
// Nothing here re-resolves a name or reads syntax: the resolved tree already
// names the constructor, the destructor and the member each action acts on.

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

// How many bytes of value-initialization are still written as the elements
// they are.  Beyond this the elements stop being what the reader wants to see
// and the storage starts being it, and a span written by hand would also grow
// with a bound the source only wrote a number for.
const unsigned long long kZeroSpanLimit = 64;

// 12.4p8 and 15.2p2: how many destructions after a destructor's body are still
// written as the destructions they are.  Each of them needs the ones behind it
// as its own cleanup, which is n(n+1)/2 calls; beyond this the same order is
// written as a chain of n blocks, each running one destruction and entering the
// next, because past here the calls stop being a description of the object and
// the order starts being it.
const std::size_t kUnwindSuffixLimit = 16;

}  // namespace

void LowirFunctionLowering::add_initialization(const Operand& storage,
                                               TypeId type,
                                               const DumpNode& node)
{
	TypeTable& types = unit_.types();
	if (node.fact.kind == FactKind::ConstructorAction &&
	    types.kind(types.strip_cv(type)) == TypeKind::Array)
	{
		// 12.6p1: the object with static storage duration is an array, so what
		// runs before the program is the constructor of each of its elements.
		array_lifecycle(node, true);
		return;
	}
	if (node.fact.kind == FactKind::ConstructorAction)
	{
		// 3.6.2p2 and 12.1p5: the object is initialized by calling its
		// constructor on it, which is the one action its initialization is.
		// Whether that call comes to anything is the call's own question and is
		// asked there rather than a second time here: 12.1p5's "there is
		// nothing to do" is true of a trivial default constructor, and 12.8p12's
		// trivial copy still carries the bytes of the object it was given.
		// The object has a name of the program image, so where the call wrote
		// nothing the address named for it is no part of the program either.
		LowValue object;
		object.type = type;
		object.lvalue = true;
		object.operand = storage;
		const std::size_t block = current_;
		const std::size_t named = out_.blocks[block].instructions.size();
		const unsigned counted = temps_;
		constructor_call(address_of(object), node);
		if (current_ == block &&
		    out_.blocks[block].instructions.size() == named + 1)
		{
			out_.blocks[block].instructions.pop_back();
			temps_ = counted;
		}
		return;
	}
	if (types.is_reference(type))
	{
		const LowValue bound = expression(node, true);
		store(address_of(bound), storage, type);
		return;
	}
	if (types.kind(types.strip_cv(type)) == TypeKind::Array &&
	    (node.fact.kind == FactKind::BracedInitList ||
	     node.fact.kind == FactKind::AggregateInitialization))
	{
		// 3.6.2p2 and 8.5.1p1: the clauses initialize the elements, and an
		// element of a namespace-scope array is named the way the program would
		// name it - the array, moved by whole elements - rather than by a
		// cursor counting bytes from the front of storage the program image
		// already holds.
		const TypeId array = types.strip_cv(type);
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			LowObject at;
			at.storage = storage;
			at.elements.push_back(LowObject::ElementStep(array, index));
			if (node.children[index]->fact.kind == FactKind::ConstructorAction)
			{
				const unsigned long long run =
					node.children[index]->fact.elements;
				if (run > 1)
				{
					// 8.5.1p7: the elements from here to the end of the array
					// are all built by that same one call, which is the loop
					// the call is and not one call per element.
					construct_element_run(*node.children[index],
					                      object_address(at), run,
					                      types.object_size(
						                      types.strip_cv(types.target(array))),
					                      types.strip_cv(types.target(array)),
					                      UnwindMark());
					break;
				}
				// 12.6p1: the element is one object of its class, built by the
				// constructor 8.5 chose for it before the program runs.
				constructor_call(object_address(at), *node.children[index]);
				continue;
			}
			initialize_into(at, types.target(array), *node.children[index]);
		}
		return;
	}
	initialize(storage, type, node);
}

// 5.2.1p1 and 8.3.4p6: the address of one element of the array at `array`.
// The array is read as the pointer to its first element that 4.2 makes it, and
// the element is that pointer moved by whole elements - which LowIR spells by
// the width of an element where an element has one, and by the bytes 5.2.1p1
// counts where it does not.
Operand LowirFunctionLowering::array_element(const Operand& array,
                                             TypeId array_type,
                                             unsigned long long index)
{
	Instruction decayed;
	decayed.kind = Instruction::IK_UNARY;
	decayed.op = "decay";
	decayed.type.text = "ptr";
	decayed.first = array;
	return element_step(emit(decayed), array_type, index);
}

// One dimension of that walk: the pointer already stands at the first element
// of `array_type`, and this moves it to the one `index` names.  A dimension of
// a multi-dimensional array is one such step, taken from the outermost in, the
// way 5.2.1p1 reads the subscripts the source would write.
Operand LowirFunctionLowering::element_step(const Operand& cursor,
                                            TypeId array_type,
                                            unsigned long long index)
{
	TypeTable& types = unit_.types();
	return element_of_step(cursor, types.target(types.strip_cv(array_type)),
	                       index);
}

// The same step named by what an element is rather than by what the array is,
// which is what a subobject path holds: 8.5.1p1 says which element of the array
// a clause reached, and the element's own type is what says how wide the step
// to it is.
Operand LowirFunctionLowering::element_of_step(const Operand& cursor,
                                               TypeId element,
                                               unsigned long long index)
{
	TypeTable& types = unit_.types();
	Instruction move;
	move.kind = Instruction::IK_INDEX;
	move.index_projection = lowir_model::IPK_ARRAY_ELEMENT;
	move.type = unit_.low_type(element);
	move.first = cursor;
	move.second = named_operand(Operand::OP_INTEGER, decimal(index));
	const unsigned long long stride = types.object_size(types.strip_cv(element));
	if (move.type.text.compare(0, 4, "obj<") == 0)
	{
		move.type.text = "i8";
		if (stride != 1)
		{
			// An element with no register form is not a width LowIR indexes by,
			// so the step counts the bytes an element occupies.  One byte is
			// already what `i8` counts.
			Instruction scale;
			scale.kind = Instruction::IK_BINARY;
			scale.op = "mul";
			scale.type.text = "i64";
			scale.first = named_operand(Operand::OP_INTEGER, decimal(index));
			scale.second = named_operand(Operand::OP_INTEGER, decimal(stride));
			move.second = emit(scale);
		}
	}
	return emit(move);
}

// 8.3.4p1: the dimensions of an array type, outermost first, and how many
// objects it holds altogether.  An element of a multi-dimensional array is
// reached one dimension at a time, the way the subscripts the source would
// write read it, so the walk is the same one for every place that names one.
unsigned long long LowirFunctionLowering::array_dimensions(
	TypeId type, std::vector<TypeId>& dimensions,
	std::vector<unsigned long long>& bounds)
{
	TypeTable& types = unit_.types();
	unsigned long long total = 1;
	for (TypeId at = types.strip_cv(type); types.kind(at) == TypeKind::Array;
	     at = types.strip_cv(types.target(at)))
	{
		const unsigned long long stride =
			types.object_size(types.strip_cv(types.target(at)));
		const unsigned long long bound =
			stride == 0 ? 0 : types.object_size(at) / stride;
		dimensions.push_back(at);
		bounds.push_back(bound);
		total *= bound;
	}
	return total;
}

// The address of the `flat`th object those dimensions hold, counted the way the
// storage lays them out: the last dimension moves fastest.
Operand LowirFunctionLowering::element_at(
	const Operand& array, const std::vector<TypeId>& dimensions,
	const std::vector<unsigned long long>& bounds, unsigned long long flat)
{
	Instruction decayed;
	decayed.kind = Instruction::IK_UNARY;
	decayed.op = "decay";
	decayed.type.text = "ptr";
	decayed.first = array;
	Operand at = emit(decayed);
	unsigned long long left = flat;
	for (std::size_t depth = 0; depth < dimensions.size(); ++depth)
	{
		unsigned long long below = 1;
		for (std::size_t inner = depth + 1; inner < bounds.size(); ++inner)
		{
			below *= bounds[inner];
		}
		at = element_step(at, dimensions[depth], below == 0 ? 0 : left / below);
		left = below == 0 ? 0 : left % below;
	}
	return at;
}

// 12.6p1 and 12.4p8: the action the array names, run on each of its elements.
// 12.6p1 creates them in increasing order; a subobject is destroyed in the
// reverse of the order it was created in, which the action says.
void LowirFunctionLowering::array_lifecycle(const DumpNode& node,
                                            bool construct)
{
	TypeTable& types = unit_.types();
	std::vector<TypeId> dimensions;
	std::vector<unsigned long long> bounds;
	const unsigned long long total =
		array_dimensions(node.fact.type, dimensions, bounds);
	const TypeId element = dimensions.empty()
		? node.fact.type
		: types.strip_cv(types.target(dimensions.back()));
	if (construct && !node.fact.zero_initialized &&
	    node.children[0]->children[0]->fact.entity->trivial)
	{
		// 12.1p5: the constructor of every element does nothing, so no element
		// has anything written for it - and the address of one is not computed
		// for a call that is not made.
		return;
	}
	const DumpNode& named =
		construct ? *node.children[0]->children[1] : *node.children[0];
	// 12.6p1 gives every element the same construction, so past the count a
	// reader wants to see written out the elements stop being a description of
	// the array and the bound starts being one.  A construction the source
	// wrote arguments for is not that same one call - the arguments are read
	// where they stand - so it stays written out.
	if (total > kArrayLoopLimit && !dimensions.empty() &&
	    (!construct || node.children[0]->children.size() <= 2))
	{
		array_lifecycle_loop(node, construct, named, total, element);
		return;
	}
	for (unsigned long long step = 0; step < total; ++step)
	{
		const unsigned long long index =
			node.fact.reverse_elements ? total - 1 - step : step;
		if (!construct)
		{
			destruction_step(node, true, index);
			continue;
		}
		// 15.2p2: an element built after another is one the exception out of it
		// leaves standing, so each element is a step of its own.
		mark_unwind_step();
		// The array is named again for each element: the element's address is
		// the array's plus the elements before it, which is one description of
		// where it is however many readers the array has.
		const LowValue object = expression(named, true);
		const Operand at =
			element_at(object.operand, dimensions, bounds, index);
		constructor_call(at, node, false, element);
	}
}

// The address of one element counted from a value rather than from a number:
// the array's own address moved by the bytes the elements before it occupy.
// An element of class type has no register width, so LowIR indexes it by those
// bytes, which is the one step every element of the loop is.
Operand LowirFunctionLowering::element_at_value(const Operand& base,
                                                const Operand& index,
                                                unsigned long long stride)
{
	Instruction scale;
	scale.kind = Instruction::IK_BINARY;
	scale.op = "mul";
	scale.type.text = "i64";
	scale.first = index;
	scale.second = named_operand(Operand::OP_INTEGER, decimal(stride));
	const Operand offset = emit(scale);
	Instruction move;
	move.kind = Instruction::IK_INDEX;
	move.type.text = "i8";
	move.first = base;
	move.second = offset;
	return emit(move);
}

// 12.4p1: what ends the lifetime of a subobject the constructor `entity`
// builds, which is the destructor of the class that declares it.  A class that
// ends a lifetime with nothing has none for a handler to call.
const SemaEntity* LowirFunctionLowering::subobject_destructor(
	const SemaEntity& constructor) const
{
	const Scope* const owner = constructor.region;
	const SemaEntity* const declared = owner == nullptr ? nullptr : owner->owner;
	const SemaEntity* const destructor =
		declared == nullptr ? nullptr : declared->destructor;
	return destructor == nullptr || destructor->trivial ? nullptr : destructor;
}

// 12.6p1 and 12.4p8 written as the loop the order is, for an array whose bound
// the source wrote as one number.  The array is named once, as the pointer to
// its first element 4.2 makes it, and every element is that pointer moved by
// the bytes before it - so the output holds one call and not the bound's worth
// of them.
//
// 15.2p2 is what the index is for beyond counting: an exception out of the
// element being built leaves the ones before it standing, and how many those
// are is a value only the loop knows.  So the handler reads the index back and
// destroys that many, which is the same loop run backwards.
void LowirFunctionLowering::array_lifecycle_loop(const DumpNode& node,
                                                 bool construct,
                                                 const DumpNode& named,
                                                 unsigned long long total,
                                                 TypeId element)
{
	TypeTable& types = unit_.types();
	const unsigned long long stride = types.object_size(types.strip_cv(element));
	if (!construct)
	{
		const SemaEntity& destructor = *node.fact.entity;
		const LowValue object = expression(named, true);
		// The loop counts bytes from the front of the array, so the address of
		// the array is already the cursor and 4.2's decay to a pointer to the
		// first element would name the same byte a second time.
		destroy_array_loop(address_of(object),
		                   named_operand(Operand::OP_INTEGER, decimal(total)),
		                   stride, destructor, node.fact.base_subobject, false,
		                   nullptr);
		return;
	}
	// 15.2p2: the whole array is one step of the object being built, so the
	// place it begins is marked once and the instructions that named it are
	// what a later step's handler writes again.
	mark_unwind_step();
	const UnwindMark mark = unwind_mark_;
	unwind_mark_ = UnwindMark();
	const LowValue object = expression(named, true);
	construct_element_run(node, object.operand, total, stride, element, mark);
}

// 12.6p1 and 8.5.1p7: `count` consecutive elements of an array standing at
// `base`, each built by the one call `action` names.  What makes them a loop
// rather than a list is that the call is the same one for every element - so
// the index the loop carries is the only thing that differs between them, and
// it is what 15.2p2's handler reads to learn how many were built.
void LowirFunctionLowering::construct_element_run(const DumpNode& action,
                                                  const Operand& base,
                                                  unsigned long long count,
                                                  unsigned long long stride,
                                                  TypeId element,
                                                  const UnwindMark& mark)
{
	TypeTable& types = unit_.types();
	const TypeId counter = types.fundamental(FT_LONG_INT);
	const DumpNode& call = *action.children[0];
	const SemaEntity& constructor = *call.children[0]->fact.entity;
	std::vector<Instruction> naming;
	if (mark.active && mark.block == current_)
	{
		const std::vector<Instruction>& written =
			out_.blocks[current_].instructions;
		naming.assign(written.begin() + static_cast<std::ptrdiff_t>(mark.at),
		              written.end());
	}
	const SemaEntity* const destructor = subobject_destructor(constructor);
	// A handler is needed where an exception out of one element would leave
	// something standing: the elements before it, or a subobject an earlier
	// step built.
	const bool guarded =
		mark.active && (destructor != nullptr || !unwind_live_.empty());
	const std::string cond = reserve_block("array_ctor_cond");
	const std::string body = reserve_block("array_ctor_body");
	const std::string end = reserve_block("array_ctor_end");
	const std::string cleanup =
		guarded ? reserve_block("array_ctor_cleanup") : std::string();
	const std::string past =
		guarded ? reserve_block("array_ctor_cont") : std::string();
	const std::string index =
		add_generated_slot("array_ctor_index", unit_.low_type(counter));
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
	test.second = named_operand(Operand::OP_INTEGER, decimal(count));
	branch(emit(test), body, end);
	open_block(body);
	const Operand address = element_at_value(base, at, stride);
	if (action.fact.zero_initialized)
	{
		// 8.5p7: the element's storage is zero before the constructor the
		// standard gave its class runs, which is one element's worth of bytes
		// and not the array's.
		zero_object(address, element);
	}
	unit_.declare_entity(constructor);
	if (guarded)
	{
		emit_handler(false, cleanup);
	}
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type = unit_.low_type(types.target(constructor.type));
	out.first = named_operand(
		Operand::OP_GLOBAL,
		unit_.function_symbol(constructor, action.fact.base_subobject));
	out.args.push_back(address);
	emit_void(out);
	if (guarded)
	{
		emit_handler_end();
	}
	Instruction next;
	next.kind = Instruction::IK_BINARY;
	next.op = "add";
	next.type = unit_.low_type(counter);
	next.first = at;
	next.second = named_operand(Operand::OP_INTEGER, "1");
	store(emit(next), cursor, counter);
	jump(cond);
	open_block(end);
	if (guarded)
	{
		jump(past);
		open_block(cleanup);
		// This handler stands in the loop the block before it opened, so what
		// named the array reaches it: unlike a handler another step wrote, it
		// is entered only from the one block that block dominates.  What it
		// does not know without asking is how many elements were built, which
		// is the loop's own index.
		const Operand built = load(cursor, counter);
		if (destructor != nullptr)
		{
			destroy_array_loop(base, built, stride, *destructor, false, false,
			                   nullptr);
		}
		for (std::size_t live = unwind_live_.size(); live-- > 0;)
		{
			// 12.6.2p10: the subobjects built before this array are destroyed
			// after it and in the reverse of the order they were built in.
			replay_unwind(unwind_live_[live]);
		}
		emit_resume();
		// The handler this array wrote is its own, so the next step cannot name
		// it however many subobjects are live when it is reached.
		unwind_dispatch_.clear();
		open_block(past);
	}
	if (mark.active)
	{
		push_unwind(constructor, naming, base, count, stride);
	}
}

// 12.4p8 over `count` elements of the array at `base`, in the reverse of the
// order they were created in.  The index is an object of the function because
// the count is a value: 15.2p2 asks for the elements an exception left
// standing, and only the construction's own index says how many those are.
//
// `guarded` says this loop stands in 12.4p8's suffix, where a destruction that
// throws leaves the elements behind it still standing - which is the same loop
// again, run from the index this one had reached, and then `after` where the
// suffix has steps behind this one and 15.1p2's resume where it has none.
void LowirFunctionLowering::destroy_array_loop(const Operand& base,
                                               const Operand& count,
                                               unsigned long long stride,
                                               const SemaEntity& destructor,
                                               bool base_subobject, bool guarded,
                                               const std::string* after,
                                               const char* blocks,
                                               const char* counter_name)
{
	TypeTable& types = unit_.types();
	const TypeId counter = types.fundamental(FT_LONG_INT);
	const std::string named(blocks);
	const std::string cond = reserve_block((named + "_cond").c_str());
	const std::string body = reserve_block((named + "_body").c_str());
	const std::string end = reserve_block((named + "_end").c_str());
	const std::string cleanup =
		guarded ? reserve_block((named + "_cleanup").c_str()) : std::string();
	const std::string past =
		guarded ? reserve_block((named + "_cont").c_str()) : std::string();
	const std::string index =
		add_generated_slot(counter_name, unit_.low_type(counter));
	const Operand cursor = named_operand(Operand::OP_SLOT, index);
	unit_.declare_call_target(destructor, base_subobject);
	store(count, cursor, counter);
	jump(cond);
	open_block(cond);
	const Operand at = load(cursor, counter);
	Instruction test;
	test.kind = Instruction::IK_CMP;
	test.op = "ne";
	test.type = unit_.low_type(counter);
	test.first = at;
	test.second = named_operand(Operand::OP_INTEGER, "0");
	branch(emit(test), body, end);
	open_block(body);
	Instruction back;
	back.kind = Instruction::IK_BINARY;
	back.op = "sub";
	back.type = unit_.low_type(counter);
	back.first = at;
	back.second = named_operand(Operand::OP_INTEGER, "1");
	const Operand previous = emit(back);
	store(previous, cursor, counter);
	const Operand address = element_at_value(base, previous, stride);
	if (guarded)
	{
		// 12.4p8: this destruction may itself throw, and what stands behind it
		// is the elements below it and the rest of the suffix.
		emit_handler(true, cleanup);
	}
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type.text = "void";
	out.first = named_operand(Operand::OP_GLOBAL,
	                          unit_.function_symbol(destructor, base_subobject));
	out.args.push_back(address);
	emit_void(out);
	if (guarded)
	{
		emit_handler_end();
	}
	jump(cond);
	open_block(end);
	if (!guarded)
	{
		return;
	}
	jump(past);
	open_block(cleanup);
	// The index was written back before the call, so what it holds is how many
	// elements stand below the one that threw - which is the same loop again.
	destroy_array_loop(base, load(cursor, counter), stride, destructor,
	                   base_subobject, false, nullptr, blocks, counter_name);
	if (after != nullptr)
	{
		jump(*after);
	}
	else
	{
		emit_resume();
	}
	open_block(past);
}

// 12.4p8: how many ends of a lifetime one action is - one per element for an
// array, and one otherwise.  15.2p2 asks about each of them separately, so the
// count is what the destructions after a destructor's body are counted in.
unsigned long long LowirFunctionLowering::destruction_steps(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	if (node.fact.entity->trivial ||
	    types.kind(types.strip_cv(node.fact.type)) != TypeKind::Array)
	{
		return node.fact.entity->trivial ? 0 : 1;
	}
	std::vector<TypeId> dimensions;
	std::vector<unsigned long long> bounds;
	return array_dimensions(node.fact.type, dimensions, bounds);
}

// 12.4p3: one of those ends, which is one call of the destructor of the
// object's class on the object - or on the element at `index` of the array the
// action names, which is where the array is plus the elements before it.
void LowirFunctionLowering::destruction_step(const DumpNode& node, bool element,
                                             unsigned long long index,
                                             unsigned long long count,
                                             const std::string* cleanup,
                                             bool suffix)
{
	TypeTable& types = unit_.types();
	const SemaEntity& destructor = *node.fact.entity;
	if (destructor.trivial)
	{
		return;
	}
	if (count != 0)
	{
		// 12.4p8: the whole array is this one step, ended by the loop 12.6p1's
		// order run backwards rather than by the elements written out.
		const LowValue whole = expression(*node.children[0], true);
		const Operand base = address_of(whole);
		std::vector<TypeId> dimensions;
		std::vector<unsigned long long> bounds;
		array_dimensions(node.fact.type, dimensions, bounds);
		const TypeId held = types.strip_cv(types.target(dimensions.back()));
		destroy_array_loop(base,
		                   named_operand(Operand::OP_INTEGER, decimal(count)),
		                   types.object_size(held), destructor,
		                   node.fact.base_subobject, suffix, cleanup);
		return;
	}
	if (cleanup != nullptr)
	{
		emit_handler(true, *cleanup);
	}
	const LowValue object = expression(*node.children[0], true);
	Operand at = address_of(object);
	if (element)
	{
		std::vector<TypeId> dimensions;
		std::vector<unsigned long long> bounds;
		array_dimensions(node.fact.type, dimensions, bounds);
		at = element_at(at, dimensions, bounds, index);
	}
	unit_.declare_entity(destructor);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type = unit_.low_type(types.target(destructor.type));
	out.first = named_operand(
		Operand::OP_GLOBAL,
		unit_.function_symbol(destructor, node.fact.base_subobject));
	out.args.push_back(at);
	emit_void(out);
	if (cleanup != nullptr)
	{
		emit_handler_end();
	}
}

// 12.1p5 and 8.5p6: constructing the object at `address`, which is one call of
// its constructor on it.  A constructor that does nothing is no call at all.
void LowirFunctionLowering::constructor_call(const Operand& address,
                                             const DumpNode& node, bool always,
                                             TypeId zeroed)
{
	TypeTable& types = unit_.types();
	const DumpNode& call = *node.children[0];
	const SemaEntity& constructor = *call.children[0]->fact.entity;
	// 15.2p2: the partly built object is the one a constructor is initializing
	// the subobjects of, so only a call that builds one of those subobjects is
	// a step of it.  `always` says this object stands at an address instead -
	// 12.2p1's temporary and 5.3.4p12's object - which is no subobject of
	// anything and leaves the step whose clause it was written in still
	// looking for its own call.
	// 15.2p2's partly built object exists only while 12.6.2's initializations
	// run, so a constructor call written anywhere else - a declaration in a
	// body, a temporary - is no step of one however the general machinery has
	// marked where it began.
	const bool subobject = !always && unwinding_;
	const UnwindMark mark = subobject ? unwind_mark_ : UnwindMark();

	std::vector<Instruction> named;
	if (mark.active && mark.block == current_)
	{
		const std::vector<Instruction>& written =
			out_.blocks[current_].instructions;
		named.assign(written.begin() + static_cast<std::ptrdiff_t>(mark.at),
		             written.end());
	}
	if (node.fact.zero_initialized)
	{
		// 8.5p7: the object was value-initialized and its class wrote no
		// constructor, so its storage is zero before the one the standard gave
		// it runs - and that zero is the whole initialization where the
		// constructor is the trivial one.  What is zeroed is one object, which
		// for an array of class type is the element being constructed rather
		// than every byte the array occupies.
		zero_object(address, zeroed == kNoType ? node.fact.type : zeroed);
	}
	// 12.8p15: this construction carries the value of an object it was given
	// into the one it builds, which is work whatever the definition of the
	// member doing it comes to - so it is the one construction 12.1p5's "there
	// is nothing to do" is never the answer for.
	const bool transfers_value =
		(constructor.transfer == kCopyConstructorTransfer ||
		 constructor.transfer == kMoveConstructorTransfer) &&
		call.children.size() > 2;
	if (transfers_value && constructor.trivial &&
	    (node.fact.subobject_step ||
	     !types.is_copy_deleted(types.strip_cv(node.fact.type))))
	{
		// 12.8p12: the copy or move the standard defines for this class does
		// nothing but carry the bytes of the object it reads from, so the
		// transfer is that copy and no call stands for it.  A trivial
		// constructor with nothing to read from is 12.1p5's, which leaves the
		// storage holding what it held; this one leaves it holding an object.
		//
		// 12.8p11 is what the bytes are not enough for: a class whose copy
		// constructor no program may name is one the program said is carried
		// by the member it declared for it, and an initialization the program
		// wrote of an object of it is the call of that member however little
		// its definition comes to.  One step inside such a definition is not
		// that initialization - 12.8p15 chose the constructor there, and where
		// that one is trivial the subobject is its bytes.
		unit_.owe_internal_definition(constructor);
		if (types.is_empty_class(types.strip_cv(node.fact.type)))
		{
			// 9p6: an object of a class that holds nothing has no bytes to
			// carry, so nothing is read out of the object this transfer reads
			// from and nothing is written into the one it builds.  1.9p12 still
			// evaluates the operand where evaluating it is something the
			// program can observe - a temporary created there is an object with
			// a lifetime of its own however little it holds.  It is evaluated
			// for what running it does and not for the object it is worth, so
			// nothing here asks for storage to read that object out of.
			if (observable_expression(*call.children[2]))
			{
				expression(*call.children[2]);
			}
			return;
		}
		// 12.8p15: what the transfer carries is the object the operand is worth,
		// which is the same question `place_class_object`'s copy asks - so it is
		// read the same way.  A call that handed its object back holding no
		// storage of its own is that object already, and giving it storage
		// first would be one copy more than the copy asked for.
		const LowValue source = expression(*call.children[2]);
		copy_object_storage(address, class_copy_source(source),
		                    node.fact.type, holds_class_value(source));
		return;
	}
	if (!always && !transfers_value &&
	    (constructor.trivial ||
	     (!constructor.user_provided &&
	      unit_.construction_writes_nothing(constructor))))
	{
		// 12.1p5: there is nothing for a call of this constructor to do.  A
		// constructor the program wrote is called wherever the program says
		// so, however empty its body; one the standard gave the class is the
		// steps 12.6.2 gives it, and where each of those comes to nothing the
		// whole construction does.
		//
		// 3.2p2: the initialization named it whether or not a call of it is
		// written, and 3.5p4's internal linkage makes the definition one no
		// other unit may hold - while the body that is not written still owes
		// what it would have called.
		unit_.owe_internal_definition(constructor);
		if (!constructor.trivial)
		{
			unit_.owe_elided_construction(constructor);
		}
		return;
	}
	// 15.2p2: an exception out of this step leaves the subobjects the steps
	// before it built, and they are destroyed before it goes on unwinding.  The
	// region opens where the step began, which is why the place was marked -
	// and where 8.5.1p1's clause reached the subobject, what the step is, is
	// the arguments that clause wrote and the call they are passed to, so the
	// path down to the subobject stands before the region.  A clause that wrote
	// no argument leaves the naming as the whole of the step, which is what
	// 12.6.2 and 12.6p1 name a subobject with.
	UnwindMark opened = mark;
	opened.at_call = mark.at_call && call.children.size() > 2;
	// 15.4p1: a call of a function whose exception-specification says it throws
	// nothing is not a place an exception leaves this step by, so nothing the
	// steps before it built needs a handler around this one.
	const UnwindRegion region =
		mark.active && !unwind_live_.empty() && !constructor.nonthrowing
			? open_unwind_region(opened)
			: UnwindRegion();
	unit_.declare_entity(constructor);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type = unit_.low_type(types.target(constructor.type));
	out.first = named_operand(
		Operand::OP_GLOBAL,
		unit_.function_symbol(constructor, node.fact.base_subobject));
	out.args.push_back(address);
	// 15.2p2: the step this call belongs to, and the fact that a call is still
	// to be made here - which is what says a temporary an argument creates is
	// one this call could throw past.
	// 12.6.2's step already marked where it began, and what names the subobject
	// is what has been written since that mark - so this call joins the step
	// rather than opening one, and an argument's own call finds it taken.
	const bool step = subobject ? false : mark_call_step();
	if (subobject)
	{
		++step_depth_;
	}
	if (!constructor.nonthrowing)
	{
		++pending_calls_;
	}
	// 12.6.2: whatever else the constructor was chosen with is passed after the
	// object, in the order the resolved call wrote them.
	const std::vector<TypeId>& parameters = types.parameters(constructor.type);
	for (std::size_t index = 2; index < call.children.size(); ++index)
	{
		const std::size_t at = index - 1;
		const bool bound =
			at < parameters.size() && types.is_reference(parameters[at]);
		const LowValue argument = expression(*call.children[index], bound);
		out.args.push_back(
			passed_operand(*call.children[index], argument, parameters, at));
	}
	if (!constructor.nonthrowing)
	{
		--pending_calls_;
	}
	if (region.dispatch.empty())
	{
		// 15.2p2 in an ordinary body: the objects standing here are the ones a
		// declaration and 12.2p1's temporaries began, and the handler that ends
		// them is opened where this step began.
		note_call(!constructor.nonthrowing);
	}
	emit_void(out);
	release_call_step(step);
	if (!region.dispatch.empty())
	{
		close_unwind_region(region);
	}
	if (mark.active)
	{
		push_unwind(constructor, named, address);
	}
}

// 15.2p2: the place one subobject's construction begins.  Whether a handler
// stands around it is known only once the step has said whether it made a call,
// so the place is remembered and the region is written into it afterwards.
void LowirFunctionLowering::mark_unwind_step(bool at_call)
{
	if (!unwinding_)
	{
		return;
	}
	unwind_mark_.active = true;
	unwind_mark_.at_call = at_call;
	unwind_mark_.block = current_;
	unwind_mark_.at = out_.blocks[current_].instructions.size();
	call_since_mark_ = false;
}

// 15.2p2: the step one call belongs to.  A call written as the operand of
// another - an argument, the object a member call is made on - stands inside
// the step the outer call opened, because what the handler owes is everything
// the outer call has already brought into being.  So the outermost call of an
// expression takes the mark and the ones under it find it taken.
bool LowirFunctionLowering::mark_call_step()
{
	const bool take = step_depth_ == 0;
	++step_depth_;
	if (!take)
	{
		return false;
	}
	unwind_mark_.active = true;
	unwind_mark_.at_call = false;
	unwind_mark_.block = current_;
	unwind_mark_.at = out_.blocks[current_].instructions.size();
	call_since_mark_ = false;
	return true;
}

void LowirFunctionLowering::release_call_step(bool)
{
	if (step_depth_ != 0)
	{
		--step_depth_;
	}
}

// 15.2p2 and 15.4p1: a call about to be written.  Where objects stand whose
// lifetimes an exception out of it would have to end, the handler that ends
// them is opened where this step began.
void LowirFunctionLowering::note_call(bool throwing)
{
	if (!throwing)
	{
		return;
	}
	call_since_mark_ = true;
	settle_pending_region();
	if (region_open_ || unwind_live_.empty() || !unwind_mark_.active ||
	    unwind_mark_.block != current_)
	{
		return;
	}
	region_ = open_unwind_region(unwind_mark_);
	region_open_ = true;
}

// The region a close left for whatever is written next.  A close that nothing
// follows leaves no region at all, which is what keeps a handler out of the end
// of a full-expression that has nothing more to do.
void LowirFunctionLowering::settle_pending_region()
{
	if (!region_pending_ || closing_region_)
	{
		return;
	}
	region_pending_ = false;
	if (unwind_live_.empty() || full_expressions_ == 0)
	{
		return;
	}
	UnwindMark here;
	here.active = true;
	here.block = current_;
	here.at = out_.blocks[current_].instructions.size();
	region_ = open_unwind_region(here);
	region_open_ = true;
}

void LowirFunctionLowering::close_region()
{
	region_pending_ = false;
	if (!region_open_)
	{
		return;
	}
	region_open_ = false;
	closing_region_ = true;
	close_unwind_region(region_);
	closing_region_ = false;
	region_ = UnwindRegion();
}

void LowirFunctionLowering::open_full_expression()
{
	++full_expressions_;
}

// 12.2p3: the full-expression is over, so the objects it created have been
// destroyed and the handler that stood around it comes off.
void LowirFunctionLowering::close_full_expression()
{
	if (full_expressions_ != 0)
	{
		--full_expressions_;
	}
	close_region();
}

// 3.8p1 and 15.2p2: the object `node` began a lifetime for joins the ones an
// exception has to end.  The set has changed, so the handler that stood around
// what came before it ends and a new one covers what comes after; where none
// stood, the step that built the object gets one of its own exactly where a
// call still to be made in this full-expression could throw past it.
void LowirFunctionLowering::begin_object_lifetime(
	const DumpNode& node, const UnwindMark& mark, const Operand& at,
	const std::vector<Instruction>& address)
{
	if (node.fact.destruction == nullptr)
	{
		return;
	}
	if (!region_open_ && call_since_mark_ && pending_calls_ != 0 &&
	    mark.active && mark.block == current_)
	{
		// A call still to be written in this full-expression is a place the
		// object could be left standing by, and the step that built it is what
		// the handler for that has to cover.
		region_ = open_unwind_region(mark);
		region_open_ = true;
	}
	const bool covered = region_open_;
	close_region();
	LowUnwind live;
	live.destructor = node.fact.destruction;
	live.object = node.fact.object != nullptr ? node.fact.object
	                                          : node.fact.entity;
	live.base_subobject = false;
	live.address = address;
	live.at = at;
	unwind_live_.push_back(live);
	// Where a handler already stood, the rest of the full-expression stands
	// under one too: the set it owes has changed, so the region it stood in
	// ends and another begins.  Where none stood, nothing yet asks for one.
	region_pending_ = covered && full_expressions_ != 0;
	// The step that built the object is over and the next one begins here, so
	// a handler the code after it needs stands where that code does.
	unwind_mark_.active = true;
	unwind_mark_.at_call = false;
	unwind_mark_.block = current_;
	unwind_mark_.at = out_.blocks[current_].instructions.size();
	call_since_mark_ = false;
}

// 3.8p1: the program wrote the end of this object's lifetime, so an exception
// after it no longer owes one.
void LowirFunctionLowering::end_object_lifetime(const DumpNode& node)
{
	const SemaEntity* const object =
		node.children.empty() ? nullptr : node.children[0]->fact.entity;
	if (object == nullptr)
	{
		return;
	}
	for (std::size_t index = unwind_live_.size(); index-- > 0;)
	{
		if (unwind_live_[index].object == object)
		{
			unwind_live_.erase(unwind_live_.begin() +
			                   static_cast<std::ptrdiff_t>(index));
			// Every handler written for more objects than stand below this one
			// destroys an object that no longer stands, so those are no longer
			// blocks a later step may name.
			if (dispatch_cache_.size() > index + 1)
			{
				dispatch_cache_.resize(index + 1);
			}
			return;
		}
	}
}

// 15.2p2: the handler the subobjects already built need, pushed where the step
// begins.  The list of them only grows, so a step needing exactly what the step
// before it needed names that block again rather than writing a second copy of
// the same destructions.
LowirFunctionLowering::UnwindRegion LowirFunctionLowering::open_unwind_region(
	const UnwindMark& mark)
{
	UnwindRegion region;
	region.live = unwind_live_.size();
	const bool held = region.live < dispatch_cache_.size() &&
		!dispatch_cache_[region.live].empty();
	region.fresh = !held;
	region.dispatch = held ? dispatch_cache_[region.live]
	                       : reserve_block("call_unwind_dispatch");
	Instruction open;
	open.kind = Instruction::IK_EH_TRY;
	open.first = named_operand(Operand::OP_LABEL, region.dispatch);
	std::vector<Instruction>& written = out_.blocks[mark.block].instructions;
	// 8.5.1p1's clause reached the subobject down a path the step did not walk,
	// so the region begins where the initialization does; everywhere else the
	// naming is the step's own and stands inside it.
	const std::size_t opens = mark.at_call ? written.size() : mark.at;
	written.insert(written.begin() + static_cast<std::ptrdiff_t>(opens), open);
	return region;
}

// The end of that region on the path the step took without throwing, and the
// block the other path runs.
void LowirFunctionLowering::close_unwind_region(const UnwindRegion& held)
{
	emit_handler_end();
	if (!held.fresh)
	{
		return;
	}
	// The block the step goes on in is named here rather than where the region
	// opened, because a step that opened blocks of its own - an arm, a loop -
	// numbered those first.
	UnwindRegion region = held;
	region.end = reserve_block("call_unwind_end");
	jump(region.end);
	const std::string behind = unwind_dispatch_;
	const std::size_t behind_live = unwind_dispatch_live_;
	open_block(region.dispatch);
	if (region.live > kUnwindSuffixLimit && !behind.empty() &&
	    behind_live + 1 == region.live)
	{
		// 12.6.2p10 again, written as the chain 12.4p8's suffix is written as:
		// what this handler owes is the subobject the step before it built plus
		// everything that step's own handler owed, so past the count a reader
		// wants to see written out it destroys the one and enters the other.
		// n subobjects then cost n destructions and not n(n+1)/2.
		replay_unwind(unwind_live_.back());
		jump(behind);
	}
	else
	{
		for (std::size_t index = region.live; index-- > 0;)
		{
			// 12.6.2p10: a subobject is destroyed in the reverse of the order it
			// was created in, which an exception does not change.
			replay_unwind(unwind_live_[index]);
		}
		emit_resume();
	}
	open_block(region.end);
	unwind_dispatch_ = region.dispatch;
	unwind_dispatch_live_ = region.live;
	if (dispatch_cache_.size() <= region.live)
	{
		dispatch_cache_.resize(region.live + 1);
	}
	dispatch_cache_[region.live] = region.dispatch;
}

// One of those destructions.  The instructions that named the subobject where
// it was built are written again here with temporaries of this block, because a
// block reached only by an exception names nothing another block produced.
void LowirFunctionLowering::replay_unwind(const LowUnwind& live)
{
	std::unordered_map<std::string, std::string> renamed;
	for (std::size_t index = 0; index < live.address.size(); ++index)
	{
		Instruction copy = live.address[index];
		Operand* const operands[3] = { &copy.first, &copy.second, &copy.third };
		for (std::size_t at = 0; at < 3; ++at)
		{
			if (operands[at]->kind != Operand::OP_TEMP)
			{
				continue;
			}
			const std::unordered_map<std::string, std::string>::const_iterator
				found = renamed.find(operands[at]->text);
			if (found != renamed.end())
			{
				operands[at]->text = found->second;
			}
		}
		if (copy.dest.empty())
		{
			emit_void(copy);
			continue;
		}
		const std::string produced = copy.dest;
		renamed[produced] = emit(copy).text;
	}
	Operand at = live.at;
	if (at.kind == Operand::OP_TEMP)
	{
		const std::unordered_map<std::string, std::string>::const_iterator found =
			renamed.find(at.text);
		if (found != renamed.end())
		{
			at.text = found->second;
		}
	}
	if (live.elements != 0)
	{
		// 12.6p1: the subobject is an array written as a loop, so what an
		// exception owes it is that loop run backwards over every element of
		// it.  `at` is already the pointer to the first element, because that
		// is what the step it replays produced.
		destroy_array_loop(
			at, named_operand(Operand::OP_INTEGER, decimal(live.elements)),
			live.stride, *live.destructor, live.base_subobject, false, nullptr);
		return;
	}
	// The name is asked for here rather than where the subobject joined the
	// list, because a step whose subobject no later step leaves standing writes
	// no handler at all - and a declaration nothing calls is not one this unit
	// owes.  This call names one of the ABI's two entry points, so it owes that
	// one and not the other.
	unit_.declare_call_target(*live.destructor, live.base_subobject);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type.text = "void";
	out.first = named_operand(
		Operand::OP_GLOBAL,
		unit_.function_symbol(*live.destructor, live.base_subobject));
	out.args.push_back(at);
	emit_void(out);
}

// 15.2p2: the subobject this step built joins the ones an exception out of a
// later step has to destroy.  A class whose destructor does nothing leaves
// nothing to destroy, so it joins nothing.
void LowirFunctionLowering::push_unwind(const SemaEntity& constructor,
                                        const std::vector<Instruction>& address,
                                        const Operand& at,
                                        unsigned long long elements,
                                        unsigned long long stride)
{
	const SemaEntity* const destructor = subobject_destructor(constructor);
	if (destructor == nullptr)
	{
		return;
	}
	LowUnwind live;
	live.destructor = destructor;
	live.elements = elements;
	live.stride = stride;
	// 12.4 and the ABI: the references name the complete-object entry here even
	// where the subobject is a base class subobject, which 12.4p8's suffix does
	// not.  This milestone has no virtual base, so the two entries destroy the
	// same storage, and the text of the object file is what says which name a
	// unit owes - so this is written the way the references write it.
	live.base_subobject = false;
	live.address = address;
	live.at = at;
	unwind_live_.push_back(live);
}

// 15.2p2: the handler-stack instructions those regions are written with.  A
// cleanup runs and goes on unwinding; `eh_end` takes the handler off again on
// the path that did not throw.
void LowirFunctionLowering::emit_handler(bool cleanup, const std::string& label)
{
	Instruction out;
	out.kind = cleanup ? Instruction::IK_EH_CLEANUP : Instruction::IK_EH_TRY;
	out.first = named_operand(Operand::OP_LABEL, label);
	emit_void(out);
}

void LowirFunctionLowering::emit_handler_end()
{
	Instruction out;
	out.kind = Instruction::IK_EH_END;
	emit_void(out);
}

void LowirFunctionLowering::emit_resume()
{
	Instruction out;
	out.kind = Instruction::IK_RESUME;
	terminate(out);
}

// 15.2p2 and 12.4p8: what a destructor owes when control leaves its own body.
// The subobjects are destroyed in the order 12.4p8 gives them, and each of
// those destructions may itself throw and leave the rest still standing - so
// each but the last stands in a cleanup region that destroys the ones behind
// it.  The whole of it is written at every point control leaves the body,
// because the body is what the outer region covers.
void LowirFunctionLowering::destructor_epilogue()
{
	emit_handler_end();
	const std::size_t total = epilogue_.size();
	// The handler of one destruction destroys the ones behind it, which are the
	// handler of the next destruction plus that destruction itself - so a
	// handler can either write them out or run the next one.  Written out they
	// are n(n+1)/2 calls, which is what a reader wants to see while there are
	// few enough of them to read; past that the chain is the same order in n
	// blocks, and the block a handler jumps to is the next handler.
	const bool chained = total > kUnwindSuffixLimit;
	std::vector<std::string> cleanups;
	std::vector<std::string> nexts;
	for (std::size_t index = 0; index + 1 < total; ++index)
	{
		cleanups.push_back(reserve_block("destructor_suffix_cleanup"));
		nexts.push_back(reserve_block("destructor_suffix_next"));
	}
	for (std::size_t index = 0; index < total; ++index)
	{
		const LowDestruction& step = epilogue_[index];
		if (index + 1 == total)
		{
			// Nothing stands behind the last one but its own elements, which
			// an array written as a loop still owes.
			destruction_step(*step.action, step.element, step.index, step.count,
			                 nullptr, true);
			break;
		}
		destruction_step(*step.action, step.element, step.index, step.count,
		                 &cleanups[index], true);
		jump(nexts[index]);
		open_block(cleanups[index]);
		if (chained)
		{
			destruction_step(*epilogue_[index + 1].action,
			                 epilogue_[index + 1].element,
			                 epilogue_[index + 1].index,
			                 epilogue_[index + 1].count);
			if (index + 2 < total)
			{
				jump(cleanups[index + 1]);
				open_block(nexts[index]);
				continue;
			}
		}
		else
		{
			for (std::size_t at = index + 1; at < total; ++at)
			{
				destruction_step(*epilogue_[at].action, epilogue_[at].element,
				                 epilogue_[at].index, epilogue_[at].count);
			}
		}
		emit_handler_end();
		emit_resume();
		open_block(nexts[index]);
	}
}

// 12.2p1: a prvalue of class type is an object, and no declaration named it, so
// the function gives it storage of its own here.  The name that storage takes
// says what asked for the object; the constructor then runs on it exactly as it
// would on one a declaration named, and is written even where it does nothing,
// because the call is the only mark this object's lifetime has begun.
LowValue LowirFunctionLowering::temporary_object(const DumpNode& node,
                                                 const Operand* into)
{
	const SemaEntity& entity = *node.fact.entity;
	LowValue value;
	value.type = node.fact.type;
	value.lvalue = true;
	value.named = true;
	const std::unordered_map<std::uint32_t, Operand>::const_iterator standing =
		placed_.find(entity.id);
	if (standing != placed_.end())
	{
		// 12.8p31: the temporary was created in the storage the place asking
		// for it owned, so the object it is stands at that address.
		value.operand = standing->second;
		return value;
	}
	const std::unordered_map<std::uint32_t, std::string>::const_iterator found =
		slots_.find(entity.id);
	if (found != slots_.end())
	{
		// The temporary was already made, so what it is worth now is the
		// storage it was given rather than a second object.
		value.operand = named_operand(Operand::OP_SLOT, found->second);
		return value;
	}
	if (into != nullptr)
	{
		// 12.8p31: the initialization named the storage this object stands in
		// before it ran, so the constructor runs there and the temporary and
		// the object being initialized are one object.
		placed_[entity.id] = *into;
		constructor_call(*into, *node.children[0], true);
		value.operand = *into;
		return value;
	}
	const std::string slot =
		add_generated_slot(node.fact.spelling.c_str(), node.fact.type);
	slots_[entity.id] = slot;
	const DumpNode& action = *node.children[0];
	const DumpNode& written = *action.children[0]->children[1];
	// The action names the object as the address of it, which is the address
	// everything that reads the temporary from here on uses.  8.5.1p2's
	// constructor of an aggregate names it by the place asking for the object
	// instead, and here that place is the storage this temporary was just
	// given, so the address is taken around the slot rather than read out of a
	// line the analysis wrote.
	LowValue held;
	held.type = node.fact.type;
	held.lvalue = true;
	held.named = true;
	held.operand = named_operand(Operand::OP_SLOT, slot);
	const Operand at = written.fact.kind == FactKind::None
		? address_of(held)
		: expression(written).operand;
	// 12.2p1: every later reader of this temporary reads the one object, so the
	// address the construction named it by is the address they all use - a
	// member of it, an argument bound to it, and 12.2p3's end of its lifetime
	// alike.  Naming the slot again instead would be a second description of
	// one place.
	placed_[entity.id] = at;
	const UnwindMark opened = unwind_mark_;
	constructor_call(at, action, true);
	// 12.2p1 and 15.2p2: the temporary's lifetime has begun, so an exception
	// out of anything written while it stands has to end it.
	begin_object_lifetime(node, opened, at, std::vector<Instruction>());
	value.operand = at;
	return value;
}

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
		constructor_call(allocated.operand, initialization, true);
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
	if (release != nullptr)
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
	TypeTable& types = unit_.types();
	SemaEntity& entity = *callee.fact.entity;
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

// 8.5p14 and 12.8p31: whether this initializer builds at the destination rather
// than being read out of an object that already stands somewhere.  The question
// the analysis asked to decide whether the copy is written at all is
// `creates_its_object`, and this is the same answer read where the destination
// is handed over, with the two things only the lowering knows added: the storage
// a temporary was already given, and whether the ABI hands a returned object
// back as bytes.  5.16p3's result object is the one addition - a conditional
// creates nothing, but the object it is worth is the one its arms fill, so where
// a place asked for that object the arms fill that place.
bool LowirFunctionLowering::creates_object(const DumpNode& node, TypeId type)
{
	TypeTable& types = unit_.types();
	if (types.strip_cv(node.fact.type) != types.strip_cv(type))
	{
		// The initializer is worth an object of another class, which reaches
		// this one through a conversion rather than by standing in its storage.
		return false;
	}
	if (node.fact.kind == FactKind::Conditional)
	{
		// 5.16p3: the conditional is a prvalue of class type, so the result
		// object each of its operands initializes is this destination.
		return node.fact.category == ValueCategory::PRValue;
	}
	if (!creates_its_object(node))
	{
		return false;
	}
	if (node.fact.kind == FactKind::Call)
	{
		// 6.6.3p2: only a function the ABI gives a destination to creates the
		// returned object there; one that hands back the bytes is copied in.
		return types.returns_indirectly(node.fact.type);
	}
	// 12.2p1: a temporary that already stands in storage of its own stands
	// there for every reader of it, so it is not built a second time here.
	return placed_.count(node.fact.entity->id) == 0 &&
		slots_.count(node.fact.entity->id) == 0;
}

LowValue LowirFunctionLowering::place_class_object(const Operand& destination,
                                                   TypeId type,
                                                   const DumpNode& node)
{
	LowValue object;
	object.type = type;
	object.lvalue = true;
	object.operand = destination;
	if (creates_object(node, type))
	{
		switch (node.fact.kind)
		{
		case FactKind::TemporaryObject:
			temporary_object(node, &destination);
			return object;

		case FactKind::Call:
			call_expression(node, &destination);
			return object;

		case FactKind::Conditional:
			conditional_object(node, &destination);
			return object;

		default:
			break;
		}
	}
	// 8.5p14: the initializer names an object that already stands somewhere,
	// so what the initialization comes to is 12.8p15's copy of it.
	const LowValue value = expression(node);
	copy_class_object(destination, class_copy_source(value), type,
	                  holds_class_value(value));
	return object;
}

Operand LowirFunctionLowering::open_object_slot(TypeId type, const char* prefix,
                                                Operand* storage)
{
	LowValue held;
	held.type = type;
	held.lvalue = true;
	held.named = true;
	held.operand =
		named_operand(Operand::OP_SLOT, add_generated_slot(prefix, type));
	if (storage != nullptr)
	{
		*storage = held.operand;
	}
	return address_of(held);
}

LowValue LowirFunctionLowering::class_object_slot(const DumpNode& node,
                                                  TypeId type,
                                                  const char* prefix)
{
	// 12.2p1: no declaration named this object, so the function gives it
	// storage - and names it before the initializer runs, because the
	// initializer creates its object in it.  8.5.3p5's name for that storage is
	// what asked for the object, which is the name the analysis wrote on it
	// where an argument or a binding asked, and this place otherwise.
	const Operand into = open_object_slot(
		type, node.fact.spelling.empty() ? prefix : node.fact.spelling.c_str());
	if (node.fact.object != nullptr)
	{
		// 12.2p3: the analysis gave this prvalue an object because some region
		// holds the end of its lifetime, and the storage it was just given is
		// what that end names - so the two are bound here, once, before the
		// initializer that fills the storage runs.
		placed_[node.fact.object->id] = into;
	}
	const UnwindMark opened = unwind_mark_;
	const LowValue standing = place_class_object(into, type, node);
	// 12.2p1: the object stands, so 15.2p2 owes its destruction from here on.
	begin_object_lifetime(node, opened, into, std::vector<Instruction>());
	return standing;
}

Operand LowirFunctionLowering::class_argument(const DumpNode& node, TypeId type)
{
	Operand storage;
	const Operand into = open_object_slot(type, "argobj", &storage);
	place_class_object(into, type, node);
	return storage;
}

// 12.8p15: a copy of a class object copies what the object holds, which for a
// class with no base subobject and no non-static data member is nothing at all.
// The bytes move as one span rather than one member at a time, which is what
// the copy is exactly while every member's own copy is the copy of its bytes.
//
// This is the one place an object of class type is copied - an initialization,
// an argument, a returned object, an arm of a conditional and a parameter's
// entry all reach it - so it is where 12.8p25 is asked whether the copy the
// program wrote is the copy of the bytes.
void LowirFunctionLowering::copy_class_object(const Operand& destination,
                                              const Operand& source,
                                              TypeId type, bool stored)
{
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(type);
	if (!types.is_trivially_copied(bare))
	{
		// 12.8p1: the copy is a call of the copy constructor the program wrote,
		// which this milestone does not select or pass an object to.  Writing
		// the bytes instead would be a different program, so the copy is
		// refused where it is asked for.
		throw std::runtime_error(
			"an object of the class type " + types.description(bare) +
			" is copied, which 12.8p1 makes a call of the copy constructor its "
			"program wrote and this milestone does not write");
	}
	copy_object_storage(destination, source, type, stored);
}

// 12.8p15: the bytes of one object of class type written into the storage of
// another, which is what a copy the standard defines comes to.  A class that
// holds nothing has no bytes for it, and 9p6 gives its object a size only so
// that two of them stand apart.
void LowirFunctionLowering::copy_object_storage(const Operand& destination,
                                                const Operand& source,
                                                TypeId type, bool stored)
{
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(type);
	if (types.is_empty_class(bare) && !stored)
	{
		// 9p6: the object holds nothing, so there are no bytes to read out of
		// it - but a value the ABI handed back is the object itself, and
		// putting it where the object stands is a store however little it says.
		return;
	}
	Instruction copy;
	copy.kind = Instruction::IK_COPYOBJ;
	copy.byte_count = static_cast<std::size_t>(types.object_size(bare));
	copy.byte_alignment = static_cast<std::size_t>(types.object_align(bare));
	copy.first = source;
	copy.second = destination;
	emit_void(copy);
}

// 5.2.2p4 and 5.2.2p7: what the argument standing at `at` is passed as - the
// conversion the parameter it reached asks for, or, where the declaration named
// no parameter there and wrote an ellipsis instead, the default argument
// promotions.  A call the program wrote and one 12.6.2 wrote for a constructor
// each pass an argument this way.
Operand LowirFunctionLowering::passed_operand(
	const DumpNode& node, const LowValue& value,
	const std::vector<TypeId>& parameters, std::size_t at)
{
	if (at < parameters.size())
	{
		return argument_operand(node, value, parameters[at]);
	}
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(value.type);
	TypeId promoted = bare;
	if (types.is_floating(bare) && types.fundamental_type(bare) == FT_FLOAT)
	{
		promoted = types.fundamental(FT_DOUBLE);
	}
	else if (types.is_integral(bare) && unit_.width(bare) < 4)
	{
		promoted =
			types.fundamental(unit_.is_signed(bare) ? FT_INT : FT_UNSIGNED_INT);
	}
	return converted(value, promoted);
}

// 5.2.2p4 and 12.8p31: what one argument of a call is passed as.  A prvalue of
// the parameter's own class was created in storage of its own a moment ago, and
// that storage is what the call passes: the temporary and the parameter are one
// object, so no copy stands between them and no second slot holds one.
Operand LowirFunctionLowering::argument_operand(const DumpNode& node,
                                                const LowValue& value,
                                                TypeId parameter)
{
	TypeTable& types = unit_.types();
	if (!types.is_reference(parameter) &&
	    types.is_class(types.strip_cv(parameter)))
	{
		return class_value_slot(node, value, types.strip_cv(parameter),
		                        "argobj");
	}
	return converted(value, parameter);
}

// 12.8p15 and 12.8p31: storage of the function's own holding a copy of one
// object of class type, which is what a place that needs a value of the class
// rather than the object the source named is given - a call's argument, a
// return's value.  A prvalue of that same class was created in storage of its
// own a moment ago, so that storage is the copy: the temporary and the place
// asking are one object, and no copy stands between them.
Operand LowirFunctionLowering::class_value_slot(const DumpNode& node,
                                                const LowValue& value,
                                                TypeId type,
                                                const char* prefix)
{
	TypeTable& types = unit_.types();
	if (node.fact.kind == FactKind::TemporaryObject &&
	    node.fact.entity != nullptr &&
	    types.strip_cv(node.fact.type) == type)
	{
		const std::unordered_map<std::uint32_t, std::string>::const_iterator
			found = slots_.find(node.fact.entity->id);
		if (found != slots_.end())
		{
			return named_operand(Operand::OP_SLOT, found->second);
		}
	}
	const std::string slot = add_generated_slot(prefix, type);
	const Operand storage = named_operand(Operand::OP_SLOT, slot);
	LowValue held;
	held.type = type;
	held.lvalue = true;
	held.operand = storage;
	// The storage the copy is made in is named before the object it is made
	// from: the place asking is what this instruction is about.
	const Operand into = address_of(held);
	copy_class_object(into, class_copy_source(value), type,
	                  holds_class_value(value));
	return storage;
}

// 12.8p15: what a copy of a class object is made from.  Where the value stands
// in storage that is its address; where a call handed it back holding no
// storage of its own it is the value itself, which is one object either way -
// and giving it storage first would be one copy more than the copy asked for.
Operand LowirFunctionLowering::class_copy_source(const LowValue& value)
{
	if (holds_class_value(value))
	{
		return value.operand;
	}
	return address_of(value);
}

bool LowirFunctionLowering::holds_class_value(const LowValue& value)
{
	return !value.lvalue && value.operand.kind == Operand::OP_TEMP &&
		unit_.types().is_class(unit_.types().strip_cv(value.type));
}

// 8.5p5: an object of class type is zero-initialized by giving every byte it
// occupies the value zero.  The bytes are written as the widest stores that fit
// - which is what the references write, and which is not the same as one store
// per member, because two members may share one - and as one `zeroinit` where
// there are more of them than a reader would follow.  A class that holds
// nothing occupies no bytes a copy or a zero reaches, so it is written nothing
// and its address is not even computed.
void LowirFunctionLowering::zero_object(const Operand& address, TypeId type)
{
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(type);
	if (types.is_empty_class(bare))
	{
		return;
	}
	zero_span(address, types.object_size(bare), types.object_align(bare));
}

// 8.5p6's zero over a span of storage: the widest write the bytes left will
// take, until there are none, and one `zeroinit` past the point where writing
// them out stops being shorter than saying how many there are.
void LowirFunctionLowering::zero_span(const Operand& address,
                                      unsigned long long size,
                                      unsigned long long align)
{
	if (size == 0)
	{
		return;
	}
	if (size > kZeroSpanLimit)
	{
		Instruction zero;
		zero.kind = Instruction::IK_ZEROINIT;
		zero.byte_count = static_cast<std::size_t>(size);
		zero.byte_alignment = static_cast<std::size_t>(align);
		zero.first = address;
		emit_void(zero);
		return;
	}
	static const char* const kWidths[] = {"i64", "i32", "i16", "i8"};
	static const unsigned long long kBytes[] = {8, 4, 2, 1};
	unsigned long long at = 0;
	while (at < size)
	{
		std::size_t step = 0;
		while (kBytes[step] > size - at)
		{
			++step;
		}
		Operand place = address;
		if (at != 0)
		{
			Instruction index;
			index.kind = Instruction::IK_INDEX;
			index.type.text = "i8";
			index.first = address;
			index.second = named_operand(Operand::OP_INTEGER, decimal(at));
			place = emit(index);
		}
		Instruction write;
		write.kind = Instruction::IK_STORE;
		write.type.text = kWidths[step];
		write.first = named_operand(Operand::OP_INTEGER, "0");
		write.second = place;
		emit_void(write);
		at += kBytes[step];
	}
}

void LowirFunctionLowering::add_destruction(const DumpNode& node)
{
	destructor_call(node);
}

// 3.7.2p2 and 3.6.2p2: what one thread's copy of an object with thread storage
// duration asks that thread to run.  There is one such object per thread and no
// point before a thread starts at which the program could reach it, so both the
// initialization the translation does not settle and 12.4p11's handing of the
// destruction to the runtime stand in a body of their own that runs at most
// once per thread - which is what the flag beside the object says, being itself
// an object of that thread.  An object whose value the image already holds runs
// no initialization here and is still an object whose lifetime ends.
void LowirFunctionLowering::add_thread_initialization(const std::string& guard,
                                                      const Operand& storage,
                                                      TypeId type,
                                                      const DumpNode* node,
                                                      const DumpNode* destruction)
{
	const TypeId counter = unit_.types().fundamental(FT_LONG_INT);
	const std::string run = reserve_block("local_static_ctor_run");
	const std::string done = reserve_block("local_static_ctor_done");
	const Operand flag = named_operand(Operand::OP_GLOBAL, guard);
	Instruction test;
	test.kind = Instruction::IK_CMP;
	test.op = "ne";
	test.type = unit_.low_type(counter);
	test.first = load(flag, counter);
	test.second = named_operand(Operand::OP_INTEGER, "0");
	branch(emit(test), done, run);
	open_block(run);
	if (node != nullptr)
	{
		add_initialization(storage, type, *node);
	}
	if (destruction != nullptr)
	{
		// 3.7.2p2 and 12.4p11: the object's lifetime ends with its thread, and
		// the runtime is what knows when that is - so what ends it is given to
		// the runtime as the object it runs on and the function it runs, with
		// the image the pair belongs to.  The runtime runs them in the reverse
		// of the order it was given them, which is the order the objects of
		// this thread were begun in.
		Instruction hand;
		hand.kind = Instruction::IK_CALL;
		hand.type.text = "i32";
		hand.first = named_operand(Operand::OP_GLOBAL,
		                           unit_.thread_atexit_symbol());
		Instruction object;
		object.kind = Instruction::IK_ADDR;
		object.type.text = "ptr";
		object.first = storage;
		const Operand at = emit(object);
		Instruction ends;
		ends.kind = Instruction::IK_ADDR;
		ends.type.text = "ptr";
		ends.first = named_operand(
			Operand::OP_GLOBAL,
			unit_.function_symbol(*destruction->fact.entity,
			                      destruction->fact.base_subobject));
		unit_.declare_entity(*destruction->fact.entity);
		Instruction image;
		image.kind = Instruction::IK_ADDR;
		image.type.text = "ptr";
		image.first = named_operand(Operand::OP_GLOBAL,
		                            unit_.image_handle_symbol());
		hand.args.push_back(emit(ends));
		hand.args.push_back(at);
		hand.args.push_back(emit(image));
		emit(hand);
	}
	store(named_operand(Operand::OP_INTEGER, "1"), flag, counter);
	jump(done);
	open_block(done);
}

// 3.8p1: the destructor actions a statement carries for the blocks control
// leaves through it, written where control leaves them.
void LowirFunctionLowering::leave_blocks(const DumpNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind == FactKind::DestructorAction)
		{
			end_object_lifetime(*node.children[index]);
			destructor_call(*node.children[index]);
		}
	}
}

// 12.4p3 and 3.8p1: the end of an object's lifetime, which is one call of the
// destructor of its class on the object the action names.
void LowirFunctionLowering::destructor_call(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const SemaEntity& destructor = *node.fact.entity;
	if (destructor.trivial)
	{
		return;
	}
	if (types.kind(types.strip_cv(node.fact.type)) == TypeKind::Array)
	{
		// 12.4p8: the object whose lifetime ends is each element of the array,
		// and the destructor runs on each of them.
		array_lifecycle(node, false);
		return;
	}
	destruction_step(node, false, 0);
}

// 12.6.2: one member of the object a constructor is initializing, given what
// its mem-initializer or its own declaration said to initialize it with.  The
// value is computed before the address of the subobject it is stored into,
// because that is the order the references write and 1.9 leaves it open.
void LowirFunctionLowering::member_initialization(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const SemaEntity& member = *node.fact.entity;
	const TypeId type = node.fact.type;
	if (node.children.size() < 2)
	{
		return;
	}
	if (!member.bit_field)
	{
		// 12.6.2p10 initializes the members in the order 9.2p13 laid them out,
		// so the storage this one takes is no longer a later bit-field's to
		// give zero to along with its own bits.
		claims_storage(member.offset,
		               member.offset + types.object_size(types.strip_cv(type)));
	}
	const DumpNode& written = *node.children[1];
	if (types.is_reference(type))
	{
		// 8.5.3p5 and 9.2p13: a member of reference type holds the address of
		// what it was bound to, which is what its storage is written with.
		// What this names is that storage itself rather than the object the
		// binding reaches, so the projection is the member's own and not the
		// `reference_field` a read through the reference makes it.
		const LowValue bound = expression(written, true);
		const Operand storage = member_storage(*node.children[0], member, true);
		store(address_of(bound), storage, type);
		return;
	}
	if (written.fact.kind == FactKind::AggregateInitialization ||
	    written.fact.kind == FactKind::BracedInitList ||
	    types.kind(types.strip_cv(type)) == TypeKind::Array)
	{
		// 8.5.1: the subobjects the clauses reached are each named from the
		// object again, which is what the aggregate path already does; the
		// object here is the member rather than a place the function holds.
		LowObject base;
		base.written = node.children[0];
		base.member = &member;
		initialize_into(base, type, written);
		return;
	}
	const LowValue value = expression(written);
	const Operand held = initializer_value(value, type);
	if (member.bit_field)
	{
		// 9.6p2: the member is a run of bits inside a unit the class also gave
		// to the members beside it, so the value goes in where those are.
		LowObject into;
		into.written = node.children[0];
		into.member = &member;
		const std::vector<const DumpNode*> path;
		initialize_bit_field(member, into, path, member.offset, held, type);
		return;
	}
	const Operand storage = member_storage(*node.children[0], member);
	store(held, storage, type);
}

void LowirFunctionLowering::initialize(const Operand& storage, TypeId type,
                                       const DumpNode& node)
{
	LowObject object;
	object.storage = storage;
	initialize_into(object, type, node);
}

void LowirFunctionLowering::initialize_into(const LowObject& object,
                                            TypeId type, const DumpNode& node)
{
	TypeTable& types = unit_.types();
	if (node.fact.kind == FactKind::AggregateInitialization)
	{
		initialize_aggregate(object, type, node);
		return;
	}
	if (types.kind(types.strip_cv(type)) == TypeKind::Array)
	{
		if (node.fact.kind == FactKind::Literal && node.fact.spelling.empty() &&
		    node.children.empty())
		{
			// 8.5p7: `m()` names no clause for any element, so every element is
			// value-initialized - which is one initialization of each element
			// rather than one of the array's bytes.
			value_initialize_array(object, type);
			return;
		}
		initialize_array(object, type, node);
		return;
	}
	if (node.fact.kind == FactKind::BracedInitList)
	{
		// 8.5.1p2 and 8.5p7: a scalar takes the value of its one clause, and
		// an empty list value-initializes it.
		if (node.children.empty())
		{
			store(zero_operand(type), object_storage(object), type);
			return;
		}
		initialize_into(object, type, *node.children[0]);
		return;
	}
	if (types.is_class(types.strip_cv(type)))
	{
		// 8.5p14 and 12.8p31: the object being initialized is where the
		// initializer creates its own object, and where the initializer names
		// one that already stands somewhere it is 12.8p15's copy of it.  The
		// declaration of an object of class type already named its address, and
		// one piece of storage has one address however many readers it has, so
		// the initialization runs at the address that declaration computed
		// rather than at a second one for the same storage - and that address
		// is named before the initializer, because the object standing there is
		// what the initialization is about.
		const Operand into =
			object.addressed ? object.address : object_address(object);
		place_class_object(into, types.strip_cv(type), node);
		return;
	}
	const LowValue value = expression(node);
	store(initializer_value(value, type), object_storage(object), type);
}

void LowirFunctionLowering::initialize_aggregate(const LowObject& object,
                                                 TypeId type,
                                                 const DumpNode& node)
{
	// The object is named once per subobject rather than once for the whole
	// initialization: the address of a subobject is what its own path through
	// the object says it is, and building each from the object keeps the
	// description of one subobject independent of the ones written before it.
	//
	// 8.5p7: this is one initialization of one object, so what it has written
	// is counted from that object; an element of an array and a member with a
	// constructor each get here again and count from their own.
	written_through_.push_back(0);
	std::vector<const DumpNode*> path;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		initialize_subobject(object, *node.children[index], path);
	}
	written_through_.pop_back();
}

// The place a store names, which for a slot or a global is the place itself:
// nothing has to be emitted to name storage the function already holds.
Operand LowirFunctionLowering::object_storage(const LowObject& object)
{
	if (!object.elements.empty())
	{
		return object_address(object);
	}
	return object.written != nullptr
		? member_storage(*object.written, *object.member)
		: object.storage;
}

Operand LowirFunctionLowering::object_address(const LowObject& object)
{
	Operand at;
	if (object.written != nullptr)
	{
		at = member_storage(*object.written, *object.member);
	}
	else
	{
		LowValue held;
		held.lvalue = true;
		held.operand = object.storage;
		at = address_of(held);
	}
	// 8.5.1p1: what is being initialized is one element of that array, which is
	// where the array is plus the elements before it - and where the walk
	// stepped through more than one dimension, one such step per dimension,
	// which is 5.2.1p1's own reading of the subscripts.  The array decays once,
	// because after the first step the walk already stands at an element.
	for (std::size_t index = 0; index < object.elements.size(); ++index)
	{
		const LowObject::ElementStep& step = object.elements[index];
		at = index == 0 ? array_element(at, step.array, step.index)
		                : element_step(at, step.array, step.index);
	}
	return at;
}

Operand LowirFunctionLowering::subobject_address(
	const LowObject& object, const std::vector<const DumpNode*>& path)
{
	TypeTable& types = unit_.types();
	Operand at = object_address(object);
	for (std::size_t index = 0; index < path.size(); ++index)
	{
		const DumpNode& step = *path[index];
		if (step.fact.entity != nullptr)
		{
			// 9.2p13: a member begins where the layout of its class put it.
			Instruction move;
			move.kind = Instruction::IK_INDEX;
			move.type.text = "i8";
			move.index_projection = lowir_model::IPK_FIELD;
			move.first = at;
			move.second = named_operand(Operand::OP_INTEGER,
			                            decimal(step.fact.entity->offset));
			at = emit(move);
			continue;
		}
		// 8.3.4p6 and 4.2: an element is reached through the pointer view of
		// the array, counted the one way every other element of an array of
		// this milestone is counted - which for an element with no register
		// width is the bytes 5.2.1p1 counts rather than the object type, and is
		// what the element step already says.
		Instruction decayed;
		decayed.kind = Instruction::IK_UNARY;
		decayed.op = "decay";
		decayed.type.text = "ptr";
		decayed.first = at;
		at = element_of_step(emit(decayed), step.fact.type, step.fact.value);
	}
	(void)types;
	return at;
}

void LowirFunctionLowering::initialize_subobject(
	const LowObject& object, const DumpNode& node,
	std::vector<const DumpNode*>& path)
{
	// 15.2p2: whatever this subobject turns out to be, its initialization
	// begins here, and where it is one of class type the call it makes is a
	// step an exception out of a later one has to undo.  8.5.1p1's path to the
	// subobject is walked before that initialization, so the region begins
	// where the call does and the path is what the handler replays.
	mark_unwind_step(true);
	path.push_back(&node);
	if (!node.children.empty() &&
	    node.children[0]->fact.kind == FactKind::SubobjectInitialization)
	{
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			initialize_subobject(object, *node.children[index], path);
		}
		path.pop_back();
		return;
	}
	if (node.fact.entity != nullptr && node.fact.entity->bit_field)
	{
		// 9.6p2: the clause reached a run of bits inside a unit the class also
		// gave to the members beside it, so the value goes in where those are
		// and the place is named after it rather than before.
		const SemaEntity& field = *node.fact.entity;
		const unsigned long long unit = subobject_offset(path);
		if (field.bit_width == 0)
		{
			// 9.6p2: a field of width zero holds no bits, so there is nothing
			// for a clause to reach it with and nothing to write.
			path.pop_back();
			return;
		}
		const unsigned long long size =
			unit_.types().object_size(unit_.types().strip_cv(node.fact.type));
		if (node.children.empty())
		{
			// 8.5p7: a member no clause reached is value-initialized.  Where
			// this initialization still owns the unit, the zero it gives the
			// field is the zero of the whole unit; where an earlier field of
			// the same unit was written, that write already gave these bits
			// their zero and there is nothing left to do.
			if (claims_storage(unit, unit + size))
			{
				store(zero_operand(node.fact.type),
				      subobject_address(object, path), node.fact.type);
			}
			path.pop_back();
			return;
		}
		const LowValue value = expression(*node.children[0]);
		const Operand held = initializer_value(value, node.fact.type);
		initialize_bit_field(field, object, path, unit, held, node.fact.type);
		path.pop_back();
		return;
	}
	// 8.5p7: whatever this subobject is, the bytes it occupies are the
	// initialization's own from here on, so a bit-field of a storage unit that
	// reaches back into them may not take the unit whole.
	claims_storage(subobject_offset(path),
	               subobject_offset(path) +
	                   unit_.types().object_size(node.fact.type));
	if (node.fact.op == 0 &&
	    unit_.types().is_empty_class(
		    unit_.types().strip_cv(node.fact.type)) &&
	    (node.children.empty() ||
	     (node.children[0]->fact.kind == FactKind::ConstructorAction &&
	      node.children[0]->children[0]->children[0]->fact.entity->trivial)))
	{
		// 8.5p7 and 9p6: a subobject of a class that holds nothing, whose
		// initialization is the zero of storage it has none of and a
		// constructor 12.1p5 makes trivial.  Neither writes anything, so there
		// is no address to compute for it either - which is what the references
		// write for a member no clause reached and for one whose clause chose
		// that same trivial constructor, an element of an array of the class
		// included.
		path.pop_back();
		return;
	}
	if (node.children.size() == 1 && node.fact.op == 0 &&
	    node.children[0]->fact.kind != FactKind::AggregateInitialization &&
	    node.children[0]->fact.kind != FactKind::BracedInitList &&
	    unit_.types().kind(unit_.types().strip_cv(node.fact.type)) !=
		    TypeKind::Array &&
	    !unit_.types().is_class(unit_.types().strip_cv(node.fact.type)))
	{
		// The value is computed before the address of the subobject it is
		// stored into, because that is the order the references write and 1.9
		// leaves it open - the same order 12.6.2's mem-initializer writes.
		const LowValue value = expression(*node.children[0]);
		const Operand held = initializer_value(value, node.fact.type);
		const Operand into = subobject_address(object, path);
		path.pop_back();
		store(held, into, node.fact.type);
		return;
	}
	const Operand at = subobject_address(object, path);
	path.pop_back();
	if (!node.children.empty() &&
	    node.children[0]->fact.kind == FactKind::ConstructorAction &&
	    node.children[0]->fact.elided_prvalue &&
	    unit_.types().is_empty_class(unit_.types().strip_cv(node.fact.type)))
	{
		// 8.5.1p2 copy-initializes the subobject from the clause that reached
		// it; where that clause is a prvalue of its own class, 12.8p31 makes
		// the two one object and 12.8p15's copy of a class with no non-static
		// data member and no base subobject copies nothing.  The checked-in
		// LowIR writes no action for that subobject at all - the construction
		// of the prvalue goes with the copy it was elided into.  The address is
		// still named, because 8.5.1p1 reached the subobject, and the
		// constructor the analysis chose is still a use of it, so its
		// definition is still written.  This is the one initialization whose
		// clause the output does not evaluate, and it is that prvalue alone:
		// 5.2.3p2's `T()`, a braced clause 13.3.1.7 hands to a constructor, an
		// element of an array, an argument and a mem-initializer each write
		// their call.
		unit_.declare_entity(
			*node.children[0]->children[0]->children[0]->fact.entity);
		return;
	}
	if (!node.children.empty() &&
	    node.children[0]->fact.kind == FactKind::ConstructorAction)
	{
		// 8.5.1p2: the clause reached a subobject of class type, which is one
		// object built where it stands - the constructor 8.5 chose runs on the
		// storage this path names, exactly as it would on a declared object.
		constructor_call(at, *node.children[0]);
		return;
	}
	if (node.fact.op != 0)
	{
		// 8.5.1p7: every element from this one on is value-initialized, which
		// is one span of zero bytes rather than one store per element.
		TypeTable& types = unit_.types();
		const unsigned long long stride = types.object_size(node.fact.type);
		Instruction zero;
		zero.kind = Instruction::IK_ZEROINIT;
		zero.byte_count = static_cast<std::size_t>(
			stride * (types.bound(types.strip_cv(node.fact.spelled)) -
			          node.fact.value));
		zero.byte_alignment =
			static_cast<std::size_t>(types.object_align(node.fact.type));
		zero.first = at;
		emit_void(zero);
		return;
	}
	if (node.children.empty())
	{
		// 8.5p7: a subobject no clause reached is value-initialized, which for
		// every type this milestone lays out is the zero of it.  For one of
		// class type that zero is the zero of the bytes it occupies rather than
		// a value a store of its own type could hold.
		if (unit_.types().is_class(unit_.types().strip_cv(node.fact.type)))
		{
			zero_object(at, node.fact.type);
			return;
		}
		store(zero_operand(node.fact.type), at, node.fact.type);
		return;
	}
	initialize(at, node.fact.type, *node.children[0]);
}

// 8.5p7: every element of the array is value-initialized, which for an element
// of any type this milestone lays out is the zero of it.  The elements are
// written as the elements they are - each named from the array again, which is
// the one description of where an element is - while there are few enough of
// them for that to be a description of the array; past that the storage is what
// the initialization is about and the zero is one span.
void LowirFunctionLowering::value_initialize_array(const LowObject& object,
                                                   TypeId type)
{
	TypeTable& types = unit_.types();
	const TypeId array = types.strip_cv(type);
	const unsigned long long size = types.object_size(array);
	if (size == 0)
	{
		return;
	}
	std::vector<TypeId> dimensions;
	std::vector<unsigned long long> bounds;
	const unsigned long long total = array_dimensions(array, dimensions, bounds);
	if (total == 0 || size > kZeroSpanLimit)
	{
		Instruction zero;
		zero.kind = Instruction::IK_ZEROINIT;
		zero.byte_count = static_cast<std::size_t>(size);
		zero.byte_alignment = static_cast<std::size_t>(types.object_align(array));
		zero.first = object_address(object);
		emit_void(zero);
		return;
	}
	const TypeId element = types.strip_cv(types.target(dimensions.back()));
	for (unsigned long long index = 0; index < total; ++index)
	{
		const Operand at =
			element_at(object_address(object), dimensions, bounds, index);
		if (types.is_class(element))
		{
			zero_object(at, element);
			continue;
		}
		store(zero_operand(element), at, element);
	}
}

void LowirFunctionLowering::initialize_array(const LowObject& object,
                                             TypeId type, const DumpNode& node)
{
	// 8.5.1: the clauses initialize the elements in order and the elements no
	// clause reached are value-initialized.  Where the array is the object
	// itself the elements are addressed from one base, by byte, so the storage
	// is named once however many there are; where it is a subobject of one, an
	// element is named the way the program would name it - the object, the
	// member, then the element - which is the one description every other place
	// that reaches an element uses, and the only one 15.2p2's handler can write
	// again in a block of its own.
	TypeTable& types = unit_.types();
	const TypeId element = types.target(types.strip_cv(type));
	const unsigned long long stride = types.object_size(element);
	const unsigned long long bound = types.bound(types.strip_cv(type));
	if (node.children.size() > bound)
	{
		throw std::runtime_error("an array initializer has more clauses than "
		                         "the array has elements");
	}
	const bool subobject = object.written != nullptr;
	// 8.5p7: the elements no clause reached are value-initialized, which is one
	// span of zero bytes.  A scalar element is still written one store at a
	// time while there are few enough for that to be a description of the
	// elements; past that, and for an element no single store can hold, the
	// span is what the initialization is, and `zeroinit` is how LowIR spells
	// one.
	// 8.5.1p7: the elements no clause reached may be one action rather than one
	// each, so how many elements the children account for is not how many
	// children there are.
	unsigned long long covered = 0;
	for (std::size_t at = 0; at < node.children.size(); ++at)
	{
		const unsigned long long run = node.children[at]->fact.elements;
		covered += run == 0 ? 1 : run;
	}
	const unsigned long long left = (bound - covered) * stride;
	const bool spelled_elementwise =
		unit_.low_type(element).text.compare(0, 4, "obj<") != 0 &&
		left <= kZeroSpanLimit;
	const unsigned long long written =
		spelled_elementwise ? bound : node.children.size();
	const Operand address = subobject ? Operand() : object_address(object);
	for (unsigned long long index = 0; index < written; ++index)
	{
		// 15.2p2: an element built after another is one an exception out of
		// that later one leaves standing, so each is a step of its own.
		mark_unwind_step(true);
		LowObject reached = object;
		Operand at = address;
		if (subobject)
		{
			reached.elements.push_back(
				LowObject::ElementStep(types.strip_cv(type), index));
		}
		else if (index != 0)
		{
			Instruction step;
			step.kind = Instruction::IK_INDEX;
			step.type.text = "i8";
			step.first = address;
			step.second =
				named_operand(Operand::OP_INTEGER, decimal(index * stride));
			at = emit(step);
		}
		if (index < node.children.size())
		{
			if (node.children[index]->fact.kind == FactKind::ConstructorAction)
			{
				const unsigned long long run =
					node.children[index]->fact.elements;
				if (run > 1)
				{
					// 8.5.1p7: every element from here to the end of the array
					// is value-initialized by that same one call, so what is
					// written is the loop the call is - which is where the step
					// this mark opened begins, and the elements stand inside it.
					const UnwindMark mark = unwind_mark_;
					unwind_mark_ = UnwindMark();
					const Operand base =
						subobject ? object_address(reached) : at;
					construct_element_run(*node.children[index], base, run,
					                      stride, types.strip_cv(element), mark);
					break;
				}
				// 12.6p1: the element is one object of its class, built where
				// it stands by the constructor 8.5 chose for it.
				constructor_call(subobject ? object_address(reached) : at,
				                 *node.children[index]);
				continue;
			}
			if (subobject)
			{
				// The element keeps the walk that named it, so an element of
				// its own - a dimension further in - is named from the object
				// again rather than from the address this step produced, and
				// the address is written once by whoever needs it.
				initialize_into(reached, element, *node.children[index]);
				continue;
			}
			initialize(at, element, *node.children[index]);
			continue;
		}
		store(zero_operand(element),
		      subobject ? object_address(reached) : at, element);
	}
	if (spelled_elementwise || left == 0)
	{
		return;
	}
	Operand at = address;
	if (subobject)
	{
		LowObject reached = object;
		if (written != 0)
		{
			reached.elements.push_back(
				LowObject::ElementStep(types.strip_cv(type), written));
		}
		at = object_address(reached);
	}
	else if (written != 0)
	{
		Instruction step;
		step.kind = Instruction::IK_INDEX;
		step.type.text = "i8";
		step.first = address;
		step.second =
			named_operand(Operand::OP_INTEGER, decimal(written * stride));
		at = emit(step);
	}
	Instruction zero;
	zero.kind = Instruction::IK_ZEROINIT;
	zero.byte_count = left;
	zero.byte_alignment = types.object_align(types.strip_cv(element));
	zero.first = at;
	emit_void(zero);
}
