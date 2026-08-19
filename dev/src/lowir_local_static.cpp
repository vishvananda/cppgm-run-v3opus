#include "lowir_lower.h"

#include <sstream>

#include "ast_tokens.h"
#include "lowir_abi.h"
#include "sema_scope.h"

// 3.7.1p3 and 6.7p4: the object a block declares `static`.
//
// Its storage duration is the program's and its name is one block's, so it is
// neither of the two things the rest of the lowering already writes.  It is not
// an object of its block: no slot holds it, and the end of the block does not
// end it.  It is not a namespace-scope object either: nothing outside the block
// can name it, 6.7p4 initializes it the first time control reaches the
// declaration rather than before `main`, and 3.6.3p3 hands its destruction to
// the runtime at that point rather than writing it into the program's shutdown.
//
// So this file owns the three things that follow from that: the name the image
// gives the storage, what the image holds for it before the program starts, and
// the guard 6.7p4 asks for around the initialization that is left.

namespace {

using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;

LowType low(const std::string& text)
{
	LowType type;
	type.text = text;
	return type;
}

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

// A string an identifier cannot hold, written as one an identifier can: the
// bytes, in order, as hexadecimal.  What it names is exactly what it was made
// from, which is what a symbol two translation units have to agree on needs.
std::string hex_of(const std::string& text)
{
	static const char kDigits[] = "0123456789abcdef";
	std::string out;
	out.reserve(2 * text.size());
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		const unsigned char byte = static_cast<unsigned char>(text[index]);
		out += kDigits[byte >> 4];
		out += kDigits[byte & 0xf];
	}
	return out;
}

}  // namespace

// 3.7.1p3: the name the image gives the storage of one such object.
//
// The program spells no name that reaches it, so the symbol is made of the two
// facts that do: the function whose body declared it, and where in that body
// the declaration stands.  Both are asked once per declaration - `global_symbol`
// holds the answer against the entity - so a name used n times costs one of
// these and n lookups.
std::string LowirUnitLowering::local_static_symbol(const SemaEntity& entity)
{
	const SemaEntity* const owner = entity.local_function;
	std::string symbol = "__local_static__";
	if (owner != nullptr)
	{
		symbol += local_static_owner(*owner);
	}
	symbol += "__" + entity.name + "__";
	symbol += local_static_place(entity, owner);
	return symbol;
}

// The function part of that name, whose one job is to say which function of
// the program the object belongs to.
//
// The qualified name written with `__` for `::` says that for every function a
// program *declares*, because the declaration part below tells two of one name
// apart: 13.1's `f(int)` and `f(double)` are two declarations at two places,
// and so are 13.5's `operator+` and `operator-`, whose punctuation flattens to
// one spelling.
//
// 14.7's specializations are the one shape that is not: `value<1>` and
// `value<2>` are two functions made from *one* declaration, so they stand at
// one place and the part below cannot tell them apart.  What does is what
// already tells their object-file names apart, which is carried whole instead.
std::string LowirUnitLowering::local_static_owner(const SemaEntity& owner)
{
	if (abi_instantiated(owner, types_))
	{
		return "function_symbol_" + hex_of(abi_symbol_of(owner, types_));
	}
	const std::string written =
		(owner.region == nullptr ? std::string() : owner.region->prefix) +
		abi_qualified_name(owner);
	return flatten_symbol_name(written);
}

// 3.5p3 and 7.1.2p4: whether the definition that declared the object is one
// every unit may hold, which is what makes the object one the whole program
// shares.  A definition with internal linkage is this unit's alone whatever
// else was written on it - `static inline`, or an `inline` of 7.3.1.1p1's
// unnamed namespace - so the object it declares is this unit's too, which is
// the same reading `describe_symbol` and `writes_base_entry` already make of
// one.
bool LowirUnitLowering::local_static_shared(const SemaEntity* owner)
{
	return owner != nullptr && !owner->internal_linkage &&
		shared_definition(*owner);
}

