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
		// The object has a name of the program image, so nothing has to be
		// emitted to name it and a constructor that does nothing leaves no
		// action at all.
		if (node.children[0]->children[0]->fact.entity->trivial)
		{
			return;
		}
		LowValue object;
		object.type = type;
		object.lvalue = true;
		object.operand = storage;
		constructor_call(address_of(object), node);
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
			at.element_array = array;
			at.element_index = index;
			if (node.children[index]->fact.kind == FactKind::ConstructorAction)
			{
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
	const TypeId element = types.target(types.strip_cv(array_type));
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
	for (unsigned long long step = 0; step < total; ++step)
	{
		const unsigned long long index =
			node.fact.reverse_elements ? total - 1 - step : step;
		// The array is named again for each element: the element's address is
		// the array's plus the elements before it, which is one description of
		// where it is however many readers the array has.
		const LowValue object = expression(named, true);
		const Operand at = element_at(
			construct ? object.operand : address_of(object), dimensions, bounds,
			index);
		if (construct)
		{
			constructor_call(at, node, false, element);
			continue;
		}
		const SemaEntity& destructor = *node.fact.entity;
		unit_.declare_entity(destructor);
		Instruction out;
		out.kind = Instruction::IK_CALL;
		out.type = unit_.low_type(types.target(destructor.type));
		out.first = named_operand(
			Operand::OP_GLOBAL,
			unit_.function_symbol(destructor, node.fact.base_subobject));
		out.args.push_back(at);
		emit_void(out);
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
	if (constructor.trivial && !always)
	{
		return;
	}
	unit_.declare_entity(constructor);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type = unit_.low_type(types.target(constructor.type));
	out.first = named_operand(
		Operand::OP_GLOBAL,
		unit_.function_symbol(constructor, node.fact.base_subobject));
	out.args.push_back(address);
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
	emit_void(out);
}

// 12.2p1: a prvalue of class type is an object, and no declaration named it, so
// the function gives it storage of its own here.  The name that storage takes
// says what asked for the object; the constructor then runs on it exactly as it
// would on one a declaration named, and is written even where it does nothing,
// because the call is the only mark this object's lifetime has begun.
LowValue LowirFunctionLowering::temporary_object(const DumpNode& node)
{
	const SemaEntity& entity = *node.fact.entity;
	LowValue value;
	value.type = node.fact.type;
	value.lvalue = true;
	value.named = true;
	const std::unordered_map<std::uint32_t, std::string>::const_iterator found =
		slots_.find(entity.id);
	if (found != slots_.end())
	{
		// The temporary was already made, so what it is worth now is the
		// storage it was given rather than a second object.
		value.operand = named_operand(Operand::OP_SLOT, found->second);
		return value;
	}
	const std::string slot =
		add_generated_slot(node.fact.spelling.c_str(), node.fact.type);
	slots_[entity.id] = slot;
	const DumpNode& action = *node.children[0];
	// The action names the object as the address of it, which is the address
	// everything that reads the temporary from here on uses.
	const LowValue object = expression(*action.children[0]->children[1]);
	constructor_call(object.operand, action, true);
	value.operand = object.operand;
	return value;
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
                                              TypeId type)
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
	if (types.is_empty_class(bare))
	{
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
	copy_class_object(into, class_copy_source(value), type);
	return storage;
}

// 12.8p15: what a copy of a class object is made from.  Where the value stands
// in storage that is its address; where a call handed it back holding no
// storage of its own it is the value itself, which is one object either way -
// and giving it storage first would be one copy more than the copy asked for.
Operand LowirFunctionLowering::class_copy_source(const LowValue& value)
{
	if (!value.lvalue && value.operand.kind == Operand::OP_TEMP &&
	    unit_.types().is_class(unit_.types().strip_cv(value.type)))
	{
		return value.operand;
	}
	return address_of(value);
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
	const unsigned long long size = types.object_size(bare);
	if (size == 0)
	{
		return;
	}
	if (size > kZeroSpanLimit)
	{
		Instruction zero;
		zero.kind = Instruction::IK_ZEROINIT;
		zero.byte_count = static_cast<std::size_t>(size);
		zero.byte_alignment = static_cast<std::size_t>(types.object_align(bare));
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
	const LowValue object = expression(*node.children[0], true);
	unit_.declare_entity(destructor);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type = unit_.low_type(types.target(destructor.type));
	out.first = named_operand(
		Operand::OP_GLOBAL,
		unit_.function_symbol(destructor, node.fact.base_subobject));
	out.args.push_back(address_of(object));
	emit_void(out);
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
			store(literal_operand(type, 0), object_storage(object), type);
			return;
		}
		initialize_into(object, type, *node.children[0]);
		return;
	}
	const LowValue value = expression(node);
	if (types.is_class(types.strip_cv(type)))
	{
		// 12.8p15: the object holds what the initializer's object holds, which
		// is one copy of its bytes into the storage this initialization already
		// owns - not a value read out of one object and written into another.
		// The declaration of an object of class type already named its address,
		// and one piece of storage has one address however many readers it
		// has, so the copy is written into the address that declaration
		// computed rather than into a second one for the same storage.
		const Operand into =
			object.addressed ? object.address : object_address(object);
		copy_class_object(into, class_copy_source(value), types.strip_cv(type));
		return;
	}
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
	if (object.element_array != kNoType)
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
	if (object.element_array == kNoType)
	{
		return at;
	}
	// 8.5.1p1: what is being initialized is one element of that array, which is
	// where the array is plus the elements before it.
	return array_element(at, object.element_array, object.element_index);
}

