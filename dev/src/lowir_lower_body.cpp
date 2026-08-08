#include "lowir_lower.h"

#include <sstream>
#include <stdexcept>

#include "lowir_abi.h"
#include "sema_scope.h"
#include "token_model.h"

// The function layer of the PA15 lowering: one body, read once, in source
// order.
//
// A statement emits into the block that is open and opens the blocks its own
// control flow needs; an expression is lowered bottom up into the operands its
// operands already produced.  The block labels of one construct are reserved
// where the construct starts, so the numbering of a nested one follows the
// numbering of the one that holds it without either being renumbered later.

namespace {

using lowir_model::Instruction;
using lowir_model::Operand;

std::string decimal(unsigned long long value)
{
	std::ostringstream out;
	out << value;
	return out.str();
}

std::string signed_decimal(long long value)
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

// The child of `node` that is the resolved expression it holds, skipping the
// declaration wrappers the dump writes around one.
const DumpNode* only_child(const DumpNode& node)
{
	return node.children.empty() ? nullptr : node.children[0];
}

// 9.6p2: the bits of a storage unit the field owns, at the bottom of the unit.
// A width of 64 is written out rather than shifted, because a shift by the
// width of the value it shifts is not one.
unsigned long long field_mask(const SemaEntity& field)
{
	return field.bit_width >= 64 ? ~0ull : ((1ull << field.bit_width) - 1);
}

// The mask a read or a write of the field's own bits is made with, and the mask
// the bits it leaves alone are kept by, each at the width of the type the unit
// is accessed at.  Both are spelled unsigned however that type is signed,
// because a mask is a pattern of bits and not a number.
std::string mask_bits(unsigned long long bits, unsigned long long size)
{
	return decimal(size >= 8 ? bits : (bits & ((1ull << (8 * size)) - 1)));
}

}  // namespace

LowirFunctionLowering::LowirFunctionLowering(LowirUnitLowering& unit,
                                             lowir_model::Function& out)
	: unit_(unit)
	, out_(out)
	, temps_(0)
	, blocks_(0)
	, generated_slots_(0)
	, current_(0)
	, open_(false)
	, returns_(kNoType)
	, indirect_result_(false)
	, returned_object_open_(false)
	, return_slot_local_(nullptr)
	, element_runs_(0)
	, destroyed_class_(kNoType)
	, unwinding_(false)
	, unwind_dispatch_live_(0)
	, ended_lifetimes_(0)
	, region_open_(false)
	, region_pending_(false)
	, call_since_mark_(false)
	, closing_region_(false)
	, full_expressions_(0)
	, pending_calls_(0)
	, step_depth_(0)
{}

// ---------------------------------------------------------------- emission

Operand LowirFunctionLowering::temp()
{
	// A parameter is named after the declaration it stands for, so a function
	// can already hold the name a temporary would take.  The counter keeps
	// rising until it names one nothing else in the function does, which is
	// what keeps a source declaration from being reachable as a temporary.
	std::string name;
	do
	{
		++temps_;
		name = "t" + decimal(temps_);
	}
	while (taken_.count(name) != 0);
	return named_operand(Operand::OP_TEMP, name);
}

Operand LowirFunctionLowering::emit(Instruction& instruction)
{
	// 15.2p2: a handler a change in the live objects left for whatever came
	// next stands around this instruction, because this is what came next.
	settle_pending_region();
	const Operand result = temp();
	instruction.dest = result.text;
	out_.blocks[current_].instructions.push_back(instruction);
	return result;
}

void LowirFunctionLowering::emit_void(Instruction& instruction)
{
	settle_pending_region();
	out_.blocks[current_].instructions.push_back(instruction);
}

void LowirFunctionLowering::terminate(Instruction& instruction)
{
	// 15.2p2: control leaves this block, so the handler that stood around what
	// it held comes off before it does - and a handler left pending for a next
	// instruction that never came is no handler at all.
	if (!closing_region_)
	{
		region_pending_ = false;
		close_region();
	}
	out_.blocks[current_].instructions.push_back(instruction);
	open_ = false;
}

bool LowirFunctionLowering::terminated() const
{
	return !open_;
}

std::string LowirFunctionLowering::reserve_block(const char* prefix)
{
	++blocks_;
	return std::string(prefix) + "_" + decimal(blocks_);
}

void LowirFunctionLowering::open_block(const std::string& label)
{
	lowir_model::Block block;
	block.label = label;
	out_.blocks.push_back(block);
	current_ = out_.blocks.size() - 1;
	open_ = true;
	// 15.2p2: a handler is written inside the block it covers, so a step whose
	// region has still to be opened begins again where this block does.
	unwind_mark_.active = true;
	unwind_mark_.at_call = false;
	unwind_mark_.block = current_;
	unwind_mark_.at = 0;
	call_since_mark_ = false;
	step_depth_ = 0;
}

void LowirFunctionLowering::jump(const std::string& label)
{
	Instruction instruction;
	instruction.kind = Instruction::IK_JUMP;
	instruction.first = named_operand(Operand::OP_LABEL, label);
	terminate(instruction);
}

void LowirFunctionLowering::branch(const Operand& condition,
                                   const std::string& on_true,
                                   const std::string& on_false)
{
	Instruction instruction;
	instruction.kind = Instruction::IK_BRANCH;
	instruction.first = condition;
	instruction.second = named_operand(Operand::OP_LABEL, on_true);
	instruction.third = named_operand(Operand::OP_LABEL, on_false);
	terminate(instruction);
}

Operand LowirFunctionLowering::load(const Operand& storage, TypeId type)
{
	Instruction instruction;
	instruction.kind = Instruction::IK_LOAD;
	instruction.type = unit_.low_type(type);
	instruction.first = storage;
	return emit(instruction);
}

void LowirFunctionLowering::store(const Operand& value, const Operand& storage,
                                  TypeId type)
{
	Instruction instruction;
	instruction.kind = Instruction::IK_STORE;
	instruction.type = unit_.low_type(type);
	instruction.first = value;
	instruction.second = storage;
	emit_void(instruction);
}

Operand LowirFunctionLowering::literal_operand(TypeId type,
                                               unsigned long long bits)
{
	Operand operand;
	operand.kind = Operand::OP_INTEGER;
	TypeTable& types = unit_.types();
	if (types.is_floating(types.strip_cv(type)))
	{
		// 2.14.4: a floating zero is spelled at the width it is written for,
		// which is what the suffix of a LowIR floating literal says - and it is
		// asked of the one owner of that suffix, so the zero of an `f32` is
		// spelled the way every other `f32` value in the output is.  Every
		// floating value that is not this zero was spelled by the program and
		// reaches the output as `fact.spelling`, so no other one is asked for
		// here.
		operand.kind = Operand::OP_FLOAT;
		operand.text = unit_.spell_floating(type, "0.0");
		return operand;
	}
	const unsigned long long value = unit_.narrowed(type, bits);
	operand.text = unit_.is_signed(type)
		? signed_decimal(static_cast<long long>(value))
		: decimal(value);
	return operand;
}

// 8.5p7: the value an object of scalar type is value-initialized with, which is
// the zero of its type.  For a pointer that zero is 4.10p1's null pointer value
// and LowIR spells it `nullptr`, which is not the same operand as the `0` a
// program writing the null pointer constant wrote: the one is a value of the
// object's own type and the other is the integer 4.10p1 converts from, and the
// references write each where it belongs.  Every other type shares the literal
// its own zero is.
Operand LowirFunctionLowering::zero_operand(TypeId type)
{
	TypeTable& types = unit_.types();
	if (types.kind(types.strip_cv(type)) == TypeKind::Pointer)
	{
		return named_operand(Operand::OP_INTEGER, "nullptr");
	}
	return literal_operand(type, 0);
}

// ------------------------------------------------------------------- slots

std::string LowirFunctionLowering::add_slot(const SemaEntity& entity,
                                            TypeId type, const char* unnamed)
{
	// 3.3.3p4: a name declared in a block hides one of the same name outside
	// it, so two slots of one function can be named after one identifier.  The
	// second and later of them are given a suffix, which keeps every slot name
	// distinct without renaming the one that got there first.
	std::string name = entity.name;
	if (name.empty())
	{
		name = std::string(unnamed) + decimal(out_.params.size());
	}
	// The suffix already given to that identifier is where the next one starts,
	// so a function that declares one name in n blocks names n slots in n
	// steps rather than in n^2: without it the k-th `x` walks every suffix
	// before it before finding the one that is free.
	std::string chosen = name;
	unsigned& shadow = slot_shadows_[name];
	while (!slot_names_.insert(chosen).second)
	{
		shadow = shadow < 2 ? 2 : shadow + 1;
		chosen = name + "__shadow" + decimal(shadow);
	}
	out_.slots.push_back(std::make_pair(chosen, unit_.low_type(type)));
	name_object(entity.id, chosen);
	return chosen;
}

std::string LowirFunctionLowering::add_generated_slot(const char* prefix,
                                                      TypeId type)
{
	return add_generated_slot(prefix, unit_.low_type(type));
}

std::string LowirFunctionLowering::add_generated_slot(
	const char* prefix, const lowir_model::LowType& type)
{
	// A generated slot must not be reachable from a source name, so its number
	// keeps rising until it names storage nothing in the function already does.
	std::string chosen;
	do
	{
		++generated_slots_;
		chosen = std::string(prefix) + "__" + decimal(generated_slots_);
	}
	while (!slot_names_.insert(chosen).second);
	out_.slots.push_back(std::make_pair(chosen, type));
	return chosen;
}

// 12.2p1: this object stands here for every reader of it, and this is the one
// place that is written down - so an element run standing over it is also the
// one place that has to say the reader it is written for is *this* element's.
void LowirFunctionLowering::place_object(std::uint32_t entity, const Operand& at)
{
	placed_[entity] = at;
	if (element_runs_ != 0)
	{
		element_objects_.push_back(entity);
	}
}