// The declaration part.  Within one translation unit the terminals the
// init-declarator was written from name it and nothing else, which is the
// cheapest thing that does.
//
// 7.1.2p4 and 14.7.1p1 leave a definition every unit may hold, though, and two
// units reading one such body reach it after different amounts of text - so a
// span of *this* unit's stream would name two symbols for one object.  What
// those units do agree on is the source the definition was written in, so such
// a declaration is named by the place its declarator-id stands at: the file the
// reading was in and the line and column within it, which is one fact of the
// program rather than of a reading of it.
//
// The counter below it is the last resort, for a stream that kept no such
// record: it names the declaration by its place among the ones the body
// declares, which two units reading the same body in the same order agree on
// and nothing else does.
std::string LowirUnitLowering::local_static_place(const SemaEntity& entity,
                                                  const SemaEntity* owner)
{
	const bool shared = local_static_shared(owner);
	if (!shared && entity.declared_end != 0)
	{
		return "tokens" + decimal(entity.declared_begin) + "_" +
			decimal(entity.declared_end);
	}
	if (positions_ != nullptr)
	{
		const std::string written = positions_->text_at(entity.declared_begin);
		if (!written.empty())
		{
			return "source" + hex_of(" at " + written);
		}
	}
	const std::string key =
		owner == nullptr ? std::string() : function_symbol(*owner);
	return "local" + decimal(local_static_places_[key]++);
}

// 3.6.2 and 6.7p4: what the image holds for the object before the program
// starts, and what is left for the program to run at the declaration.
//
// The image half is the same one a namespace-scope definition gets, with two
// differences that follow from where the object was written:
//
// - 3.6.2p1's constant initialization of an object of class type is not written
//   into the image.  12.1p5 makes the initialization of such an object a call,
//   12.4p3 makes the end of its lifetime one, and 6.7p4 puts both behind the
//   same guard - so writing the value into the image would leave the guard
//   describing a lifetime that had already begun.
// - a reference the program bound to an object of static storage duration is
//   3.6.2p1's constant initialization: it holds an address the image lays out,
//   ends no lifetime, and needs no guard, so it joins the initializations that
//   run before `main`.
//
// Null where the image is the whole of it, and the initializer clause the
// guarded body has to run otherwise.
const DumpNode* LowirUnitLowering::local_static_definition(const DumpNode& node)
{
	SemaEntity& entity = *node.fact.entity;
	const std::string symbol = global_symbol(entity);
	const TypeId type = node.fact.type;
	const DumpNode* const written =
		node.children.empty() ? nullptr : node.children[0];
	const bool object = types_.is_class(types_.element_of(types_.strip_cv(type)));
	defined_.insert(symbol);
	const bool first = builder_.emitted_globals_.insert(symbol).second;
	lowir_model::GlobalDefinition global;
	global.name = symbol;
	global.type = low_type(type);
	global.metadata.binding = local_static_binding(entity);
	const DumpNode* left = written;
	if (object)
	{
		global.structured = true;
		add_zero_item(global, types_.object_size(types_.strip_cv(type)));
	}
	else
	{
		// The clauses are read into the definition once, whether or not this
		// unit is the one that writes it: what is read out of them is both what
		// the image holds and whether 6.7p4 has anything left to guard, and a
		// second unit reaching a shared object still has to know the second.
		left = global_image(global, node, type, false);
		if (left != nullptr && !global.structured)
		{
			// 3.6.2p2: the image holds the zero the storage starts as, and the
			// value is written where the program reaches the declaration.
			global.init_kind = lowir_model::GlobalDefinition::INIT_ZERO;
		}
	}
	if (first)
	{
		program_.globals.push_back(global);
	}
	if (left == nullptr)
	{
		return nullptr;
	}
	if (!object && types_.is_reference(type) && static_bound(*written))
	{
		// 3.6.2p1: the reference is bound to an object the image lays out, so
		// the binding is settled before the program runs and no first use of it
		// is where it happens.
		if (first)
		{
			dynamic_initializer(entity, *written, type);
		}
		return nullptr;
	}
	return left;
}

// 3.6.2p1: whether the initializer denotes an object the image lays out, which
// is what makes the binding of a reference a constant initialization.  A member
// of such an object, an element of one and a conversion to a base of one are
// all the same object reached further in; a call is not one of them, whatever
// it returns.
bool LowirUnitLowering::static_bound(const DumpNode& node)
{
	switch (node.fact.kind)
	{
	case FactKind::Id:
	{
		const SemaEntity* const entity = node.fact.entity;
		if (entity == nullptr || entity->kind != SemaKind::Variable)
		{
			return entity != nullptr && entity->kind == SemaKind::Function;
		}
		return entity->local_static ||
			(entity->region != nullptr &&
			 (entity->region->kind == ScopeKind::Namespace ||
			  entity->region->kind == ScopeKind::Class));
	}

	case FactKind::Literal:
		return !node.fact.spelling.empty() &&
			types_.kind(types_.strip_cv(node.fact.type)) == TypeKind::Array;

	case FactKind::Member:
	case FactKind::Subscript:
	case FactKind::BaseConversion:
	case FactKind::Cast:
		return !node.children.empty() && static_bound(*node.children[0]);

	default:
		return false;
	}
}

