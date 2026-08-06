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

// How many bytes of value-initialization are still written as the elements
// they are.  Beyond this the elements stop being what the reader wants to see
// and the storage starts being it, and a span written by hand would also grow
// with a bound the source only wrote a number for.
const unsigned long long kZeroSpanLimit = 64;

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
	std::string chosen = name;
	unsigned shadow = 1;
	while (!slot_names_.insert(chosen).second)
	{
		++shadow;
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
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(value.type);
	if (types.kind(bare) == TypeKind::Array ||
	    types.kind(bare) == TypeKind::Function)
	{
		return decay(value);
	}
	return load(value.operand, value.type);
}

LowValue LowirFunctionLowering::as_value(const LowValue& value)
{
	TypeTable& types = unit_.types();
	const TypeId bare = types.strip_cv(value.type);
	LowValue out = value;
	out.lvalue = false;
	out.has_held = false;
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
	if (value.operand.kind == Operand::OP_TEMP)
	{
		return value.operand;
	}
	Instruction instruction;
	instruction.kind = Instruction::IK_ADDR;
	instruction.first = value.operand;
	return emit(instruction);
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
	if (value.operand.kind == Operand::OP_TEMP &&
	    types.kind(types.strip_cv(value.type)) != TypeKind::Function)
	{
		// The address is already in hand, and the point where the array became
		// a pointer view of itself is where that address was made.  4.3 is not
		// the same: a function is not an object, so the pointer view of one is
		// marked wherever the function is named.
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
	    (unit_.is_signed(wanted) ||
	     unit_.width(wanted) == unit_.width(value.type)))
	{
		// A widening of an immediate is the immediate it widens to, so the
		// conversion is spelled as the value it produces rather than as the
		// operation that would produce it.
		return literal_operand(wanted, value.value);
	}
	if (types.kind(wanted) == TypeKind::Fundamental &&
	    types.fundamental_type(wanted) == FT_BOOL)
	{
		// 4.12: a value converts to `bool` by comparing it with zero.
		LowValue held = value;
		held.has_held = true;
		held.held = operand;
		return truth_value(held);
	}
	return convert_scalar(operand, value.type, wanted);
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
	if (low.text[0] != 'f' && low.text != "ptr")
	{
		// A branch tests an integer for being other than zero, which is what
		// the value already is.
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

void LowirFunctionLowering::add_initialization(const Operand& storage,
                                               TypeId type,
                                               const DumpNode& node)
{
	TypeTable& types = unit_.types();
	if (node.fact.kind == FactKind::ConstructorAction)
	{
		// 3.6.2p2 and 12.1p5: the object is initialized by calling its
		// constructor on it, which is the one action its initialization is.
		LowValue object;
		object.type = type;
		object.lvalue = true;
		object.operand = storage;
		constructor_action(object, node, false);
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

// 12.1p5 and 8.5p6: constructing `object`, which is one call of its
// constructor on the address of it.  A constructor that does nothing is no
// call at all; the address of the object is still materialized where the
// declaration that names the object is what gave it its storage, because that
// is where 3.8p1 begins its lifetime.
void LowirFunctionLowering::constructor_action(const LowValue& object,
                                               const DumpNode& node,
                                               bool declared_here)
{
	TypeTable& types = unit_.types();
	const DumpNode& call = *node.children[0];
	const SemaEntity& constructor = *call.children[0]->fact.entity;
	if (constructor.trivial && !declared_here)
	{
		return;
	}
	const Operand address = address_of(object);
	if (constructor.trivial)
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
		out.args.push_back(converted(argument, parameters[at]));
	}
	emit_void(out);
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
		jump(breaks_.back());
		return;

	case FactKind::Continue:
		if (continues_.empty())
		{
			throw std::runtime_error("a continue statement leaves no loop");
		}
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
	if (node.children.empty())
	{
		// 8.5p6: an object with no initializer holds no value the program may
		// read, and its storage is all the declaration asks for.
		return;
	}
	if (node.children[0]->fact.kind == FactKind::ConstructorAction)
	{
		// 12.1p5: the object's lifetime begins where its declaration is
		// reached, which is where its constructor runs on it.
		LowValue object;
		object.type = type;
		object.lvalue = true;
		object.operand = storage;
		constructor_action(object, *node.children[0], true);
		return;
	}
	if (types.is_reference(type))
	{
		// 8.5.3p5: a reference binds the object its initializer names.
		const LowValue bound = expression(*node.children[0]);
		store(address_of(bound), storage, type);
		return;
	}
	initialize(storage, type, *node.children[0]);
}

void LowirFunctionLowering::return_statement(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	Instruction instruction;
	instruction.kind = Instruction::IK_RETURN;
	instruction.type = out_.return_type;
	if (node.children.empty())
	{
		if (!types.is_void(returns_))
		{
			instruction.first = literal_operand(returns_, 0);
		}
		terminate(instruction);
		return;
	}
	const LowValue value =
		expression(*node.children[0], types.is_reference(returns_));
	if (types.is_void(types.strip_cv(value.type)))
	{
		// 6.6.3p3: the expression was evaluated for what it does, and there is
		// no value for the return to carry.
		terminate(instruction);
		return;
	}
	instruction.first = converted(value, returns_);
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
LowValue LowirFunctionLowering::member_expression(const DumpNode& node)
{
	TypeTable& types = unit_.types();
	const DumpNode& written = *node.children[0];
	const LowValue object = expression(written, true);
	const Operand base =
		types.kind(types.strip_cv(object.type)) == TypeKind::Pointer
			? rvalue(object)
			: address_of(object);
	const SemaEntity& member = *node.fact.entity;
	LowValue value;
	value.type = node.fact.type;
	value.lvalue = true;
	Instruction step;
	step.kind = Instruction::IK_INDEX;
	step.type.text = "i8";
	step.index_projection = types.is_reference(member.type)
		? lowir_model::IPK_REFERENCE_FIELD
		: lowir_model::IPK_FIELD;
	step.first = base;
	step.second = named_operand(Operand::OP_INTEGER, decimal(member.offset));
	value.operand = emit(step);
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
	const bool object_result = as_object ||
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
	if (types.is_void(types.strip_cv(value.type)))
	{
		rvalue(source);
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
		if (at < parameters.size())
		{
			call.args.push_back(converted(argument, parameters[at]));
			continue;
		}
		// 5.2.2p7: an argument matched by the ellipsis goes through the default
		// argument promotions.
		const TypeId bare = types.strip_cv(argument.type);
		TypeId promoted = bare;
		if (types.is_floating(bare) && types.fundamental_type(bare) == FT_FLOAT)
		{
			promoted = types.fundamental(FT_DOUBLE);
		}
		else if (types.is_integral(bare) && unit_.width(bare) < 4)
		{
			promoted = types.fundamental(unit_.is_signed(bare) ? FT_INT
			                                                   : FT_UNSIGNED_INT);
		}
		call.args.push_back(converted(argument, promoted));
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
	const TypeId bare = types.strip_cv(operand.type);
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
		const LowValue target = expression(*node.children[0], true);
		const TypeId written_type = types.strip_cv(target.type);
		const Operand written = converted(right, written_type);
		store(written, target.operand, written_type);
		LowValue assigned = target;
		assigned.has_held = true;
		assigned.held = written;
		return assigned;
	}
	const LowValue left = expression(*node.children[0], true);
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
	const bool addressed =
		node.fact.category != ValueCategory::PRValue &&
		(as_object || types.kind(types.strip_cv(node.fact.type)) == TypeKind::Array);
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
	instruction.second = rvalue(offset);
	LowValue value;
	value.type = node.fact.type;
	value.lvalue = true;
	value.operand = emit(instruction);
	return value;
}

void LowirFunctionLowering::initialize(const Operand& storage, TypeId type,
                                       const DumpNode& node)
{
	TypeTable& types = unit_.types();
	if (types.kind(types.strip_cv(type)) == TypeKind::Array)
	{
		initialize_array(storage, type, node);
		return;
	}
	if (node.fact.kind == FactKind::BracedInitList)
	{
		// 8.5.1p2 and 8.5p7: a scalar takes the value of its one clause, and
		// an empty list value-initializes it.
		if (node.children.empty())
		{
			store(literal_operand(type, 0), storage, type);
			return;
		}
		initialize(storage, type, *node.children[0]);
		return;
	}
	const LowValue value = expression(node);
	store(converted(value, type), storage, type);
}

void LowirFunctionLowering::initialize_array(const Operand& storage,
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
	LowValue base;
	base.type = type;
	base.lvalue = true;
	base.operand = storage;
	const Operand address = address_of(base);
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