void LowirFunctionLowering::name_object(std::uint32_t entity,
                                        const std::string& slot)
{
	slots_[entity] = slot;
	if (element_runs_ != 0)
	{
		element_objects_.push_back(entity);
	}
}

// 12.6p1 and 12.8p15/p28: one element's run of a step written once for the
// whole array.  The lines the run writes are the element's, and so is every
// object the step creates while it runs - the storage a by-value parameter
// stands in, a temporary a default argument asked for - because the next element
// runs the same step and 8.3.6p9 evaluates that default argument again.  So a
// run drops what it placed at its end, which leaves the next run finding no
// object of its own already standing and building it where this one built its.
std::size_t LowirFunctionLowering::open_element_run()
{
	++element_runs_;
	return element_objects_.size();
}

void LowirFunctionLowering::close_element_run(std::size_t opened)
{
	for (std::size_t index = opened; index < element_objects_.size(); ++index)
	{
		placed_.erase(element_objects_[index]);
		slots_.erase(element_objects_[index]);
	}
	element_objects_.resize(opened);
	--element_runs_;
}

LowValue LowirFunctionLowering::storage_of(const SemaEntity& entity)
{
	LowValue value;
	value.type = entity.type;
	value.lvalue = true;
	const std::unordered_map<std::uint32_t, Operand>::const_iterator standing =
		placed_.find(entity.id);
	if (standing != placed_.end())
	{
		// 12.8p31: the object was created in storage the place asking for it
		// owned, so what names it is that address rather than a slot.
		value.operand = standing->second;
		return value;
	}
	const std::unordered_map<std::uint32_t, std::string>::const_iterator found =
		slots_.find(entity.id);
	if (found != slots_.end())
	{
		value.operand = named_operand(Operand::OP_SLOT, found->second);
	}
	else
	{
		unit_.declare_entity(entity);
		// 3.7.2p2: there is no point in the program before every thread that
		// names a thread-local object, so what initializes one runs where the
		// object is named rather than before the program.  The body it runs
		// does nothing after the first time a thread reaches it.
		const std::string* const initializer =
			unit_.thread_initializer_of(entity);
		if (initializer != nullptr)
		{
			Instruction start;
			start.kind = Instruction::IK_CALL;
			start.type.text = "void";
			start.first = named_operand(Operand::OP_GLOBAL, *initializer);
			emit_void(start);
		}
		value.operand =
			named_operand(Operand::OP_GLOBAL, unit_.global_symbol(entity));
	}
	TypeTable& types = unit_.types();
	if (types.is_reference(entity.type))
	{
		// 8.3.2p5: a name of reference type is an lvalue naming what the
		// reference is bound to, and the reference itself holds that address.
		value.type = types.target(entity.type);
		value.operand = load(value.operand, entity.type);
		// 4.2p1: the lvalue the name stands for is the array itself, so a
		// pointer view of it is a conversion the source wrote a name at - the
		// same place a member of reference type marks - and not an address a
		// computation already had in hand.
		value.named = true;
	}
	return value;
}

// -------------------------------------------------------------- conversion

Operand LowirFunctionLowering::rvalue(const LowValue& value)
{
	if (value.has_held)
	{
		return value.held;
	}
	if (!value.lvalue)
	{
		return value.operand;
	}
	if (value.field != nullptr)
	{
		return read_bit_field(value);
	}
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(value.type);
	if (types.kind(bare) == TypeKind::Array ||
	    types.kind(bare) == TypeKind::Function)
	{
		return decay(value);
	}
	return load(value.operand, value.type);
}

// 9.6p2: whether the field owns every bit of its storage unit, in which case
// the mask that keeps its own bits keeps all of them and the mask that keeps
// the others keeps none - so neither is written.
bool LowirFunctionLowering::fills_unit(const SemaEntity& field, TypeId type)
{
	return field.bit_offset == 0 &&
		field.bit_width >= 8 * unit_.width(type);
}

// 9.6p2: the value a bit-field holds.  The unit it sits in is loaded at the
// type the unit is read with, the field's own bits are brought down to the
// bottom of it, and everything above the width the declaration wrote is masked
// away.  What the value is worth keeps the type the member was declared with,
// so a conversion above this one is the one that type asks for.
Operand LowirFunctionLowering::read_bit_field(const LowValue& value)
{
	const SemaEntity& field = *value.field;
	const TypeId access = field.bit_access;
	Operand held = load(value.operand, access);
	if (field.bit_offset != 0)
	{
		Instruction down;
		down.kind = Instruction::IK_BINARY;
		down.op = "shr";
		down.type = unit_.low_type(access);
		down.first = held;
		down.second =
			named_operand(Operand::OP_INTEGER, decimal(field.bit_offset));
		held = emit(down);
	}
	if (fills_unit(field, access))
	{
		return held;
	}
	Instruction keep;
	keep.kind = Instruction::IK_BINARY;
	keep.op = "and";
	keep.type = unit_.low_type(access);
	keep.first = held;
	keep.second = named_operand(
		Operand::OP_INTEGER,
		mask_bits(field_mask(field), unit_.width(access)));
	return emit(keep);
}

// 4.5p1 and 5p9: one operation on values of `type`, written the way the source
// would have written it - each operand promoted, the operation at the promoted
// type, and 4.7p3 converting the result back.  An initialization computes the
// unit as an expression of the member's own type, so a member narrower than
// `int` is joined together at `int` like any other operand would be; a constant
// is the same value at either width and needs nothing to reach it.
Operand LowirFunctionLowering::field_binary(const char* op, TypeId type,
                                            const Operand& left,
                                            const Operand& right)
{
	TypeTable& types = unit_.types();
	const TypeId wide = unit_.width(type) < unit_.width(types.fundamental(FT_INT))
		? types.fundamental(FT_INT)
		: type;
	Instruction instruction;
	instruction.kind = Instruction::IK_BINARY;
	instruction.op = op;
	instruction.type = unit_.low_type(wide);
	instruction.first = promoted_operand(left, type, wide);
	instruction.second = promoted_operand(right, type, wide);
	const Operand held = emit(instruction);
	if (wide == type)
	{
		return held;
	}
	LowValue value;
	value.type = wide;
	value.operand = held;
	return converted(value, type);
}

Operand LowirFunctionLowering::promoted_operand(const Operand& held, TypeId from,
                                                TypeId to)
{
	if (from == to || held.kind == Operand::OP_INTEGER)
	{
		return held;
	}
	LowValue value;
	value.type = from;
	value.operand = held;
	return converted(value, to);
}

// 9.6p2: the value `held` masked to the width the field was declared with and
// moved up to where the field's bits sit in its storage unit, which is what
// both a write and an initialization put into the unit.  8.5.1's write spells
// the mask before the value and 5.17's the value before the mask, which is the
// one difference between the two shapes the references write.
Operand LowirFunctionLowering::placed_bits(const SemaEntity& field,
                                           const Operand& held, TypeId type,
                                           bool initializer)
{
	Operand bits = held;
	if (!fills_unit(field, type))
	{
		const Operand mask = named_operand(
			Operand::OP_INTEGER,
			mask_bits(field_mask(field), unit_.width(type)));
		if (initializer)
		{
			bits = field_binary("and", type, mask, held);
		}
		else
		{
			Instruction keep;
			keep.kind = Instruction::IK_BINARY;
			keep.op = "and";
			keep.type = unit_.low_type(type);
			keep.first = held;
			keep.second = mask;
			bits = emit(keep);
		}
	}
	if (field.bit_offset == 0)
	{
		return bits;
	}
	const Operand up =
		named_operand(Operand::OP_INTEGER, decimal(field.bit_offset));
	if (initializer)
	{
		return field_binary("shl", type, bits, up);
	}
	Instruction shift;
	shift.kind = Instruction::IK_BINARY;
	shift.op = "shl";
	shift.type = unit_.low_type(type);
	shift.first = bits;
	shift.second = up;
	return emit(shift);
}

// 8.5p7 and 9.6p2: whether the bytes [first, last) of the object being
// initialized are still the initialization's to give zero to.  An
// initialization writes the subobjects in the order 9.2p13 laid them out, so
// what it has already written is everything below one byte, and a bit-field
// whose storage unit starts at or after that byte owns every bit of the unit -
// the ones no field of it was given among them.
bool LowirFunctionLowering::claims_storage(unsigned long long first,
                                           unsigned long long last)
{
	if (written_through_.empty())
	{
		written_through_.push_back(0);
	}
	const bool owns = first >= written_through_.back();
	if (last > written_through_.back())
	{
		written_through_.back() = last;
	}
	return owns;
}

unsigned long long LowirFunctionLowering::subobject_offset(
	const std::vector<const DumpNode*>& path) const
{
	TypeTable& types = unit_.types();
	unsigned long long at = 0;
	for (std::size_t index = 0; index < path.size(); ++index)
	{
		const DumpNode& step = *path[index];
		at += step.fact.entity != nullptr
			? step.fact.entity->offset
			: step.fact.value * types.object_size(step.fact.type);
	}
	return at;
}

// 8.5.1 and 12.6.2: `held` written into a bit-field of the object being
// initialized.  The place is named from the object again for the load and for
// the store, because that is the one description of where the field is that
// does not depend on what is being put in it - and it is what the references
// write.
void LowirFunctionLowering::initialize_bit_field(
	const SemaEntity& field, const LowObject& object,
	const std::vector<const DumpNode*>& path, unsigned long long unit,
	const Operand& held, TypeId type)
{
	TypeTable& types = unit_.types();
	if (claims_storage(unit, unit + types.object_size(types.strip_cv(type))) ||
	    fills_unit(field, type))
	{
		// 8.5p6: every bit of the unit is being initialized here, so the bits
		// beside the field's take the zero the initialization gives them.
		const Operand bits = placed_bits(field, held, type, true);
		store(bits, subobject_address(object, path), type);
		return;
	}
	const Operand at = subobject_address(object, path);
	const Operand kept = field_binary(
		"and", type, load(at, type),
		named_operand(Operand::OP_INTEGER,
		              mask_bits(~(field_mask(field) << field.bit_offset),
		                        unit_.width(type))));
	const Operand bits = placed_bits(field, held, type, true);
	// The unit is joined before it is named again, because the value is what
	// the initialization computes and the place is where it then goes.
	const Operand whole = field_binary("or", type, kept, bits);
	store(whole, subobject_address(object, path), type);
}

