#include "lowir_lower.h"

#include <sstream>
#include <stdexcept>

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
	const Operand result = temp();
	instruction.dest = result.text;
	out_.blocks[current_].instructions.push_back(instruction);
	return result;
}

void LowirFunctionLowering::emit_void(Instruction& instruction)
{
	out_.blocks[current_].instructions.push_back(instruction);
}

void LowirFunctionLowering::terminate(Instruction& instruction)
{
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
		// A floating zero is spelled at the width it is written for, which is
		// what the suffix of a LowIR floating literal says.
		const std::string low = unit_.low_type(type).text;
		operand.kind = Operand::OP_FLOAT;
		operand.text = low == "f32" ? "0.0f" : low == "f80" ? "0.0L" : "0.0";
		return operand;
	}
	const unsigned long long value = unit_.narrowed(type, bits);
	operand.text = unit_.is_signed(type)
		? signed_decimal(static_cast<long long>(value))
		: decimal(value);
	return operand;
}

// ------------------------------------------------------------------- slots

std::string LowirFunctionLowering::add_slot(const SemaEntity& entity,
                                            TypeId type)
{
	// 3.3.3p4: a name declared in a block hides one of the same name outside
	// it, so two slots of one function can be named after one identifier.  The
	// second and later of them are given a suffix, which keeps every slot name
	// distinct without renaming the one that got there first.
	std::string name = entity.name;
	if (name.empty())
	{
		name = "__param" + decimal(out_.params.size());
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
	slots_[entity.id] = chosen;
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

LowValue LowirFunctionLowering::storage_of(const SemaEntity& entity)
{
	LowValue value;
	value.type = entity.type;
	value.lvalue = true;
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
	if (source.text == target.text)
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

// 8.5p14 and 5.19: the value an initializer gives the object it initializes.
// An initialization writes what the initializer is worth as the type the object
// holds, so where that is a constant the conversion is the value it produces
// and nothing computes it; 4.12's conversion to `bool` is a comparison rather
// than a value read at another width, and is written where it stands.
Operand LowirFunctionLowering::initializer_value(const LowValue& value,
                                                 TypeId target)
{
	TypeTable& types = unit_.types();
	const TypeId wanted = types.strip_cv(target);
	if (value.constant && types.is_integral(wanted) &&
	    types.kind(wanted) == TypeKind::Fundamental &&
	    types.fundamental_type(wanted) != FT_BOOL &&
	    types.is_integral(types.strip_cv(value.type)))
	{
		return literal_operand(wanted, value.value);
	}
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

void LowirFunctionLowering::run(const DumpNode& node, TypeId type)
{
	TypeTable& types = unit_.types();
	returns_ = types.target(type);
	open_block("entry");
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
		parameter.type = unit_.low_type(written);
		if (types.is_reference(written))
		{
			// 8.5.3: a reference parameter is passed as the address of what it
			// was bound to, which the boundary says rather than the type.
			parameter.metadata.passing = lowir_model::PPM_REFERENCE;
		}
		parameter.name = add_slot(*child.fact.entity, written);
		taken_.insert(parameter.name);
		out_.params.push_back(parameter);
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
			copy_class_object(address_of(held),
			                  named_operand(Operand::OP_TEMP, parameter.name),
			                  written);
			continue;
		}
		// 5.1.1p8: a parameter is an object of the function, so its value is
		// written into the storage the body reads it from.
		store(named_operand(Operand::OP_TEMP, parameter.name),
		      named_operand(Operand::OP_SLOT, parameter.name), written);
	}
	for (; index < node.children.size(); ++index)
	{
		statement(*node.children[index]);
	}
	if (!terminated())
	{
		// 6.6.3p2 and 3.6.1p5: falling off the end of `main` returns zero, and
		// of any other function returns nothing the caller reads.
		Instruction instruction;
		instruction.kind = Instruction::IK_RETURN;
		instruction.type = out_.return_type;
		instruction.first = literal_operand(returns_, 0);
		terminate(instruction);
	}
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
		// control reaches the statement it jumps to.
		leave_blocks(node);
		jump(breaks_.back());
		return;

	case FactKind::Continue:
		if (continues_.empty())
		{
			throw std::runtime_error("a continue statement leaves no loop");
		}
		leave_blocks(node);
		jump(continues_.back());
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
		const LowValue object = expression(*node.children[0]->children[1]);
		constructor_call(object.operand, node);
		return;
	}

	case FactKind::DestructorAction:
		destructor_call(node);
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
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		statement(*node.children[index]);
	}
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
	if (written->fact.kind == FactKind::Conditional)
	{
		// 6.2p1 and 5.16: both arms still run, and neither has a value the
		// statement keeps.
		discarded_conditional(*written);
		return;
	}
	// 6.2p1: the value is computed and discarded.
	expression(*written);
}