Operand LowirFunctionLowering::subobject_address(
	const LowObject& object, const std::vector<const DumpNode*>& path)
{
	TypeTable& types = unit_.types();
	Operand at = object_address(object);
	for (std::size_t index = 0; index < path.size(); ++index)
	{
		const DumpNode& step = *path[index];
		Instruction move;
		move.kind = Instruction::IK_INDEX;
		if (step.fact.entity != nullptr)
		{
			// 9.2p13: a member begins where the layout of its class put it.
			move.type.text = "i8";
			move.index_projection = lowir_model::IPK_FIELD;
			move.first = at;
			move.second = named_operand(Operand::OP_INTEGER,
			                            decimal(step.fact.entity->offset));
			at = emit(move);
			continue;
		}
		// 8.3.4p6 and 4.2: an element is reached through the pointer view of
		// the array, counted in elements rather than in bytes.
		Instruction decayed;
		decayed.kind = Instruction::IK_UNARY;
		decayed.op = "decay";
		decayed.type.text = "ptr";
		decayed.first = at;
		move.type = unit_.low_type(step.fact.type);
		move.index_projection = lowir_model::IPK_ARRAY_ELEMENT;
		move.first = emit(decayed);
		move.second =
			named_operand(Operand::OP_INTEGER, decimal(step.fact.value));
		at = emit(move);
	}
	(void)types;
	return at;
}

void LowirFunctionLowering::initialize_subobject(
	const LowObject& object, const DumpNode& node,
	std::vector<const DumpNode*>& path)
{
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
				store(literal_operand(node.fact.type, 0),
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
	if (node.children.empty() && node.fact.op == 0 &&
	    unit_.types().is_empty_class(
		    unit_.types().strip_cv(node.fact.type)))
	{
		// 8.5p7 and 9p6: no clause reached a subobject of a class that holds
		// nothing, so the zero it is value-initialized with has no bytes to be
		// written into and there is no address to compute for it either.
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
		store(literal_operand(node.fact.type, 0), at, node.fact.type);
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
		store(literal_operand(element, 0), at, element);
	}
}

void LowirFunctionLowering::initialize_array(const LowObject& object,
                                             TypeId type, const DumpNode& node)
{
	// 8.5.1: the clauses initialize the elements in order and the elements no
	// clause reached are value-initialized.  The elements are addressed from
	// one base, by byte, so the storage is named once however many there are.
	TypeTable& types = unit_.types();
	const TypeId element = types.target(types.strip_cv(type));
	const unsigned long long stride = types.object_size(element);
	const unsigned long long bound = types.bound(types.strip_cv(type));
	if (node.children.size() > bound)
	{
		throw std::runtime_error("an array initializer has more clauses than "
		                         "the array has elements");
	}
	const Operand address = object_address(object);
	// 8.5p7: the elements no clause reached are value-initialized, which is one
	// span of zero bytes.  A scalar element is still written one store at a
	// time while there are few enough for that to be a description of the
	// elements; past that, and for an element no single store can hold, the
	// span is what the initialization is, and `zeroinit` is how LowIR spells
	// one.
	const unsigned long long left = (bound - node.children.size()) * stride;
	const bool spelled_elementwise =
		unit_.low_type(element).text.compare(0, 4, "obj<") != 0 &&
		left <= kZeroSpanLimit;
	const unsigned long long written =
		spelled_elementwise ? bound : node.children.size();
	for (unsigned long long index = 0; index < written; ++index)
	{
		Operand at = address;
		if (index != 0)
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
				// 12.6p1: the element is one object of its class, built where
				// it stands by the constructor 8.5 chose for it.
				constructor_call(at, *node.children[index]);
				continue;
			}
			initialize(at, element, *node.children[index]);
			continue;
		}
		store(literal_operand(element, 0), at, element);
	}
	if (spelled_elementwise || left == 0)
	{
		return;
	}
	Operand at = address;
	if (written != 0)
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