// 5.17 and 5.3.2: `held` written into the field an lvalue names, whose unit the
// caller already holds.  9.6p2 leaves the bits beside the field alone, which is
// what the read-modify-write is for.
void LowirFunctionLowering::assign_bit_field(const LowValue& target,
                                             const Operand& held, TypeId type)
{
	const SemaEntity& field = *target.field;
	const Operand bits = placed_bits(field, held, type, false);
	Instruction keep;
	keep.kind = Instruction::IK_BINARY;
	keep.op = "and";
	keep.type = unit_.low_type(type);
	keep.first = load(target.operand, type);
	if (fills_unit(field, type))
	{
		// 9.6p2: the unit holds nothing beside the field's own bits, so there
		// is nothing to put back and the value is the whole of what it holds.
		store(bits, target.operand, type);
		return;
	}
	keep.second = named_operand(
		Operand::OP_INTEGER,
		mask_bits(~(field_mask(field) << field.bit_offset),
		          unit_.width(type)));
	Instruction join;
	join.kind = Instruction::IK_BINARY;
	join.op = "or";
	join.type = unit_.low_type(type);
	join.first = emit(keep);
	join.second = bits;
	store(emit(join), target.operand, type);
}

LowValue LowirFunctionLowering::as_value(const LowValue& value)
{
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(value.type);
	LowValue out = value;
	out.lvalue = false;
	out.has_held = false;
	// 4.1: what the field held is a value now, and a value is not in any
	// storage unit, so nothing below reads it as a run of bits again.
	out.field = nullptr;
	if (types.kind(bare) == TypeKind::Array)
	{
		out.operand = decay(value);
		out.type = types.pointer_to(types.target(bare));
		out.unnamed = false;
		return out;
	}
	if (types.kind(bare) == TypeKind::Function)
	{
		out.operand = decay(value);
		out.type = types.pointer_to(bare);
		return out;
	}
	out.operand = rvalue(value);
	return out;
}

Operand LowirFunctionLowering::address_of(const LowValue& value)
{
	if (value.field != nullptr)
	{
		// 5.3.1p3 and 9.6p3: a bit-field has no address of its own, so there is
		// nothing for the built-in `&` to produce and nothing for a reference
		// or a pointer to be bound to.
		throw std::runtime_error("the address of a bit-field is taken, which "
		                         "9.6p3 gives no address to");
	}
	if (value.operand.kind == Operand::OP_TEMP)
	{
		if (!value.lvalue &&
		    unit_.types().is_class(unit_.types().strip_cv(value.type)))
		{
			// 12.2p1: a prvalue of class type is an object, and a call that
			// returns one by value hands back what it holds rather than
			// storage holding it.  No declaration named that object, so the
			// function gives it storage here - and everything that reads the
			// prvalue as an object reads that storage.
			return materialized_class_value(value);
		}
		return value.operand;
	}
	Instruction instruction;
	instruction.kind = Instruction::IK_ADDR;
	instruction.first = value.operand;
	return emit(instruction);
}

// 12.2p1: the storage a class prvalue no declaration named is given, which is
// one slot of the function with the value copied into it.  The slot is named
// after the expression that wrote the prvalue, because nothing else asked for
// it: a place that needs a copy of its own - an argument, a return - names its
// own storage and copies the value straight into that.
Operand LowirFunctionLowering::materialized_class_value(const LowValue& value)
{
	const TypeId type = unit_.types().strip_cv(value.type);
	const std::string slot = add_generated_slot("tmpobj", type);
	LowValue held;
	held.type = type;
	held.lvalue = true;
	held.operand = named_operand(Operand::OP_SLOT, slot);
	Instruction instruction;
	instruction.kind = Instruction::IK_ADDR;
	instruction.first = held.operand;
	const Operand at = emit(instruction);
	copy_class_object(at, value.operand, type);
	return at;
}

Operand LowirFunctionLowering::decay(const LowValue& value)
{
	// 4.2 and 4.3: the array or function is where it is, and the pointer view
	// of it is the address of its storage.  `unary decay` is what marks that
	// point for a later pass instead of leaving it to be reconstructed.
	TypeTable& types = unit_.types();
	if (value.unnamed)
	{
		// No declaration named this object, so there is no point where a name
		// of it became a pointer view of it.
		return address_of(value);
	}
	if (value.operand.kind == Operand::OP_TEMP && !value.named &&
	    types.kind(types.strip_cv(value.type)) != TypeKind::Function)
	{
		// The address is already in hand as a pointer, and no name of the array
		// stands here, so there is no point at which one became a pointer view
		// of it.  4.3 is not the same: a function is not an object, so the
		// pointer view of one is marked wherever the function is named.
		return value.operand;
	}
	Instruction instruction;
	instruction.kind = Instruction::IK_UNARY;
	instruction.op = "decay";
	instruction.type.text = "ptr";
	instruction.first = address_of(value);
	return emit(instruction);
}

Operand LowirFunctionLowering::convert_scalar(const Operand& operand,
                                              TypeId from, TypeId to)
{
	TypeTable& types = unit_.types();
	const lowir_model::LowType source = unit_.low_type(from);
	const lowir_model::LowType target = unit_.low_type(to);
	// 3.9.1p2: LowIR spells both eight byte integral types `i64` and says which
	// operator reads a value as unsigned, so two types the same width apart in
	// signedness share one spelling.  4.7p2's conversion between them is still
	// a reading of the value at another type, which LowIR names with a copy -
	// what shares a spelling and nothing else is the same bits and not the same
	// operand.
	if (source.text == target.text &&
	    (unit_.is_signed(from) == unit_.is_signed(to) ||
	     !types.is_integral(types.strip_cv(from)) ||
	     !types.is_integral(types.strip_cv(to))))
	{
		return operand;
	}
	const bool from_float = types.is_floating(types.strip_cv(from));
	const bool to_float = types.is_floating(types.strip_cv(to));
	const bool to_pointer = target.text == "ptr";
	const bool from_pointer = source.text == "ptr";
	Instruction instruction;
	if (to_pointer != from_pointer ||
	    (!from_float && !to_float && unit_.width(to) == unit_.width(from)))
	{
		// 5.2.10 and 4.7p2: an address read as an integer, an integer read as
		// an address, and an integer read at its own width with the other
		// signedness are all the same bits under a new type.  LowIR spells that
		// as a copy, because no conversion operator names it.
		instruction.kind = Instruction::IK_COPY;
		instruction.type = target;
		instruction.first = operand;
		return emit(instruction);
	}
	if (to_pointer)
	{
		// 4.10: a pointer conversion changes what the address means and not the
		// address, so there is nothing to compute.
		return operand;
	}
	instruction.kind = Instruction::IK_CONVERT;
	instruction.type = target;
	instruction.source_type = source;
	instruction.first = operand;
	if (from_float && to_float)
	{
		instruction.op = unit_.width(to) > unit_.width(from) ? "fpext" : "fptrunc";
	}
	else if (from_float)
	{
		instruction.op = unit_.is_signed(to) ? "fptosi" : "fptoui";
	}
	else if (to_float)
	{
		instruction.op = unit_.is_signed(from) ? "sitofp" : "uitofp";
	}
	else if (unit_.width(to) > unit_.width(from))
	{
		instruction.op = unit_.is_signed(from) ? "sext" : "zext";
	}
	else
	{
		instruction.op = "trunc";
	}
	return emit(instruction);
}

