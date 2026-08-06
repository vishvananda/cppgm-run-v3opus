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
	initialize(storage, type, node);
}

// 12.1p5 and 8.5p6: constructing the object at `address`, which is one call of
// its constructor on it.  A constructor that does nothing is no call at all.
void LowirFunctionLowering::constructor_call(const Operand& address,
                                             const DumpNode& node, bool always)
{
	TypeTable& types = unit_.types();
	const DumpNode& call = *node.children[0];
	const SemaEntity& constructor = *call.children[0]->fact.entity;
	if (node.fact.zero_initialized)
	{
		// 8.5p7: the object was value-initialized and its class wrote no
		// constructor, so its storage is zero before the one the standard gave
		// it runs - and that zero is the whole initialization where the
		// constructor is the trivial one.
		zero_object(address, node.fact.type);
	}
	if (constructor.trivial && !always)
	{
		return;
	}
	unit_.declare_entity(constructor);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type = unit_.low_type(types.target(constructor.type));
	out.first =
		named_operand(Operand::OP_GLOBAL, unit_.function_symbol(constructor));
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
			argument_operand(*call.children[index], argument, parameters[at]));
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
// Everything this milestone copies is trivially copyable, so the bytes move as
// one span rather than one member at a time.
void LowirFunctionLowering::copy_class_object(const Operand& destination,
                                              const Operand& source,
                                              TypeId type)
{
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(type);
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

// 5.2.2p4 and 12.8p31: what one argument of a call is passed as.  A prvalue of
// the parameter's own class was created in storage of its own a moment ago, and
// that storage is what the call passes: the temporary and the parameter are one
// object, so no copy stands between them and no second slot holds one.
Operand LowirFunctionLowering::argument_operand(const DumpNode& node,
                                                const LowValue& value,
                                                TypeId parameter)
{
	TypeTable& types = unit_.types();
	if (node.fact.kind == FactKind::TemporaryObject &&
	    !types.is_reference(parameter) &&
	    types.is_class(types.strip_cv(parameter)) &&
	    types.strip_cv(node.fact.type) == types.strip_cv(parameter))
	{
		const std::unordered_map<std::uint32_t, std::string>::const_iterator
			found = slots_.find(node.fact.entity->id);
		if (found != slots_.end())
		{
			return named_operand(Operand::OP_SLOT, found->second);
		}
	}
	return converted(value, parameter);
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
	const LowValue object = expression(*node.children[0], true);
	unit_.declare_entity(destructor);
	Instruction out;
	out.kind = Instruction::IK_CALL;
	out.type = unit_.low_type(types.target(destructor.type));
	out.first =
		named_operand(Operand::OP_GLOBAL, unit_.function_symbol(destructor));
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
	return object.written != nullptr
		? member_storage(*object.written, *object.member)
		: object.storage;
}

Operand LowirFunctionLowering::object_address(const LowObject& object)
{
	if (object.written != nullptr)
	{
		return member_storage(*object.written, *object.member);
	}
	LowValue held;
	held.lvalue = true;
	held.operand = object.storage;
	return address_of(held);
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
	const Operand at = subobject_address(object, path);
	path.pop_back();
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