void LowirFunctionLowering::local_variable(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	SemaEntity& entity = *node.fact.entity;
	const TypeId type = node.fact.type;
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
		if (!types.is_void(returns_))
		{
			instruction.first = literal_operand(returns_, 0);
		}
	}
	else
	{
		const LowValue value = expression(*written, types.is_reference(returns_));
		// 6.6.3p3: an expression of type void was evaluated for what it does,
		// and there is no value for the return to carry.
		if (!types.is_void(types.strip_cv(value.type)))
		{
			// 6.6.3p2 and 12.8p15: a returned object of class type is a copy
			// the function makes in storage of its own, which 12.8p31 lets a
			// prvalue be created in rather than copied into.
			instruction.first =
				!types.is_reference(returns_) &&
					types.is_class(types.strip_cv(returns_))
					? class_value_slot(*written, value,
					                   types.strip_cv(returns_), "retobj")
					: converted(value, returns_);
		}
	}
	leave_blocks(node);
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
	return storage_of(*declared.fact.entity);
}

void LowirFunctionLowering::branch_on_condition(const DumpNode& node,
                                                const std::string& on_true,
                                                const std::string& on_false)
{
	// 5.14 and 5.15 in the one context where the value of `&&` or `||` is
	// never named: the operator is its own control flow, so the operands
	// branch straight to where the statement goes rather than through a slot
	// that is then tested.
	if (node.fact.kind == FactKind::Binary &&
	    (node.fact.op == OP_LAND || node.fact.op == OP_LOR))
	{
		const bool conjunction = node.fact.op == OP_LAND;
		const std::string rhs = reserve_block(conjunction ? "land_rhs" : "lor_rhs");
		branch_on_condition(*node.children[0], conjunction ? rhs : on_true,
		                    conjunction ? on_false : rhs);
		open_block(rhs);
		branch_on_condition(*node.children[1], on_true, on_false);
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
			branch_on_condition(*child.children[0], then_label, else_label);
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
			branch_on_condition(*node.children[index]->children[0], body_label,
			                    end_label);
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
			branch_on_condition(*node.children[index]->children[0], body_label,
			                    end_label);
		}
	}
	open_block(end_label);
}