Operand LowirFunctionLowering::converted(const LowValue& value, TypeId target)
{
	TypeTable& types = unit_.types();
	const TypeId wanted = types.strip_cv(
		types.is_reference(target) ? types.target(target) : target);
	if (types.is_reference(target))
	{
		if (value.lvalue || value.operand.kind != Operand::OP_TEMP)
		{
			return address_of(value);
		}
		// 8.5.3p5: a reference that does not bind the operand itself binds a
		// temporary holding the conversion of it, which is an object of the
		// function and so has storage of its own.
		const std::string slot = add_generated_slot("refarg", wanted);
		const Operand storage = named_operand(Operand::OP_SLOT, slot);
		store(convert_scalar(value.operand, value.type, wanted), storage, wanted);
		LowValue held;
		held.type = wanted;
		held.lvalue = true;
		held.operand = storage;
		return address_of(held);
	}
	if (types.is_class(wanted))
	{
		// 12.8p15: an object of class type is not a value a conversion can
		// produce - it is storage, and a copy of it is written into storage the
		// place that asked for the copy owns.  Every such place names its own:
		// 5.2.2p4's argument, 6.6.3p2's returned object, 5.16's arm and the
		// object an initialization or an assignment writes into.  Reaching here
		// means a place read a class as a value without owning one.
		throw std::runtime_error(
			"a value of the class type " + types.description(wanted) +
			" is read where this milestone has no object to hold the copy "
			"12.8p15 makes of it");
	}
	const TypeId bare = types.strip_cv(value.type);
	if (types.kind(bare) == TypeKind::Array || types.kind(bare) == TypeKind::Function)
	{
		// 4p1: clause 4 is a sequence, and 4.2 and 4.3 stand first.  The array
		// or function is the pointer it converts to, and what the target asks
		// for is asked of that pointer - which for `bool` is 4.12 and not the
		// address itself.
		return converted(as_value(value), target);
	}
	const Operand operand = rvalue(value);
	if (types.strip_cv(value.type) == types.fundamental(FT_NULLPTR_T) &&
	    types.kind(wanted) == TypeKind::Pointer)
	{
		// 4.10p1: the null pointer value of `std::nullptr_t` read as a pointer
		// of the type it is converted to.
		Instruction instruction;
		instruction.kind = Instruction::IK_COPY;
		instruction.type.text = "ptr";
		instruction.first = operand;
		return emit(instruction);
	}
	if (types.strip_cv(value.type) == wanted)
	{
		return operand;
	}
	if (value.constant && types.kind(wanted) == TypeKind::Pointer &&
	    types.is_integral(types.strip_cv(value.type)))
	{
		// 4.10p1: the constant is the null pointer value it stands for, and
		// nothing computes it.
		return named_operand(Operand::OP_INTEGER, "0");
	}
	if (value.constant && types.is_integral(wanted) &&
	    types.is_integral(types.strip_cv(value.type)) &&
	    !(types.kind(wanted) == TypeKind::Fundamental &&
	      types.fundamental_type(wanted) == FT_BOOL) &&
	    (unit_.is_signed(wanted) ||
	     unit_.width(wanted) <= unit_.width(value.type)))
	{
		// An immediate read at another width is the immediate it reads as, so
		// the conversion is spelled as the value it produces rather than as the
		// operation that would produce it.  A widening to an unsigned type is
		// not one of those: what a negative immediate widens to is a value the
		// decimal it was spelled with does not name, and 4.12's conversion to
		// `bool` is a comparison rather than the same value at another width.
		return literal_operand(wanted, value.value);
	}
	if (types.kind(wanted) == TypeKind::Fundamental &&
	    types.fundamental_type(wanted) == FT_BOOL)
	{
		// 4.12: a value converts to `bool` by comparing it with zero.  The
		// comparison is written at the type of what it compares - which is
		// what this conversion has and 5.14p1's operand, already a truth
		// value, does not - and `bool` holds the one byte its object is stored
		// in, so what the comparison produced is read at that width.
		const lowir_model::LowType low = unit_.low_type(bare);
		Instruction test;
		test.kind = Instruction::IK_CMP;
		test.op = "ne";
		test.type = low;
		test.first = operand;
		test.second = low.text == "ptr" || low.text[0] == 'f'
			? literal_operand(bare, 0)
			: named_operand(Operand::OP_INTEGER, "0");
		Instruction narrow;
		narrow.kind = Instruction::IK_COPY;
		narrow.type = unit_.low_type(wanted);
		narrow.first = emit(test);
		return emit(narrow);
	}
	return convert_scalar(operand, value.type, wanted);
}

// 8.5p14: the value an initializer gives the object it initializes, which is
// what the initializer is worth read as the type the object holds.  That is
// 4's conversion and nothing else, so it is asked for in the one place every
// other reading of a value at another type asks: an immediate read at a width
// it names the same value at is that immediate, and a widening to an unsigned
// type or 4.12's comparison with zero is the operation that produces it.
Operand LowirFunctionLowering::initializer_value(const LowValue& value,
                                                 TypeId target)
{
	return converted(value, target);
}

Operand LowirFunctionLowering::truth_for_branch(const LowValue& value)
{
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(value.type);
	if (types.kind(bare) == TypeKind::Array || types.kind(bare) == TypeKind::Function)
	{
		// 4.2 and 4.3: what a branch tests is the pointer the array or function
		// became, which 4.12 then compares with zero like any other.
		return truth_for_branch(as_value(value));
	}
	const Operand operand = rvalue(value);
	const lowir_model::LowType low = unit_.low_type(bare);
	if (low.text[0] != 'f')
	{
		// A branch tests its operand for being other than zero, which for an
		// integer or an address is what the value already is - 4.12p1's
		// conversion to `bool` writes no instruction where the terminator is
		// the only thing that reads it.  A floating value is compared, because
		// its zero is not the zero bit pattern the terminator would test.
		return operand;
	}
	Instruction instruction;
	instruction.kind = Instruction::IK_CMP;
	instruction.op = "ne";
	instruction.type = low;
	instruction.first = operand;
	instruction.second = literal_operand(bare, 0);
	return emit(instruction);
}

Operand LowirFunctionLowering::truth_value(const LowValue& value)
{
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(value.type);
	if (types.kind(bare) == TypeKind::Array || types.kind(bare) == TypeKind::Function)
	{
		return truth_value(as_value(value));
	}
	const lowir_model::LowType low = unit_.low_type(bare);
	const bool addressed = low.text == "ptr";
	Instruction instruction;
	instruction.kind = Instruction::IK_CMP;
	instruction.op = "ne";
	// A comparison is written at the type of what it compares.  5.14p1's truth
	// value is materialized at the width LowIR gives one rather than at the
	// width `bool` is stored at, so an operand that is already one is compared
	// at that width and every other at its own.
	// 5.14p1: the truth of an operand is one canonical integer value, which
	// LowIR materializes at its own width rather than at the width the operand
	// was stored at.  A pointer and a floating value are compared as they are.
	instruction.type.text = low.text[0] == 'f' || addressed ? low.text : "i64";
	instruction.first = rvalue(value);
	instruction.second = literal_operand(bare, 0);
	if (!addressed && low.text[0] != 'f')
	{
		instruction.second = named_operand(Operand::OP_INTEGER, "0");
	}
	return emit(instruction);
}

// -------------------------------------------------------------- statements

void LowirFunctionLowering::run(const DumpNode& node, TypeId type,
                                unsigned variant)
{
	TypeTable& types = unit_.types();
	returns_ = types.target(type);
	// 6.6.3p2: the caller named the storage the returned object stands in, and
	// the signature already wrote the parameter that carries it.  Every return
	// of this function creates its object there.
	indirect_result_ = !out_.params.empty() &&
		out_.params[0].metadata.passing == lowir_model::PPM_INDIRECT_RESULT;
	if (indirect_result_)
	{
		result_object_ = named_operand(Operand::OP_TEMP, out_.params[0].name);
		taken_.insert(out_.params[0].name);
		return_slot_local_ = return_slot_local(node);
	}
	open_block("entry");
	// 15.2p2 and 12.4p8: the destructions that follow the destructor's own body
	// are read before the body is, because the two blocks they stand between
	// are numbered before the blocks the body opens.
	const std::size_t body_end = collect_epilogue(node);
	const SemaEntity* const written = node.fact.entity;
	if (written != nullptr && written->special == kDestructorFunction &&
	    written->region != nullptr && written->region->owner != nullptr)
	{
		destroyed_class_ = written->region->owner->type;
	}
	if (variant == kDeletingObjectAbi && written != nullptr &&
	    written->deleting_release != nullptr)
	{
		// 5.3.5p3: this entry point runs the destructor on the complete object
		// and then gives its storage back, so the deallocation is the last step
		// of 12.4p8's suffix - which is what makes 15.2p2's handler for every
		// step before it end with the storage going back too.
		LowDestruction release;
		release.release = written->deleting_release;
		epilogue_.push_back(release);
	}
	if (!epilogue_.empty())
	{
		destructor_cleanup_ = reserve_block("destructor_cleanup");
		destructor_end_ = reserve_block("destructor_end");
	}
	const std::vector<TypeId>& declared = types.parameters(type);
	std::size_t index = 0;
	for (; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		if (child.fact.kind != FactKind::Parameter)
		{
			break;
		}
		const TypeId written = index < declared.size() ? declared[index]
		                                              : child.fact.type;
		lowir_model::Parameter parameter;
		unit_.describe_parameter(written, parameter);
		// 1.4p8 and 3.7.4.2p2: a program that replaces one of the functions the
		// implementation declared writes another declaration of it, and a
		// parameter its definition left unnamed keeps the name the
		// implementation's own declaration already gave it.
		parameter.name = add_slot(*child.fact.entity, written,
		                          node.fact.entity != nullptr &&
		                                  node.fact.entity->builtin != kNotBuiltin
		                              ? "arg"
		                              : "__param");
		taken_.insert(parameter.name);
		out_.params.push_back(parameter);
		if (parameter.metadata.passing == lowir_model::PPM_BY_ADDRESS)
		{
			// 5.2.2p4: the parameter is the object the caller built, standing
			// where the caller put it - so the body reaches it through the
			// address it was passed and nothing is copied into a second place.
			const Operand at = named_operand(Operand::OP_TEMP, parameter.name);
			place_object(child.fact.entity->id, at);
			// 15.2p2 and 12.4p5: it stands from the moment the body begins and
			// its end is this function's to write, so an exception out of
			// anything the body does has to end it - the same standing object a
			// declaration would have made, entered where the function does.
			begin_object_lifetime(child, unwind_mark_, at,
			                      std::vector<Instruction>());
			continue;
		}
		if (types.is_class(types.strip_cv(written)))
		{
			// 5.1.1p8 and 12.8p15: a parameter of class type is an object of
			// the function like any other, and what it holds is what the caller
			// copied into the storage it was passed - so a class that holds
			// nothing has nothing to be written into it, and neither the copy
			// nor the address it would be made at is computed.
			if (types.is_empty_class(types.strip_cv(written)))
			{
				continue;
			}
			LowValue held;
			held.type = written;
			held.lvalue = true;
			held.operand = named_operand(Operand::OP_SLOT, parameter.name);
			// The course ABI passes an object of class type as the storage it
			// occupies, which the caller already ran 12.8p15's copy into - so
			// what the body owes is the payload moved into the storage a name
			// reaches, and not a second copy of the object.
			copy_object_storage(address_of(held),
			                    named_operand(Operand::OP_TEMP, parameter.name),
			                    written);
			continue;
		}
		// 5.1.1p8: a parameter is an object of the function, so its value is
		// written into the storage the body reads it from.
		store(named_operand(Operand::OP_TEMP, parameter.name),
		      named_operand(Operand::OP_SLOT, parameter.name), written);
	}
	if (!epilogue_.empty())
	{
		// 15.2p2: the whole of the destructor's body stands in one cleanup
		// region, because an exception out of it leaves an object every
		// subobject of which is still alive.
		emit_handler(true, destructor_cleanup_);
	}
	// 12.4p11: an object being destroyed is an object of the class whose
	// destructor is running, from the moment that destructor begins - so the
	// vpointer names this class's table before the body runs and before the
	// base subobject's own destructor names the base's.
	const SemaEntity* vpointer_owner =
		written != nullptr && written->region != nullptr &&
			written->special != kOrdinaryFunction &&
			written->region->owner != nullptr &&
			written->region->owner->polymorphic
		? written->region->owner
		: nullptr;
	if (vpointer_owner != nullptr && written->special == kDestructorFunction)
	{
		write_vpointer(*vpointer_owner);
		vpointer_owner = nullptr;
	}
	// 15.2p2: 12.6.2's initializations are the one place a partly built object
	// exists, so the steps before the body are the ones a handler stands around
	// and the body itself is not.
	unwinding_ = node.fact.entity != nullptr &&
	             node.fact.entity->special == kConstructorFunction;
	for (; index < body_end; ++index)
	{
		const DumpNode& child = *node.children[index];
		if (vpointer_owner != nullptr &&
		    !(child.fact.kind == FactKind::ConstructorAction &&
		      child.fact.base_subobject))
		{
			// 12.6.2p10 and 12.7p3: the base subobject is built first and is an
			// object of the base while its own constructor runs, so the store
			// stands between that step and everything this class's own
			// constructor then initializes.
			write_vpointer(*vpointer_owner);
			vpointer_owner = nullptr;
		}
		if (child.fact.kind == FactKind::Compound)
		{
			unwinding_ = false;
		}
		if (!unwinding_ || (child.fact.kind != FactKind::ConstructorAction &&
		                    child.fact.kind != FactKind::MemberInitialization))
		{
			statement(child);
			continue;
		}
		// 1.9p10 and 12.6.2: one mem-initializer is a full-expression, so the
		// temporaries it wrote are destroyed once the subobject it built
		// stands - which is what the actions written after it are.
		open_full_expression();
		statement(child);
		while (index + 1 < body_end &&
		       node.children[index + 1]->fact.kind == FactKind::DestructorAction)
		{
			++index;
			statement(*node.children[index]);
		}
		close_full_expression();
	}
	unwinding_ = false;
	if (vpointer_owner != nullptr)
	{
		// A constructor whose class initializes nothing after its base still
		// leaves the object one of this class.
		write_vpointer(*vpointer_owner);
	}
	if (!epilogue_.empty())
	{
		if (!terminated())
		{
			destructor_epilogue();
			jump(destructor_end_);
		}
		open_block(destructor_cleanup_);
		for (std::size_t at = 0; at < epilogue_.size(); ++at)
		{
			// 15.2p2: the exception left the body, so every subobject is still
			// alive and every one of them is destroyed.
			epilogue_step(epilogue_[at]);
		}
		emit_handler_end();
		emit_resume();
		open_block(destructor_end_);
	}
	if (!terminated())
	{
		// 6.6.3p2 and 3.6.1p5: falling off the end of `main` returns zero, and
		// of any other function returns nothing the caller reads.
		Instruction instruction;
		instruction.kind = Instruction::IK_RETURN;
		instruction.type = out_.return_type;
		if (!indirect_result_)
		{
			instruction.first = literal_operand(returns_, 0);
		}
		terminate(instruction);
	}
}