// 6.7p4: the flag beside such an object that says whether this run of the
// program has been through its declaration.  It belongs to the object rather
// than to the body that reaches it - a shared definition is one object of the
// program and so is one flag - so it is laid out where the object is named and
// once however many units name it.
const std::string& LowirUnitLowering::local_static_guard(
	const SemaEntity& entity)
{
	std::string& held = local_static_guards_[entity.id];
	if (!held.empty())
	{
		return held;
	}
	held = global_symbol(entity) + "__guard";
	if (builder_.emitted_globals_.insert(held).second)
	{
		lowir_model::GlobalDefinition guard;
		guard.name = held;
		guard.type = low_type(types_.fundamental(FT_LONG_INT));
		guard.init_kind = lowir_model::GlobalDefinition::INIT_ZERO;
		guard.metadata.binding = local_static_binding(entity);
		program_.globals.push_back(guard);
		defined_.insert(held);
	}
	return held;
}

// 3.5p2 and 7.1.2p4: how the object file binds the storage of such an object.
// The object belongs to the one definition of the function that declared it, so
// a definition every unit may hold declares one object the whole program shares
// and a definition this unit alone holds declares one of this unit's.
lowir_model::SymbolBindingMode LowirUnitLowering::local_static_binding(
	const SemaEntity& entity)
{
	return local_static_shared(entity.local_function)
		? lowir_model::SBM_WEAK
		: lowir_model::SBM_INTERNAL;
}

// 3.6.3p3 and the ABI: the runtime function a destruction that runs when the
// program ends is handed to, together with the object it runs on.  It is the
// implementation's own, not this program's, so it is named as the
// implementation names it and declared once per program.
const std::string& LowirUnitLowering::atexit_symbol()
{
	static const std::string kSymbol = "__external_runtime____cxa_atexit";
	if (declared_.insert(kSymbol).second)
	{
		lowir_model::FunctionDeclaration declaration;
		declaration.name = kSymbol;
		for (unsigned index = 0; index < 3; ++index)
		{
			lowir_model::Parameter parameter;
			parameter.name = "arg" + decimal(index);
			parameter.type = low("ptr");
			declaration.params.push_back(parameter);
		}
		declaration.return_type = low("i32");
		declaration.metadata.linkage = lowir_model::LLM_C;
		declaration.metadata.binding = lowir_model::SBM_STRONG;
		declaration.metadata.object_symbol = "__cxa_atexit";
		program_.function_declarations.push_back(declaration);
	}
	return kSymbol;
}

// 6.7p4: the declaration, as the body that reaches it runs it.
//
// The storage is already laid out, so what stands here is the flag that says
// whether this run of the program has been through the declaration yet, and the
// initialization and hand-off of the destruction that the first run through it
// does.  A declaration whose whole initialization the image holds writes
// nothing at all: 3.6.2p1's constant initialization is done before the program
// starts, so there is nothing for a first use to be first for.
void LowirFunctionLowering::local_static_variable(const DumpNode& node)
{
	const DumpNode* const initialization = unit_.local_static_definition(node);
	const DumpNode* const destruction = unit_.declared_destruction(node);
	if (initialization == nullptr && destruction == nullptr)
	{
		return;
	}
	const SemaEntity& entity = *node.fact.entity;
	const std::string& symbol = unit_.global_symbol(entity);
	const TypeId counter = unit_.types().fundamental(FT_LONG_INT);
	const Operand storage = named_operand(Operand::OP_GLOBAL, symbol);
	const Operand flag =
		named_operand(Operand::OP_GLOBAL, unit_.local_static_guard(entity));
	const std::string ready = reserve_block("local_static_ready");
	const std::string start = reserve_block("local_static_init");
	Instruction test;
	test.kind = Instruction::IK_CMP;
	test.op = "ne";
	test.type = unit_.low_type(counter);
	test.first = load(flag, counter);
	test.second = named_operand(Operand::OP_INTEGER, "0");
	branch(emit(test), ready, start);
	open_block(start);
	// 3.6.3p3 and 6.7p4: the address of the storage, which is what the
	// initialization writes through and what the runtime is handed.  An object
	// of class type is initialized through the address its declaration names,
	// which is this one, so the initialization is given it rather than naming a
	// second address for the same storage; everything else is written into that
	// address directly.
	//
	// 12.6p1's array of class type is the one shape that names neither: the
	// declaration creates one object per element and each element's own place
	// is what its construction writes through, so the address of the array as a
	// whole is asked for by the hand-off alone.
	TypeTable& types = unit_.types();
	const TypeId stripped = types.strip_cv(node.fact.type);
	const bool built = types.is_class(stripped);
	const bool per_element =
		initialization != nullptr &&
		initialization->fact.kind == FactKind::ConstructorAction &&
		types.kind(stripped) == TypeKind::Array;
	LowValue object;
	object.type = node.fact.type;
	object.lvalue = true;
	object.operand = storage;
	const Operand address = per_element && destruction == nullptr
		? storage
		: address_of(object);
	if (initialization != nullptr)
	{
		initialize_declared(node, built ? storage : address,
		                    built ? &address : nullptr);
	}
	if (destruction != nullptr)
	{
		hand_to_runtime(unit_.atexit_symbol(), symbol, address, *destruction);
	}
	store(named_operand(Operand::OP_INTEGER, "1"), flag, counter);
	jump(ready);
	open_block(ready);
}