void LowirFunctionLowering::for_statement(const DumpNode& node)
{
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
			for (std::size_t at = 0; at < child.children.size(); ++at)
			{
				statement(*child.children[at]);
			}
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
		branch_on_condition(*condition->children[0], body_label, end_label);
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
		for (std::size_t at = 0; at < iteration->children.size(); ++at)
		{
			expression(*iteration->children[at]);
		}
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

// ------------------------------------------------------------- expressions

LowValue LowirFunctionLowering::expression(const DumpNode& node,
                                           bool as_object)
{
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
		value.operand = literal_operand(value.type, 0);
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
	if (types.strip_cv(value.type) == types.fundamental(FT_NULLPTR_T))
	{
		// 2.14.7 and 4.10p1: `nullptr` is a value of its own type, which every
		// pointer type converts from and which LowIR spells as it is written.
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
	value.operand = literal_operand(value.type, node.fact.value);
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
	const LowValue held = expression(object, true);
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
	step.second = named_operand(Operand::OP_INTEGER, decimal(member.offset));
	return emit(step);
}

// 4.10p3 and 10p1: the base class subobject of the object the operand denotes,
// which is that object's storage at the place its class gave the base.  The
// operand is a pointer where a pointer converted and the object itself where an
// lvalue did, and the node's own type says which of the two the result is.
LowValue LowirFunctionLowering::base_conversion(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const LowValue held = expression(*node.children[0], true);
	const Operand from =
		types.kind(types.strip_cv(held.type)) == TypeKind::Pointer
			? rvalue(held)
			: address_of(held);
	Instruction step;
	step.kind = Instruction::IK_INDEX;
	step.type.text = "i8";
	step.index_projection = lowir_model::IPK_BASE_SUBOBJECT;
	step.first = from;
	step.second = named_operand(Operand::OP_INTEGER, decimal(node.fact.value));
	LowValue value;
	value.type = node.fact.type;
	value.operand = emit(step);
	value.named = true;
	value.lvalue =
		types.kind(types.strip_cv(value.type)) != TypeKind::Pointer;
	return value;
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
	const bool object_result = as_object || to_void ||
		node.fact.category != ValueCategory::PRValue;
	const LowValue source = expression(*node.children[0], object_result);
	if (node.fact.category != ValueCategory::PRValue)
	{
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
	    types.is_integral(types.strip_cv(source.type)))
	{
		// 5.2.9 over a constant: the value the cast produces is a value the
		// translation knows, so it is written as the immediate it is - and it
		// is the immediate an object of the cast's own type holds, which is
		// what a conversion above it then widens.
		value.constant = true;
		value.value = unit_.narrowed(value.type, source.value);
		value.operand = literal_operand(value.type, value.value);
		return value;
	}
	if (types.is_class(types.strip_cv(value.type)))
	{
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

LowValue LowirFunctionLowering::call_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	Instruction call;
	call.kind = Instruction::IK_CALL;
	const DumpNode& callee = *node.children[0];
	const bool direct = callee.fact.kind == FactKind::Callee;
	// What the call boundary is, is known from the callee's resolved type
	// before anything is emitted, which is what lets the arguments be lowered
	// where the source wrote them: before the callee they are passed to.
	const TypeId written = types.strip_cv(
		direct ? callee.fact.entity->type : callee.fact.type);
	const TypeId function = types.kind(written) == TypeKind::Function
		? written
		: types.target(written);
	const std::vector<TypeId>& parameters = types.parameters(function);
	for (std::size_t index = 1; index < node.children.size(); ++index)
	{
		const std::size_t at = index - 1;
		const bool bound =
			at < parameters.size() && types.is_reference(parameters[at]);
		const LowValue argument = expression(*node.children[index], bound);
		call.args.push_back(
			passed_operand(*node.children[index], argument, parameters, at));
	}
	if (direct)
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
		for (std::size_t index = 0; index < parameters.size(); ++index)
		{
			lowir_model::Parameter parameter;
			parameter.name = "arg" + decimal(index);
			parameter.type = unit_.low_type(parameters[index]);
			if (types.is_reference(parameters[index]))
			{
				parameter.metadata.passing = lowir_model::PPM_REFERENCE;
			}
			call.call_params.push_back(parameter);
		}
		call.call_return_type = unit_.low_type(types.target(function));
		if (types.variadic(function))
		{
			call.call_boundary.arity = lowir_model::CAM_VARIADIC;
		}
	}
	const TypeId result = types.target(function);
	call.type = unit_.low_type(result);
	LowValue value;
	value.type = types.is_reference(result) ? types.target(result) : result;
	if (types.is_void(types.strip_cv(result)))
	{
		emit_void(call);
		return value;
	}
	const Operand produced = emit(call);
	if (types.is_reference(result))
	{
		// 5.2.2p10: a call of a function returning a reference is an lvalue,
		// which the address it returned names.
		value.lvalue = true;
	}
	value.operand = produced;
	return value;
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
		const LowValue right = expression(*node.children[1]);
		value.operand = pointer_operation(op, left, right, common, value.type);
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
	if (types.is_object_pointer(types.strip_cv(right.type)) ||
	    types.kind(types.strip_cv(right.type)) == TypeKind::Array)
	{
		// 5.7p6: the distance between two pointers, in elements.
		Instruction difference;
		difference.kind = Instruction::IK_BINARY;
		difference.op = "sub";
		difference.type.text = "ptr";
		difference.first = converted(left, common);
		difference.second = converted(right, common);
		Instruction scale;
		scale.kind = Instruction::IK_BINARY;
		scale.op = "div";
		scale.type = unit_.low_type(result);
		scale.first = emit(difference);
		scale.second = named_operand(Operand::OP_INTEGER,
		                             decimal(types.object_size(element)));
		return emit(scale);
	}
	const bool pointer_left =
		types.is_object_pointer(types.strip_cv(left.type)) ||
		types.kind(types.strip_cv(left.type)) == TypeKind::Array;
	const LowValue& address = pointer_left ? left : right;
	const LowValue& count = pointer_left ? right : left;
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
	const LowValue right = expression(*node.children[1]);
	LowValue held;
	held.type = bare;
	held.operand = rvalue(left);
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
LowValue LowirFunctionLowering::conditional_object(const DumpNode& node)
{
	const TypeId type = unit_.types().strip_cv(node.fact.type);
	const std::string slot = add_generated_slot("condobj", type);
	const Operand storage = named_operand(Operand::OP_SLOT, slot);
	const std::string then_label = reserve_block("condobj_then");
	const std::string else_label = reserve_block("condobj_else");
	const std::string end_label = reserve_block("condobj_end");
	LowValue held;
	held.type = type;
	held.lvalue = true;
	held.operand = storage;
	const Operand into = address_of(held);
	const LowValue condition = expression(*node.children[0]);
	branch(truth_for_branch(condition), then_label, else_label);
	for (unsigned arm = 0; arm < 2; ++arm)
	{
		open_block(arm == 0 ? then_label : else_label);
		const LowValue written = expression(*node.children[arm + 1]);
		copy_class_object(into, address_of(written), type);
		jump(end_label);
	}
	open_block(end_label);
	return held;
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
		Instruction scale;
		scale.kind = Instruction::IK_BINARY;
		scale.op = "mul";
		scale.type.text = "i64";
		scale.first = instruction.second;
		scale.second = named_operand(
			Operand::OP_INTEGER,
			decimal(types.object_size(types.strip_cv(node.fact.type))));
		instruction.type.text = "i8";
		instruction.second = emit(scale);
	}
	LowValue value;
	value.type = node.fact.type;
	value.lvalue = true;
	value.named = true;
	value.operand = emit(instruction);
	return value;
}