const SemaEntity* LowirFunctionLowering::returned_local(const DumpNode& node)
{
	// 3.8p1: a lifetime ends where control leaves the block that named the
	// object, and the actions that end one stand under the return.  They are
	// no bar to 12.8p31: the object this return names is one of them, and what
	// the elision says is that its end is the caller's and not this one's.
	if (node.children.empty())
	{
		return nullptr;
	}
	for (std::size_t index = 1; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind != FactKind::DestructorAction)
		{
			return nullptr;
		}
	}
	if (node.children[0]->fact.kind == FactKind::Id)
	{
		// 12.8p12 and 12.8p31: a class whose bytes stand for the object is
		// carried into the returned object by a copy of those bytes and no
		// transfer member stands over it, so the operand of the return is the
		// name itself.  It is the same object 12.8p31 lets the destination be:
		// what the elision leaves out is the copy, and there is one here
		// whether or not it is written as a call.
		return returned_name(*node.children[0]);
	}
	if (node.children[0]->fact.kind != FactKind::TemporaryObject)
	{
		return nullptr;
	}
	const DumpNode& temporary = *node.children[0];
	if (temporary.children.empty() ||
	    temporary.children[0]->fact.kind != FactKind::ConstructorAction)
	{
		return nullptr;
	}
	// 12.8p15: the returned object is initialized by one transfer member taking
	// one argument, and that argument is the object this return names.
	const DumpNode& call = *temporary.children[0]->children[0];
	if (call.children.size() != 3 || call.children[2]->fact.kind != FactKind::Id)
	{
		return nullptr;
	}
	return returned_name(*call.children[2]);
}

// 12.8p31: the automatic object a return names, where naming it is all the
// return does.  It is one declaration of this function's own, of the function's
// own returned type, and it is the one question both spellings of the return
// ask - the transfer member's argument and the bare name a copy of the bytes
// carries.
const SemaEntity* LowirFunctionLowering::returned_name(const DumpNode& node)
{
	const SemaEntity* const named = node.fact.entity;
	TypeTable& types = unit_.types();
	if (named == nullptr || named->kind != SemaKind::Variable ||
	    types.strip_cv(named->type) != types.strip_cv(returns_))
	{
		return nullptr;
	}
	return named;
}

const SemaEntity* LowirFunctionLowering::return_slot_local(
	const DumpNode& definition)
{
	// 6.6.3p2 and 12.8p31: the objects a declaration of the function's own
	// outermost block named, which are the ones whose storage may be the
	// destination the caller passed - one an inner block named is an object of
	// that block and ends where it does.
	std::unordered_set<std::uint32_t> outermost;
	const DumpNode* body = nullptr;
	for (std::size_t index = 0; index < definition.children.size(); ++index)
	{
		if (definition.children[index]->fact.kind == FactKind::Compound)
		{
			body = definition.children[index];
			break;
		}
	}
	if (body == nullptr)
	{
		return nullptr;
	}
	for (std::size_t index = 0; index < body->children.size(); ++index)
	{
		const DumpNode& statement = *body->children[index];
		if (statement.fact.kind != FactKind::SimpleDeclaration)
		{
			continue;
		}
		for (std::size_t at = 0; at < statement.children.size(); ++at)
		{
			const DumpNode& declared = *statement.children[at];
			if (declared.fact.kind == FactKind::Variable &&
			    declared.fact.entity != nullptr)
			{
				outermost.insert(declared.fact.entity->id);
			}
		}
	}
	// Every return of the function is read once, and the object they agree on -
	// if they agree at all - is the one the destination stands for.
	const SemaEntity* chosen = nullptr;
	std::vector<const DumpNode*> pending(1, body);
	while (!pending.empty())
	{
		const DumpNode& at = *pending.back();
		pending.pop_back();
		if (at.fact.kind == FactKind::Return)
		{
			const SemaEntity* const named = returned_local(at);
			if (named == nullptr || outermost.count(named->id) == 0 ||
			    (chosen != nullptr && chosen != named))
			{
				return nullptr;
			}
			chosen = named;
			continue;
		}
		for (std::size_t index = 0; index < at.children.size(); ++index)
		{
			pending.push_back(at.children[index]);
		}
	}
	return chosen;
}

// 12.4p8: the destructions written after a destructor's body, flattened to one
// entry per call, and where in the definition's children the body ends.  They
// are the trailing `destructor-action` children of the definition, which no
// other function has: every other end of a lifetime stands under the statement
// that reaches it.
std::size_t LowirFunctionLowering::collect_epilogue(const DumpNode& node)
{
	if (node.fact.entity == nullptr ||
	    node.fact.entity->special != kDestructorFunction)
	{
		return node.children.size();
	}
	std::size_t at = node.children.size();
	while (at > 0 &&
	       node.children[at - 1]->fact.kind == FactKind::DestructorAction)
	{
		--at;
	}
	for (std::size_t index = at; index < node.children.size(); ++index)
	{
		const DumpNode& action = *node.children[index];
		const unsigned long long total = destruction_steps(action);
		std::vector<TypeId> dimensions;
		std::vector<unsigned long long> bounds;
		const bool array = total > 1 ||
			unit_.types().kind(unit_.types().strip_cv(action.fact.type)) ==
				TypeKind::Array;
		if (array && total > kArrayLoopLimit)
		{
			// 12.4p8: the elements of the array are one entry of the suffix and
			// not one each, so the handlers 15.2p2 gives the suffix are counted
			// in the subobjects the class holds rather than in a bound the
			// source wrote as a number.
			LowDestruction whole;
			whole.action = &action;
			whole.element = true;
			whole.count = total;
			epilogue_.push_back(whole);
			continue;
		}
		for (unsigned long long step = 0; step < total; ++step)
		{
			LowDestruction one;
			one.action = &action;
			one.element = array;
			one.index = action.fact.reverse_elements ? total - 1 - step : step;
			epilogue_.push_back(one);
		}
	}
	return at;
}