// 3.6.3p3 and 3.7.2p2: the end of an object's lifetime, handed to a runtime
// where the object was begun - 3.6.3p1's runtime for an object of the program
// and its thread's for an object of a thread, which is the only difference
// between the two.  Either runs what it was given in the reverse of the order
// it was given them, which is the reverse of the order the objects were begun
// in, so nothing here has to know what else has already been initialized.
//
// The runtime is handed one function and one object.  `object` is the symbol
// the storage was laid out under, which is what says whose lifetime this ends
// where the function 12.4p8 asks for is not one call.
void LowirFunctionLowering::hand_to_runtime(const std::string& runtime,
                                            const std::string& object,
                                            const Operand& address,
                                            const DumpNode& node)
{
	Instruction ends;
	ends.kind = Instruction::IK_ADDR;
	ends.type.text = "ptr";
	ends.first = named_operand(Operand::OP_GLOBAL,
	                           unit_.destruction_entry(node, object));
	Instruction image;
	image.kind = Instruction::IK_ADDR;
	image.type.text = "ptr";
	image.first =
		named_operand(Operand::OP_GLOBAL, unit_.image_handle_symbol());
	Instruction hand;
	hand.kind = Instruction::IK_CALL;
	hand.type.text = "i32";
	hand.first = named_operand(Operand::OP_GLOBAL, runtime);
	hand.args.push_back(emit(ends));
	hand.args.push_back(address);
	hand.args.push_back(emit(image));
	emit(hand);
}

// 12.4p8 and 3.6.3p3: the one function a runtime that takes one is handed.
//
// For every object but one it is the destructor itself, run on the address the
// runtime was handed beside it.  12.4p8 ends the lifetime of *each element* of
// an array, though, and a runtime handed one function and one object would end
// only the first of them - so an array asks for a body of the program's own
// that ends them all, in the reverse of the order 12.6p1 began them, which is
// the walk `add_destruction` already writes wherever the program itself is what
// reaches the end of a lifetime.
const std::string& LowirUnitLowering::destruction_entry(const DumpNode& node,
                                                        const std::string& object)
{
	if (types_.kind(types_.strip_cv(node.fact.type)) != TypeKind::Array)
	{
		declare_entity(*node.fact.entity);
		return function_symbol(*node.fact.entity, node.fact.base_subobject);
	}
	std::string& held = destruction_entries_[object];
	if (!held.empty())
	{
		return held;
	}
	held = object + "__destroy";
	if (!defined_.insert(held).second)
	{
		// Another unit reading the same definition already wrote it, and the
		// object it ends is the one object both of them laid out.
		return held;
	}
	lowir_model::Function body;
	body.name = held;
	body.return_type = low("void");
	body.metadata.binding = lowir_model::SBM_INTERNAL;
	lowir_model::Parameter given;
	given.name = "object";
	given.type = low("ptr");
	body.params.push_back(given);
	{
		LowirFunctionLowering lowering(*this, body);
		lowering.open_generated(GeneratedBody());
		lowering.add_destruction(node);
		Instruction leave;
		leave.kind = Instruction::IK_RETURN;
		leave.type.text = "void";
		body.blocks.back().instructions.push_back(leave);
	}
	// The body stands beside the ones this unit writes rather than in among
	// them: a definition being lowered holds a reference into the same list.
	pending_functions_.push_back(body);
	return held;
}

// The bodies `destruction_entry` asked for, written once every definition that
// could ask for one has been.
void LowirUnitLowering::write_pending_functions()
{
	for (std::size_t index = 0; index < pending_functions_.size(); ++index)
	{
		program_.functions.push_back(pending_functions_[index]);
	}
	pending_functions_.clear();
}