// 6.4.2p1: whether a switch that holds `node` can reach a label inside it.  A
// label of a switch written inside `node` belongs to that switch and not to
// this one, so the walk does not enter one.
bool LowirFunctionLowering::holds_label(const DumpNode& node)
{
	if (node.fact.kind == FactKind::Case || node.fact.kind == FactKind::Default ||
	    node.fact.kind == FactKind::Label)
	{
		return true;
	}
	if (node.fact.kind == FactKind::Switch)
	{
		return false;
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (holds_label(*node.children[index]))
		{
			return true;
		}
	}
	return false;
}

void LowirFunctionLowering::open_generated(const GeneratedBody& state)
{
	returns_ = kNoType;
	temps_ = state.temps;
	blocks_ = state.blocks;
	generated_slots_ = state.slots;
	if (out_.blocks.empty())
	{
		open_block("entry");
		return;
	}
	// 3.6.2p2: the program has one initialization function, so a unit that adds
	// to a body another unit began takes up the block it left open rather than
	// opening a second entry.
	for (std::size_t index = 0; index < out_.slots.size(); ++index)
	{
		slot_names_.insert(out_.slots[index].first);
	}
	current_ = out_.blocks.size() - 1;
	open_ = true;
}

void LowirFunctionLowering::suspend_generated(GeneratedBody& state) const
{
	state.temps = temps_;
	state.blocks = blocks_;
	state.slots = generated_slots_;
}

void LowirFunctionLowering::statement(const DumpNode& node)
{
	if (terminated() && !holds_label(node))
	{
		// 6.3p2: a statement no path reaches contributes nothing to run.  A
		// case label is still reachable, because the switch that holds it
		// reaches it however the statements before it end, so a statement that
		// holds one is read even where control does not fall into it.
		return;
	}
	switch (node.fact.kind)
	{
	case FactKind::Compound:
		compound_statement(node);
		return;

	case FactKind::ExpressionStatement:
		expression_statement(node);
		return;

	case FactKind::Return:
		return_statement(node);
		return;

	case FactKind::If:
		if_statement(node);
		return;

	case FactKind::While:
		while_statement(node);
		return;

	case FactKind::Do:
		do_statement(node);
		return;

	case FactKind::For:
		for_statement(node);
		return;

	case FactKind::Switch:
		switch_statement(node);
		return;

	case FactKind::Case:
	case FactKind::Default:
		case_statement(node);
		return;

	case FactKind::Break:
		if (breaks_.empty())
		{
			throw std::runtime_error("a break statement leaves no statement");
		}
		// 3.8p1: the objects of the blocks the jump leaves are destroyed before
		// control reaches the statement it jumps to - on the path the jump
		// takes.  The code after it is reached by other paths, where those
		// objects still stand, so what an exception there owes is unchanged.
		{
			const std::vector<LowUnwind> standing = unwind_live_;
			const std::vector<std::string> handlers = dispatch_cache_;
			leave_blocks(node);
			unwind_live_ = standing;
			dispatch_cache_ = handlers;
			jump(breaks_.back());
		}
		return;

	case FactKind::Continue:
		if (continues_.empty())
		{
			throw std::runtime_error("a continue statement leaves no loop");
		}
		{
			const std::vector<LowUnwind> standing = unwind_live_;
			const std::vector<std::string> handlers = dispatch_cache_;
			leave_blocks(node);
			unwind_live_ = standing;
			dispatch_cache_ = handlers;
			jump(continues_.back());
		}
		return;

	case FactKind::Label:
	{
		// 6.1p1: the labeled statement is a place control can arrive at from
		// somewhere other than the statement before it, which is a block.
		const std::string label = goto_label(node.fact.spelling);
		if (!terminated())
		{
			jump(label);
		}
		open_block(label);
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			statement(*node.children[index]);
		}
		return;
	}

	case FactKind::Goto:
		jump(goto_label(node.fact.spelling));
		return;

	case FactKind::SimpleDeclaration:
		declaration_statement(node);
		return;

	case FactKind::Variable:
		local_variable(node);
		return;

	case FactKind::MemberInitialization:
		// 15.2p2: a member of class type an aggregate clause reaches is built
		// by a call of its own, and the step is where it begins.
		mark_unwind_step();
		member_initialization(node);
		return;

	case FactKind::ConstructorAction:
	{
		if (unit_.types().kind(unit_.types().strip_cv(node.fact.type)) ==
		    TypeKind::Array)
		{
			// 12.6p1: the object being initialized is each element of the
			// array, and the constructor runs on each of them.
			array_lifecycle(node, true);
			return;
		}
		// 12.6.2: a subobject of class type is initialized by running its
		// constructor on it, and the action already names the subobject.
		// 12.6.2p6's delegation names the whole object instead, which `this`
		// already points at - so what the call is passed is what that parameter
		// holds and not the storage holding it.
		mark_unwind_step();
		const LowValue object = expression(*node.children[0]->children[1]);
		constructor_call(rvalue(object), node);
		return;
	}

	case FactKind::DestructorAction:
		if (!node.fact.full_expression_end)
		{
			// 12.2p3: this is 3.8p1's end of an object the block declared, and
			// no part of the full-expression before it, so the handler that
			// stood around that full-expression comes off in front of it.
			close_region();
		}
		// 3.8p1: the program wrote this end of a lifetime, so nothing after it
		// owes one - which is asked before the call, because a handler around
		// the call itself would owe the object it is destroying.
		end_object_lifetime(node);
		destructor_call(node);
		return;

	case FactKind::StorageTransfer:
		storage_transfer(node);
		return;

	case FactKind::ArrayTransfer:
		// 12.8p28: the one step under this node is the transfer of one element
		// of an array member, and the member is however many elements its type
		// says - so the step is written for each of them in turn.
		array_transfer(node);
		return;

	case FactKind::TypeAlias:
	case FactKind::FunctionDeclaration:
		return;

	default:
		break;
	}
	throw std::runtime_error("a statement is outside the PA15 lowering subset");
}

void LowirFunctionLowering::compound_statement(const DumpNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		statement(*node.children[index]);
	}
}

void LowirFunctionLowering::declaration_statement(const DumpNode& node)
{
	// 1.9p10: the initializer of a declaration is a full-expression, and the
	// temporaries it created are destroyed once the object it initialized
	// stands.
	open_full_expression();
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		statement(*node.children[index]);
	}
	close_full_expression();
}

void LowirFunctionLowering::arm(const DumpNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		statement(*node.children[index]);
	}
}

void LowirFunctionLowering::expression_statement(const DumpNode& node)
{
	const DumpNode* const written = only_child(node);
	if (written == nullptr)
	{
		return;
	}
	if (written->fact.kind == FactKind::DestructorAction)
	{
		// The statement wrote no expression at all, so what stands under it is
		// nothing this milestone reaches.
		return;
	}
	// 1.9p10: the expression a statement writes is a full-expression.
	open_full_expression();
	if (discarded_class_object(*written))
	{
		leave_blocks(node);
		close_full_expression();
		return;
	}
	if (written->fact.kind == FactKind::Conditional)
	{
		// 6.2p1 and 5.16: both arms still run, and neither has a value the
		// statement keeps.
		discarded_conditional(*written);
		leave_blocks(node);
		close_full_expression();
		return;
	}
	// 6.2p1: the value is computed and discarded.
	expression(*written);
	// 12.2p3: the temporaries the full-expression created are destroyed at its
	// end, in the reverse of the order they were created in.
	leave_blocks(node);
	close_full_expression();
}

// 5p11 and 12.2p1: the value of the expression is thrown away, but an object of
// class type the expression created is an object of the function all the same -
// it stands in storage of its own, which is what a lifetime the full-expression
// ends needs it to have.  A temporary the analysis already named has that
// storage; a call handing one back has none until the statement gives it some.
bool LowirFunctionLowering::discarded_class_object(const DumpNode& node)
{
	if (!stands_in_no_storage(node))
	{
		return false;
	}
	class_object_slot(node, unit_.types().strip_cv(node.fact.type), "discard");
	return true;
}

// 12.2p1: whether the expression is worth an object of class type that stands
// in no storage of the function's yet.  A temporary the analysis named and a
// subobject of one each already stand somewhere; what a call hands back is the
// object itself, which needs storage before it can be read as one - and a call
// returning its object indirectly needs the storage before it runs, because it
// is what creates the object there.  5.16p3's result object is the same: the
// conditional owns no storage, its arms fill whatever place it is given, and
// where nothing gave it one the function does - once, before the arms run,
// rather than once per arm.
bool LowirFunctionLowering::stands_in_no_storage(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	return (node.fact.kind == FactKind::Call ||
	        node.fact.kind == FactKind::Conditional) &&
		node.fact.category == ValueCategory::PRValue &&
		types.is_class(types.strip_cv(node.fact.type));
}

void LowirFunctionLowering::local_variable(const DumpNode& node)
{
	const UnwindMark opened = unwind_mark_;
	declare_local(node);
	if (node.fact.destruction == nullptr)
	{
		return;
	}
	// 3.8p1 and 15.2p2: the declaration began a lifetime, so an exception out
	// of anything after it has to end that lifetime - naming the object the way
	// the declaration named it, because a block a handler reaches names no
	// temporary another block produced.
	std::vector<Instruction> address;
	Operand at;
	const std::unordered_map<std::uint32_t, Operand>::const_iterator standing =
		placed_.find(node.fact.entity->id);
	if (standing != placed_.end())
	{
		at = standing->second;
	}
	else
	{
		const std::unordered_map<std::uint32_t, std::string>::const_iterator
			found = slots_.find(node.fact.entity->id);
		if (found == slots_.end())
		{
			return;
		}
		Instruction named;
		named.kind = Instruction::IK_ADDR;
		named.first = named_operand(Operand::OP_SLOT, found->second);
		named.dest = "declared:" + found->second;
		address.push_back(named);
		at = named_operand(Operand::OP_TEMP, named.dest);
	}
	begin_object_lifetime(node, opened, at, address);
}

void LowirFunctionLowering::declare_local(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	SemaEntity& entity = *node.fact.entity;
	const TypeId type = node.fact.type;
	if (return_slot_local_ != nullptr && return_slot_local_->id == entity.id)
	{
		// 12.8p31: every return of this function copies this object into the
		// destination the caller named, so the declaration is given that
		// destination as its storage and the two are one object.
		place_object(entity.id, result_object_);
		if (!node.children.empty() &&
		    node.children[0]->fact.kind == FactKind::ConstructorAction)
		{
			constructor_call(result_object_, *node.children[0]);
			return;
		}
		LowObject standing;
		standing.storage = result_object_;
		standing.address = result_object_;
		standing.addressed = true;
		if (!node.children.empty())
		{
			initialize_into(standing, type, *node.children[0]);
		}
		return;
	}
	const std::string slot = add_slot(entity, type);
	const Operand storage = named_operand(Operand::OP_SLOT, slot);
	LowObject opened;
	opened.storage = storage;
	if (types.is_class(types.strip_cv(type)))
	{
		// 3.8p1: the lifetime of an object of class type begins where its
		// declaration is reached, and the declaration is what names it, so the
		// address of its storage is what the declaration is worth.
		LowValue object;
		object.type = type;
		object.lvalue = true;
		object.operand = storage;
		const std::size_t block = current_;
		const std::size_t named = out_.blocks[block].instructions.size();
		const unsigned counted = temps_;
		const Operand address = address_of(object);
		if (!node.children.empty() &&
		    node.children[0]->fact.kind == FactKind::ConstructorAction)
		{
			// 12.1p5: the constructor runs on that object, there and then.
			constructor_call(address, *node.children[0]);
			return;
		}
		opened.address = address;
		opened.addressed = true;
		if (!node.children.empty() &&
		    (node.children[0]->fact.kind == FactKind::AggregateInitialization ||
		     node.children[0]->fact.kind == FactKind::BracedInitList))
		{
			// 8.5.1p1: what the clauses write into is that same address, so
			// where the declaration is initialized from a list the address it
			// named is the destination of that initialization.  A list that
			// writes nothing - `{}` for a class each of whose subobjects holds
			// nothing - asked for no destination, and the address is then no
			// part of the program.  What the initialization wrote is what says
			// so, rather than a second walk predicting what it will.
			initialize_into(opened, type, *node.children[0]);
			if (current_ == block &&
			    out_.blocks[block].instructions.size() == named + 1)
			{
				out_.blocks[block].instructions.pop_back();
				temps_ = counted;
			}
			return;
		}
	}
	if (!node.children.empty() &&
	    node.children[0]->fact.kind == FactKind::ConstructorAction &&
	    types.kind(types.strip_cv(type)) == TypeKind::Array)
	{
		// 12.6p1: the declaration created as many objects as the array has
		// elements, and the constructor ran on each of them.
		array_lifecycle(*node.children[0], true);
		return;
	}
	if (node.children.empty())
	{
		// 8.5p6: an object with no initializer holds no value the program may
		// read, and its storage is all the declaration asks for.
		return;
	}
	if (types.is_reference(type))
	{
		// 8.5.3p5: a reference binds the object its initializer names, so what
		// the initializer is read for is that object and not its value.
		const LowValue bound = expression(*node.children[0], true);
		store(address_of(bound), storage, type);
		return;
	}
	initialize_into(opened, type, *node.children[0]);
}

void LowirFunctionLowering::return_statement(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const std::vector<LowUnwind> standing = unwind_live_;
	const std::vector<std::string> handlers = dispatch_cache_;
	// 1.9p10 and 6.6.3p2: the operand of a return is a full-expression, and the
	// temporaries it created end once the returned object stands.
	open_full_expression();
	Instruction instruction;
	instruction.kind = Instruction::IK_RETURN;
	instruction.type = out_.return_type;
	// 3.8p1: the objects of the blocks the return leaves are destroyed after
	// the value has been read, so the actions stand under the return and after
	// the expression it carries.
	const DumpNode* const written =
		node.children.empty() ||
		node.children[0]->fact.kind == FactKind::DestructorAction
			? nullptr
			: node.children[0];
	if (written == nullptr)
	{
		if (!types.is_void(returns_) && !indirect_result_)
		{
			instruction.first = literal_operand(returns_, 0);
		}
	}
	else if (!types.is_reference(returns_) &&
	         types.is_class(types.strip_cv(returns_)))
	{
		// 6.6.3p2 and 12.8p31: the returned object is an object of its own,
		// standing either in the storage the caller named for it or in storage
		// of the function's, and the expression creates it there rather than
		// being read and copied into it.  Either way the place is named before
		// the expression runs.
		const TypeId type = types.strip_cv(returns_);
		if (indirect_result_)
		{
			// 12.8p31: where the object this return names is the object already
			// standing in the destination, the copy between them is not one the
			// program can tell was made, and it is not written.
			if (return_slot_local_ == nullptr ||
			    returned_local(node) != return_slot_local_)
			{
				place_class_object(result_object_, type, *written);
			}
			else if (written->fact.kind == FactKind::TemporaryObject &&
			         !written->children.empty())
			{
				// 12.8p32 and 3.2p2: the transfer this elision leaves out is
				// still one the program wrote, so the constructor 13.3 chose
				// for it is odr-used - and the definition the standard gives
				// that constructor odr-uses the transfer member of every
				// subobject.  Nothing calls the constructor itself, so this
				// unit owes no definition of it; what its definition would have
				// called is owed all the same.
				const DumpNode& action = *written->children[0];
				if (action.fact.kind == FactKind::ConstructorAction &&
				    !action.children.empty() &&
				    !action.children[0]->children.empty() &&
				    action.children[0]->children[0]->fact.entity != nullptr)
				{
					unit_.owe_elided_construction(
						*action.children[0]->children[0]->fact.entity);
				}
			}
		}
		else
		{
			// 6.6.3p2: the object the caller reads is one object of this
			// function, so the storage it stands in is one slot however many
			// returns write it - no two of them are ever standing at once.
			// The slot is a name and the address of it is a value, so the
			// name is opened once and the address taken again on each path.
			if (!returned_object_open_)
			{
				returned_object_storage_ = named_operand(
					Operand::OP_SLOT, add_generated_slot("retobj", type));
				returned_object_open_ = true;
			}
			LowValue held;
			held.type = type;
			held.lvalue = true;
			held.named = true;
			held.operand = returned_object_storage_;
			place_class_object(address_of(held), type, *written);
			instruction.first = returned_object_storage_;
		}
	}
	else
	{
		const LowValue value = expression(*written, types.is_reference(returns_));
		// 6.6.3p3: an expression of type void was evaluated for what it does,
		// and there is no value for the return to carry.
		if (!types.is_void(types.strip_cv(value.type)))
		{
			instruction.first = converted(value, returns_);
		}
	}
	leave_blocks(node);
	close_full_expression();
	// 3.8p1: the return ended those lifetimes on the path it takes; every
	// other path through the function still has the objects standing.
	unwind_live_ = standing;
	dispatch_cache_ = handlers;
	if (!epilogue_.empty())
	{
		// 12.4p8 and 15.2p2: the return leaves the destructor's own body, so
		// the region that body stands in ends here and the subobjects are
		// destroyed before control leaves the function.
		destructor_epilogue();
	}
	terminate(instruction);
}

LowValue LowirFunctionLowering::condition_value(const DumpNode& node)
{
	if (node.fact.kind != FactKind::ConditionDeclaration)
	{
		return expression(node);
	}
	// 6.4p4: the condition declares a name, and its value is the value of the
	// object it declared, which the substatements go on reading through that
	// same name.
	const DumpNode& declared = *node.children[0];
	local_variable(declared);
	if (node.children.size() > 1)
	{
		// 4p3: the object is of class type, and what the statement branches on
		// is what the conversion function of that class handed back - written
		// under the declaration, so it runs once the object stands.
		return expression(*node.children[1]);
	}
	return storage_of(*declared.fact.entity);
}

// 6.4p4 and 12.2p3: the condition of a selection or iteration statement, as the
// branch it is.  Where the condition's own full-expression left temporaries,
// the two edges out of it are where 12.2p3 ends them - so the branch goes to a
// block of its own on each side, which destroys them and then goes where the
// statement does.
void LowirFunctionLowering::branch_on_condition(const DumpNode& node,
                                                const std::string& on_true,
                                                const std::string& on_false)
{
	// 1.9p10 and 6.4p1: the condition is a full-expression of its own.
	open_full_expression();
	if (!ends_temporaries(node))
	{
		branch_on_value(*node.children[0], on_true, on_false);
		close_full_expression();
		return;
	}
	// The value is taken as a value rather than as 5.14's own control flow,
	// because every edge out of it has the same temporaries to end and a
	// short-circuit writes one edge per operand.
	const LowValue value = condition_value(*node.children[0]);
	const Operand tested = truth_for_branch(value);
	// 15.2p2: the value is the last of the full-expression, so the handler that
	// stood around it comes off before the edges out of it are named - the
	// blocks that destroy its temporaries are reached by the branch and are no
	// part of what an exception in the condition owes.
	close_full_expression();
	const std::string on_true_cleanup = reserve_block("cond_true_cleanup");
	const std::string on_false_cleanup = reserve_block("cond_false_cleanup");
	branch(tested, on_true_cleanup, on_false_cleanup);
	open_block(on_true_cleanup);
	leave_blocks(node);
	jump(on_true);
	open_block(on_false_cleanup);
	leave_blocks(node);
	jump(on_false);
}

// 3.8p1: whether any object's lifetime ends where this construct does.
bool LowirFunctionLowering::ends_temporaries(const DumpNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind == FactKind::DestructorAction)
		{
			return true;
		}
	}
	return false;
}

void LowirFunctionLowering::branch_on_value(const DumpNode& node,
                                            const std::string& on_true,
                                            const std::string& on_false)
{
	// 5.14 and 5.15 in the one context where the value of `&&` or `||` is
	// never named: the operator is its own control flow, so the operands
	// branch straight to where the statement goes rather than through a slot
	// that is then tested.
	// 12.2p3 is what keeps one of them out of that form: an operand whose
	// temporaries end where it ends has one place to write those ends, and it
	// is the block the value form gives that operand and not the edges a
	// short-circuit writes one of per operand.
	if (node.fact.kind == FactKind::Binary &&
	    (node.fact.op == OP_LAND || node.fact.op == OP_LOR) &&
	    !ends_temporaries(node))
	{
		const bool conjunction = node.fact.op == OP_LAND;
		const std::string rhs = reserve_block(conjunction ? "land_rhs" : "lor_rhs");
		branch_on_value(*node.children[0], conjunction ? rhs : on_true,
		                conjunction ? on_false : rhs);
		open_block(rhs);
		branch_on_value(*node.children[1], on_true, on_false);
		return;
	}
	const LowValue value = condition_value(node);
	branch(truth_for_branch(value), on_true, on_false);
}

void LowirFunctionLowering::if_statement(const DumpNode& node)
{
	const std::string then_label = reserve_block("if_then");
	const std::string else_label = reserve_block("if_else");
	const std::string end_label = reserve_block("if_end");
	const DumpNode* then_arm = nullptr;
	const DumpNode* else_arm = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		if (child.fact.kind == FactKind::Condition)
		{
			branch_on_condition(child, then_label, else_label);
		}
		else if (child.fact.kind == FactKind::Then)
		{
			then_arm = &child;
		}
		else if (child.fact.kind == FactKind::Else)
		{
			else_arm = &child;
		}
	}
	bool reached = false;
	open_block(then_label);
	if (then_arm != nullptr)
	{
		arm(*then_arm);
	}
	if (!terminated())
	{
		jump(end_label);
		reached = true;
	}
	open_block(else_label);
	if (else_arm != nullptr)
	{
		arm(*else_arm);
	}
	if (!terminated())
	{
		jump(end_label);
		reached = true;
	}
	if (reached)
	{
		open_block(end_label);
		// 6.4p3 and 3.8p1: an object the condition declared belongs to the
		// region the statement opened, so its lifetime ends where the statement
		// does and not where the block around the statement ends.
		leave_blocks(node);
	}
}

void LowirFunctionLowering::while_statement(const DumpNode& node)
{
	const std::string cond_label = reserve_block("while_cond");
	const std::string body_label = reserve_block("while_body");
	const std::string end_label = reserve_block("while_end");
	jump(cond_label);
	open_block(cond_label);
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind == FactKind::Condition)
		{
			branch_on_condition(*node.children[index], body_label, end_label);
		}
	}
	open_block(body_label);
	breaks_.push_back(end_label);
	continues_.push_back(cond_label);
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind != FactKind::Condition)
		{
			statement(*node.children[index]);
		}
	}
	breaks_.pop_back();
	continues_.pop_back();
	if (!terminated())
	{
		jump(cond_label);
	}
	open_block(end_label);
}

void LowirFunctionLowering::do_statement(const DumpNode& node)
{
	const std::string body_label = reserve_block("do_body");
	const std::string cond_label = reserve_block("do_cond");
	const std::string end_label = reserve_block("do_end");
	jump(body_label);
	open_block(body_label);
	breaks_.push_back(end_label);
	continues_.push_back(cond_label);
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind != FactKind::Condition)
		{
			statement(*node.children[index]);
		}
	}
	breaks_.pop_back();
	continues_.pop_back();
	if (!terminated())
	{
		jump(cond_label);
	}
	open_block(cond_label);
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind == FactKind::Condition)
		{
			branch_on_condition(*node.children[index], body_label, end_label);
		}
	}
	open_block(end_label);
}

void LowirFunctionLowering::for_statement(const DumpNode& node)
{
	// 6.5.3p1: the for-init-statement runs once before the loop and is no part
	// of it, so what it writes stands in front of the blocks the loop is - and
	// any block of its own is numbered before them.
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		if (child.fact.kind != FactKind::ForInit)
		{
			continue;
		}
		for (std::size_t at = 0; at < child.children.size(); ++at)
		{
			statement(*child.children[at]);
		}
	}
	const std::string cond_label = reserve_block("for_cond");
	const std::string body_label = reserve_block("for_body");
	const std::string iter_label = reserve_block("for_iter");
	const std::string end_label = reserve_block("for_end");
	const DumpNode* condition = nullptr;
	const DumpNode* iteration = nullptr;
	const DumpNode* body = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		switch (child.fact.kind)
		{
		case FactKind::ForInit:
			break;
		case FactKind::Condition:
			condition = &child;
			break;
		case FactKind::Iteration:
			iteration = &child;
			break;
		case FactKind::DestructorAction:
			// 6.5.3p1: an object the for-init-statement declared is destroyed
			// where the loop ends, which is after the substatement rather than
			// inside it.
			break;
		default:
			body = &child;
			break;
		}
	}
	jump(cond_label);
	open_block(cond_label);
	if (condition != nullptr)
	{
		branch_on_condition(*condition, body_label, end_label);
	}
	else
	{
		jump(body_label);
	}
	open_block(body_label);
	breaks_.push_back(end_label);
	continues_.push_back(iter_label);
	if (body != nullptr)
	{
		statement(*body);
	}
	breaks_.pop_back();
	continues_.pop_back();
	if (!terminated())
	{
		jump(iter_label);
	}
	open_block(iter_label);
	if (iteration != nullptr)
	{
		open_full_expression();
		for (std::size_t at = 0; at < iteration->children.size(); ++at)
		{
			if (iteration->children[at]->fact.kind ==
			    FactKind::DestructorAction)
			{
				continue;
			}
			expression(*iteration->children[at]);
		}
		// 12.2p3: the loop-continuation portion is a full-expression of its
		// own, so its temporaries end with it and not with the loop.
		leave_blocks(*iteration);
		close_full_expression();
	}
	jump(cond_label);
	open_block(end_label);
	leave_blocks(node);
}

void LowirFunctionLowering::switch_statement(const DumpNode& node)
{
	// 6.4.2: the condition chooses among the labels its substatement wrote, and
	// a label can be written anywhere inside that substatement.  The dispatch
	// therefore stands in a block of its own, opened before the substatement is
	// read, and the arms it chooses among are filled in once reading it has
	// found them.
	const std::string dispatch_label = reserve_block("switch_dispatch");
	const std::string end_label = reserve_block("switch_end");
	const DumpNode* condition = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind == FactKind::Condition)
		{
			condition = node.children[index];
		}
	}
	if (condition == nullptr || condition->children.empty())
	{
		throw std::runtime_error("a switch statement has no condition");
	}
	const DumpNode& written = *condition->children[0];
	const LowValue selector = condition_value(written);
	const TypeId chosen_type = written.fact.kind == FactKind::ConditionDeclaration
		? selector.type
		: written.fact.type;
	const Operand chosen = converted(selector, chosen_type);
	jump(dispatch_label);
	open_block(dispatch_label);
	const std::size_t at = current_;
	Instruction placeholder;
	placeholder.kind = Instruction::IK_SWITCH;
	placeholder.first = chosen;
	placeholder.second = named_operand(Operand::OP_LABEL, end_label);
	terminate(placeholder);

	switches_.push_back(SwitchArms());
	switches_.back().selector = written.fact.type;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind != FactKind::Condition)
		{
			reserve_case_labels(*node.children[index], switches_.back());
		}
	}
	breaks_.push_back(end_label);
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->fact.kind != FactKind::Condition)
		{
			statement(*node.children[index]);
		}
	}
	breaks_.pop_back();
	const SwitchArms arms = switches_.back();
	switches_.pop_back();
	if (!terminated())
	{
		jump(end_label);
	}
	Instruction& emitted = out_.blocks[at].instructions[0];
	emitted.switch_cases = arms.cases;
	if (!arms.fallback.empty())
	{
		emitted.second = named_operand(Operand::OP_LABEL, arms.fallback);
	}
	open_block(end_label);
}

void LowirFunctionLowering::reserve_case_labels(const DumpNode& node,
                                                SwitchArms& arms)
{
	if (node.fact.kind == FactKind::Switch)
	{
		// 6.4.2p1: a label written inside another switch belongs to that one.
		return;
	}
	if (node.fact.kind == FactKind::Case || node.fact.kind == FactKind::Default)
	{
		arms.labels[&node] = reserve_block(
			node.fact.kind == FactKind::Default ? "switch_default"
			                                    : "switch_case");
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		reserve_case_labels(*node.children[index], arms);
	}
}

const std::string& LowirFunctionLowering::goto_label(const std::string& name)
{
	std::string& label = labels_[name];
	if (label.empty())
	{
		label = reserve_block("goto");
	}
	return label;
}

void LowirFunctionLowering::case_statement(const DumpNode& node)
{
	if (switches_.empty())
	{
		throw std::runtime_error("a case label is outside every switch");
	}
	const bool fallback = node.fact.kind == FactKind::Default;
	const std::string label = switches_.back().labels[&node];
	if (!terminated())
	{
		// 6.4.2p6: control reaches a label from the statement before it as well
		// as from the switch that chose it.
		jump(label);
	}
	open_block(label);
	std::size_t first = 0;
	if (fallback)
	{
		switches_.back().fallback = label;
	}
	else
	{
		lowir_model::SwitchCase arm;
		arm.value = literal_operand(switches_.back().selector,
		                            node.children[0]->fact.value);
		arm.label = label;
		switches_.back().cases.push_back(arm);
		first = 1;
	}
	for (std::size_t index = first; index < node.children.size(); ++index)
	{
		statement(*node.children[index]);
	}
}
