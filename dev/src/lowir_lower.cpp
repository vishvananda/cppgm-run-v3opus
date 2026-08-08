#include "lowir_lower.h"

#include <sstream>
#include <stdexcept>

#include "lowir_abi.h"
#include "sema_scope.h"
#include "token_model.h"

// The program and declaration layer of the PA15 lowering: what a translation
// unit contributes to a LowIR program, and what one function definition is
// before its body is read.

namespace {

using lowir_model::LowType;

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

// The internal LowIR symbol of a namespace-scope name: the regions it is
// declared in, written with `__` where the source writes `::`, and every other
// character an identifier cannot hold written as one `_`.  Dropping those
// characters instead would leave two names one symbol: 12.4p1's `~C` and 12.1's
// `C` are two functions of one class with one qualified name, and 13.5's
// `operator+` and `operator-` are two of one arity and one parameter list.  One
// `_` per character is also what the references write.
std::string flatten_name(const std::string& name)
{
	std::string symbol;
	for (std::size_t index = 0; index < name.size(); ++index)
	{
		const char written = name[index];
		if (written == ':' && index + 1 < name.size() && name[index + 1] == ':')
		{
			symbol += "__";
			++index;
			continue;
		}
		const bool ordinary = (written >= 'a' && written <= 'z') ||
			(written >= 'A' && written <= 'Z') ||
			(written >= '0' && written <= '9') || written == '_';
		symbol += ordinary ? written : '_';
	}
	return symbol;
}

}  // namespace

std::string LowirSymbolTable::function_symbol(const SemaEntity& entity,
                                              const std::string& identity)
{
	const std::string base = flatten_name(abi_qualified_name(entity));
	std::unordered_map<std::string, std::size_t>& seen = overloads_[base];
	// 13.1 lets one name have as many declarations as the program writes, so
	// which of them a function is, is a probe rather than a walk of the ones
	// already named.
	const std::pair<std::unordered_map<std::string, std::size_t>::iterator, bool>
		found = seen.insert(std::make_pair(identity, seen.size()));
	const std::size_t index = found.first->second;
	return index == 0 ? base : base + "__ov" + decimal(index + 1);
}

std::string LowirSymbolTable::object_symbol(const SemaEntity& entity)
{
	return flatten_name(abi_qualified_name(entity));
}

LowirProgramBuilder::LowirProgramBuilder()
	: has_startup_(false)
	, has_shutdown_(false)
{}

LowirUnitLowering::LowirUnitLowering(TypeTable& types,
                                     LowirProgramBuilder& builder)
	: types_(types)
	, builder_(builder)
	, program_(builder.program_)
	, symbols_(builder.symbols_)
	, defined_(builder.defined_)
	, declared_(builder.declared_)
	, startup_(nullptr)
	, shutdown_(nullptr)
	, strings_(builder.strings_)
{}

LowirUnitLowering::~LowirUnitLowering()
{
	delete startup_;
	delete shutdown_;
}

bool LowirUnitLowering::is_signed(TypeId type)
{
	const TypeId bare = types_.strip_cv(type);
	if (types_.kind(bare) == TypeKind::Enum)
	{
		return is_signed(types_.target(bare));
	}
	if (types_.kind(bare) != TypeKind::Fundamental)
	{
		return false;
	}
	const EFundamentalType fundamental = types_.fundamental_type(bare);
	return fundamental != FT_BOOL &&
		fundamental_type_class(fundamental) == FundamentalTypeClass::SignedIntegral;
}

unsigned long long LowirUnitLowering::width(TypeId type)
{
	const TypeId bare = types_.strip_cv(type);
	if (types_.kind(bare) == TypeKind::Enum)
	{
		return width(types_.target(bare));
	}
	return types_.object_size(bare);
}

lowir_model::LowType LowirUnitLowering::low_type(TypeId type)
{
	const TypeId bare = types_.strip_cv(type);
	switch (types_.kind(bare))
	{
	case TypeKind::Pointer:
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
	case TypeKind::Function:
		return low("ptr");

	case TypeKind::Enum:
		// 7.2p8: the values of an enumeration are those of its underlying type,
		// which is what an object of it holds.
		return low_type(types_.target(bare));

	case TypeKind::Array:
	case TypeKind::Class:
		// 3.9p5: an incomplete type has no size, so there is no storage for
		// LowIR to name.  8.3.5p6 lets a function be *declared* with such a
		// return type as long as nothing calls or defines it, and what that
		// declaration says about its result is that there is none.
		if (types_.is_incomplete(bare))
		{
			return low("void");
		}
		return low("obj<" + decimal(types_.object_size(bare)) + "x" +
		           decimal(types_.object_align(bare)) + ">");

	default:
		break;
	}
	switch (types_.fundamental_type(bare))
	{
	case FT_VOID: return low("void");
	case FT_BOOL: return low("u8");
	case FT_CHAR:
	case FT_SIGNED_CHAR: return low("i8");
	case FT_UNSIGNED_CHAR: return low("u8");
	case FT_SHORT_INT: return low("i16");
	case FT_UNSIGNED_SHORT_INT:
	case FT_CHAR16_T: return low("u16");
	case FT_INT:
	case FT_WCHAR_T: return low("i32");
	case FT_UNSIGNED_INT:
	case FT_CHAR32_T: return low("u32");
	case FT_FLOAT: return low("f32");
	case FT_DOUBLE: return low("f64");
	case FT_LONG_DOUBLE: return low("f80");
	// 3.9.1p10: `std::nullptr_t` is a distinct type whose object is the size of
	// a pointer and holds no address at all, so LowIR stores it as the integer
	// of that width rather than as an address something could be loaded from.
	case FT_NULLPTR_T: return low("i64");
	default: break;
	}
	// 3.9.1p2: every remaining integral type is one of the two 8 byte ones, and
	// LowIR spells both as `i64`; the operator says which is unsigned.
	return low("i64");
}

void LowirUnitLowering::open_signature(
	TypeId returned, std::vector<lowir_model::Parameter>& params,
	lowir_model::LowType& result)
{
	if (!types_.returns_indirectly(returned))
	{
		result = low_type(returned);
		return;
	}
	// 6.6.3p2 and 12.8p31: the returned object stands in storage of the
	// caller's, so the destination is what the call passes and the function
	// hands nothing back.
	lowir_model::Parameter destination;
	destination.name = "ret";
	destination.type = low("ptr");
	destination.metadata.passing = lowir_model::PPM_INDIRECT_RESULT;
	params.push_back(destination);
	result = low("void");
}

void LowirUnitLowering::describe_parameter(TypeId written,
                                           lowir_model::Parameter& parameter)
{
	if (types_.is_reference(written))
	{
		// 8.5.3: a reference parameter is passed as the address of what it was
		// bound to, which the boundary says rather than the type.
		parameter.type = low("ptr");
		parameter.metadata.passing = lowir_model::PPM_REFERENCE;
		return;
	}
	if (types_.passes_indirectly(written))
	{
		// 5.2.2p4: the parameter and the argument are one object, standing in
		// storage of the caller's, so what crosses the boundary is its address.
		parameter.type = low("ptr");
		parameter.metadata.passing = lowir_model::PPM_BY_ADDRESS;
		return;
	}
	parameter.type = low_type(written);
}

// A name is used as often as the program writes it, and its symbol is a fact
// about the declaration rather than about the use, so each declaration is
// flattened - and a function's signature described and signed - once.
const std::string& LowirUnitLowering::global_symbol(const SemaEntity& entity)
{
	std::string& held = entity_symbols_[entity.id];
	if (held.empty())
	{
		held = LowirSymbolTable::object_symbol(entity);
	}
	return held;
}

// 12.1 and 12.4: a constructor or a destructor nothing in this unit ran on a
// complete object is the base-object entry alone, which the internal name says
// as plainly as the object name does.  Every other function is one entry and
// keeps the name its declaration flattens to.
unsigned LowirUnitLowering::abi_variant(const SemaEntity& entity)
{
	return entity.special != kOrdinaryFunction && !entity.complete_object_entry &&
	       !writes_base_entry(entity)
		? kBaseObjectAbi
		: kCompleteObjectAbi;
}

// 12.1 and 12.4: a constructor or destructor a complete object and a base class
// subobject both asked for stands under both of the ABI's entry points.  This
// milestone has no virtual base, so the two do the same thing, but they are two
// symbols the object file has to hold - and the references write them as two
// definitions rather than as one and an alias.
//
// 9.3p2 says which of the two questions is being asked.  A definition this unit
// holds that no other unit may hold - one written outside its class without
// `inline` - is the program's one definition of the function, so the object
// file owes both of the ABI's names whichever of them a call happened to write
// here.  A definition every unit that needs one may hold owes only the names
// this unit named, and a function this unit does not define owes none at all:
// what a call wrote is what is declared.
bool LowirUnitLowering::writes_base_entry(const SemaEntity& entity)
{
	if (entity.special == kOrdinaryFunction)
	{
		return false;
	}
	if (entity.defined && (!entity.inline_function || entity.internal_linkage))
	{
		// 3.5p4 and 3.5p3: a definition with internal linkage is this unit's
		// alone for the same reason one written outside its class is - no other
		// unit may hold it - so the object file owes both of the ABI's names
		// whichever of them a use here happened to write.
		return true;
	}
	return entity.complete_object_entry && entity.base_object_entry;
}

const std::string& LowirUnitLowering::function_symbol(const SemaEntity& entity,
                                                      bool base_subobject)
{
	if (base_subobject && writes_base_entry(entity))
	{
		std::string& base = base_entry_symbols_[entity.id];
		if (base.empty())
		{
			base = function_symbol(entity) + "__base_entry";
		}
		return base;
	}
	std::string& held = entity_symbols_[entity.id];
	if (held.empty())
	{
		// 13.5: two operator functions of one region can flatten to one base
		// name and take the same parameter types, so what tells the internal
		// symbols apart is the name the object file gives each - which is also
		// what makes a second unit's declaration of one function reach the
		// symbol this one named.
		held = symbols_.function_symbol(
			entity, abi_symbol_of(entity, types_, kCompleteObjectAbi));
		if (abi_variant(entity) == kBaseObjectAbi)
		{
			held += "__base_entry";
		}
	}
	return held;
}

void LowirUnitLowering::describe_symbol(const SemaEntity& entity,
                                        lowir_model::SymbolMetadata& metadata,
                                        const std::string& symbol,
                                        bool base_entry)
{
	// 3.5p3: a definition of a name with internal linkage belongs to the
	// translation unit that wrote it and no other may reach it.  7.1.2p4 makes
	// an inline definition one the whole program shares, so every unit that
	// holds one holds the same definition and none of them owns it.
	metadata.binding = entity.internal_linkage
		? lowir_model::SBM_INTERNAL
		: (entity.inline_function ? lowir_model::SBM_WEAK
		                          : lowir_model::SBM_STRONG);
	// 7.5p1: the language linkage a backend needs is a fact about the
	// declaration, which no LowIR type says.
	if (entity.c_linkage)
	{
		metadata.linkage = lowir_model::LLM_C;
	}
	// 3.5p9: the object file names the entity, and PA14's encoder is what says
	// how.  The internal LowIR symbol is a spelling of this program alone, so
	// the object name is carried only where the two differ.
	const std::string object = abi_symbol_of(
		entity, types_, base_entry ? kBaseObjectAbi : abi_variant(entity));
	if (object != symbol)
	{
		metadata.object_symbol = object;
	}
}

void LowirProgramBuilder::add_unit(const DumpNode& unit, TypeTable& types)
{
	LowirUnitLowering lowering(types, *this);
	lowering.run(unit);
}

void LowirProgramBuilder::finish()
{
	// 3.6.2p2 and 3.6.3p1: every unit's actions are in, so the two bodies that
	// run them are closed here rather than by whichever unit happened to add to
	// one last.
	lowir_model::Instruction leave;
	leave.kind = lowir_model::Instruction::IK_RETURN;
	leave.type.text = "void";
	if (has_startup_)
	{
		startup_.blocks.back().instructions.push_back(leave);
		// 3.6.2p2: a declaration with static storage duration whose
		// initialization comes to nothing still opened this body, so it can be
		// one `return` and no action at all.  It is written anyway, because the
		// references write it for every namespace-scope object of class type
		// and the checked-in fixtures ask for it - and what the failure map
		// names is the one place they do not, which is 9p6's object of an empty
		// class, settled where that object's own construction is.
		program_.functions.push_back(startup_);
		has_startup_ = false;
		startup_ = lowir_model::Function();
		startup_body_ = GeneratedBody();
	}
	if (has_shutdown_)
	{
		shutdown_.blocks.back().instructions.push_back(leave);
		program_.functions.push_back(shutdown_);
		has_shutdown_ = false;
		shutdown_ = lowir_model::Function();
		shutdown_body_ = GeneratedBody();
	}
}

void LowirUnitLowering::run(const DumpNode& unit)
{
	// 3.5: a definition names the entity every use of the name in the program
	// reaches, and the resolved tree is walked in source order, so a call
	// written before the definition it reaches has to know it is coming.  One
	// pass over the top level answers that for the whole unit.
	collect_definitions(unit);
	// 3.2p3: an inline definition belongs to the program where the program uses
	// it, and a use written in a body this unit does not write is a use all the
	// same - a function called only from an unused one is odr-used by it.  So
	// the whole resolved tree is read for uses, in the order it names them.
	demand_referenced(unit);
	// 3.7.2p2 and 3.6.2p4: a use of a thread-local object runs what initializes
	// it, and whether this unit initializes one at all is what its definition
	// says - so a use written before that definition has to know the answer,
	// exactly as a call written before a definition does.  The definitions with
	// thread storage duration are lowered first, in the order the unit writes
	// them, which is the order a thread begins them in.
	thread_definitions(unit);
	write_thread_bodies();
	drain_demanded();
	for (std::size_t index = 0; index < unit.children.size(); ++index)
	{
		declaration(*unit.children[index]);
		drain_demanded();
	}
	drain_demanded();
	// A definition no body this unit wrote asked for stands after them all:
	// what a written use asks for is written where that use is, so the order
	// the unit already has is the order its own uses were reached in.
	for (std::size_t index = 0; index < referenced_.size(); ++index)
	{
		demand_definition_by_id(referenced_[index]);
		drain_demanded();
	}
	if (startup_ != nullptr)
	{
		startup_->suspend_generated(builder_.startup_body_);
	}
	if (shutdown_ != nullptr)
	{
		shutdown_->suspend_generated(builder_.shutdown_body_);
	}
}

// 3.7.2p2: the definitions of this unit that lay out one object per thread,
// lowered before any body of the unit is, so that the answer to "does a use of
// this name run something first" is the same wherever the use is written.  A
// definition writes its global once - `emitted_globals_` is what says so - and
// the pass over the unit that follows reaches these nodes again and writes
// nothing.
void LowirUnitLowering::thread_definitions(const DumpNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		if (child.fact.kind == FactKind::Namespace)
		{
			thread_definitions(child);
		}
		else if (child.fact.kind == FactKind::Variable &&
		         child.fact.entity != nullptr &&
		         child.fact.entity->thread_storage &&
		         child.fact.entity->object_definition)
		{
			global_variable(child);
		}
	}
}

void LowirUnitLowering::collect_definitions(const DumpNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const DumpNode& child = *node.children[index];
		if (child.fact.kind == FactKind::FunctionDefinition &&
		    child.fact.entity != nullptr)
		{
			defined_.insert(function_symbol(*child.fact.entity));
			if (writes_base_entry(*child.fact.entity))
			{
				// 12.1 and 12.4: the body stands under both of the ABI's entry
				// points, so the unit holds the base-object name too and owes
				// the program no declaration of it.
				defined_.insert(function_symbol(*child.fact.entity, true));
			}
			// 3.6.2p2 folds a call of a constructor into the image of the
			// object it initializes, which is what the constructor's own
			// definition says, so every definition is indexed by what it
			// defines whether or not a use has asked for it yet.
			bodies_[child.fact.entity->id] = &child;
			if (child.fact.entity->inline_function &&
			    !(child.fact.entity->friend_definition &&
			      child.fact.entity->internal_linkage))
			{
				// 7.1.2p4: the definition is the program's rather than this
				// unit's, and 3.2p3 puts it in the program only where a use
				// asks for it.
				deferred_[child.fact.entity->id] = &child;
			}
			// 3.5p4 and 11.3p5: a friend definition with internal linkage is
			// one no other unit may hold and none of this unit's ordinary
			// lookups reaches, so there is no use for it to wait for - the
			// object file owes it where it stands.
		}
		else if (child.fact.kind == FactKind::Variable &&
		         child.fact.entity != nullptr &&
		         child.fact.entity->object_definition)
		{
			defined_.insert(global_symbol(*child.fact.entity));
		}
		else if (child.fact.kind == FactKind::Namespace)
		{
			collect_definitions(child);
		}
	}
}

void LowirUnitLowering::declaration(const DumpNode& node)
{
	switch (node.fact.kind)
	{
	case FactKind::Namespace:
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			declaration(*node.children[index]);
		}
		return;

	case FactKind::Variable:
		global_variable(node);
		return;

	case FactKind::DestructorAction:
		static_destructor(node);
		return;

	case FactKind::FunctionDefinition:
		if (node.fact.entity != nullptr &&
		    deferred_.find(node.fact.entity->id) != deferred_.end())
		{
			// 9.3p2 and 3.2p3: the definition of a member function written in
			// its class waits for a use of it, which `collect_definitions` has
			// already recorded it against.
			return;
		}
		function_definition(node);
		return;

	default:
		// A declaration of a function this unit also defines names the
		// definition; only one the program has no body for needs a line of its
		// own, and it is written where a use of it asks for it.
		return;
	}
}

// 3.9.1p1 and 4.7p2: the bits an object of `type` holds when it is given
// `bits`, which is all of them at its own width and nothing above it.
unsigned long long LowirUnitLowering::narrowed(TypeId type,
                                               unsigned long long bits)
{
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
	if (!digits.empty())
	{
		// 2.14.4p1: a floating-literal carries at most one floating-suffix.
		const char last = digits[digits.size() - 1];
		if (last == 'f' || last == 'F' || last == 'l' || last == 'L')
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

// 3.6.2p2: the constant an item of the program image holds, spelled at the type
// the storage has.  An integral value is the one 5.19's fold works out; a
// floating one is the spelling 2.14.4 gave it, because there is no integer of
// this translation that is that value.
bool LowirUnitLowering::image_value(const DumpNode& node, TypeId type,
                                    std::string& text)
{
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
	    fact.entity->constant)
	{
		bits = fact.entity->value;
		return true;
	}
	if (fact.kind == FactKind::Cast && node.children.size() == 1)
	{
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

void LowirUnitLowering::global_variable(const DumpNode& node)
{
	SemaEntity& entity = *node.fact.entity;
	const std::string symbol = global_symbol(entity);
	if (!node.fact.object_definition)
	{
		// 3.1p2 and 3.2p3: the declaration defines nothing, so the object it
		// names is another unit's.  What this unit owes the program is a name
		// for the storage its own code reaches, and a declaration no code here
		// reaches names nothing - so the line is written where a use asks for
		// it, as the declaration of a function this unit does not define is.
		return;
	}
	// 3.2p3: one definition in one program, however many units declare it.
	defined_.insert(symbol);
	if (!builder_.emitted_globals_.insert(symbol).second)
	{
		return;
	}
	lowir_model::GlobalDefinition global;
	global.name = symbol;
	const TypeId type = node.fact.type;
	global.type = low_type(type);
	describe_symbol(entity, global.metadata, symbol);
	const DumpNode* written = node.children.empty() ? nullptr : node.children[0];
	if (written != nullptr && written->fact.kind == FactKind::BracedInitList &&
	    types_.kind(types_.strip_cv(type)) != TypeKind::Array)
	{
		// 8.5.1p2: a scalar initialized from a list holds what its one clause
		// says, or zero when the list is empty.
		written = written->children.empty() ? nullptr : written->children[0];
	}
	const DumpNode* dynamic = nullptr;
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
		const SemaEntity& built = *written->children[0]->children[0]->fact.entity;
		const bool trivial = built.trivial;
		const bool nothing_to_do = trivial &&
			(written->fact.zero_initialized ||
			 types_.kind(types_.strip_cv(type)) == TypeKind::Array ||
			 types_.is_empty_class(types_.strip_cv(type)));
		if (nothing_to_do)
		{
			// 3.2p2: the initialization still named the constructor.
			owe_internal_definition(built);
		}
		dynamic = nothing_to_do ? nullptr : written;
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
		// 3.6.2p2: an object whose initializer is not a constant starts as
		// zero and is given its value before the program runs.
		global.init_kind = lowir_model::GlobalDefinition::INIT_ZERO;
		dynamic = written;
	}
	if (entity.thread_storage)
	{
		// 3.7.2p1: the definition lays out one object per thread rather than
		// one per program, and 3.7.2p2 gives the ABI a function to reach it
		// through.
		global.storage = lowir_model::GSM_THREAD_LOCAL;
		thread_wrapper(symbol, abi_thread_wrapper_of(entity),
		               global.metadata.binding);
	}
	program_.globals.push_back(global);
	if (!entity.thread_storage)
	{
		if (dynamic != nullptr)
		{
			dynamic_initializer(entity, *dynamic, type);
		}
		return;
	}
	// 12.4p11 and 3.7.2p2: the end of the object's lifetime is a fact of the
	// declaration and not of the initializer it was given, and the point the
	// runtime is handed it at is the one point per thread this unit writes.  So
	// the body is what the declaration asks for - an initialization to run, a
	// lifetime to end, or both - and an object whose value the image already
	// holds and whose class ends its lifetime with nothing still gets none.
	const DumpNode* const destruction = thread_destruction(node);
	if (dynamic == nullptr && destruction == nullptr)
	{
		return;
	}
	thread_initializer(symbol, dynamic, type, destruction);
}

// 3.7.2p2: the bodies the thread-local definitions of this unit asked for,
// written once every one of those definitions has said whether it asked for
// one.  A body reaches the objects of its own thread as any other code does, so
// what one of them names has to already have the answer - which is why the
// bodies are written after the definitions rather than as each is reached.
void LowirUnitLowering::write_thread_bodies()
{
	for (std::size_t index = 0; index < thread_bodies_.size(); ++index)
	{
		// A copy, not a reference: the vector is the one place a definition
		// records what it asked for, and nothing a body writes may leave a
		// reference into it dangling.
		const ThreadBody held = thread_bodies_[index];
		lowir_model::Function body;
		body.name = held.body;
		body.return_type = low("void");
		body.metadata.binding = lowir_model::SBM_INTERNAL;
		lowir_model::Operand storage;
		storage.kind = lowir_model::Operand::OP_GLOBAL;
		storage.text = held.symbol;
		// 3.2p2: filling an object's storage is not a use of the name that has
		// to fill it first, so the one body that must not call this one is this
		// one.  Every other thread-local a body names is reached the way a body
		// of the program reaches it.
		writing_thread_body_ = held.symbol;
		{
			LowirFunctionLowering lowering(*this, body);
			lowering.open_generated(GeneratedBody());
			lowering.add_thread_initialization(guard_of(held.symbol), storage,
			                                   held.type, held.initialization,
			                                   held.destruction);
			lowir_model::Instruction leave;
			leave.kind = lowir_model::Instruction::IK_RETURN;
			leave.type.text = "void";
			body.blocks.back().instructions.push_back(leave);
		}
		writing_thread_body_.clear();
		program_.functions.push_back(body);
	}
	thread_bodies_.clear();
}

// 3.7.2p2: the action that ends the lifetime of the object a declaration
// declared, which for a thread-local object stands under the declaration
// because no point of the program is where it runs.  Null where the class ends
// the lifetime with nothing to run.
const DumpNode* LowirUnitLowering::thread_destruction(const DumpNode& node)
{
	for (std::size_t index = node.children.size(); index-- > 0; )
	{
		if (node.children[index]->fact.kind == FactKind::DestructorAction)
		{
			return node.children[index];
		}
	}
	return nullptr;
}

void LowirUnitLowering::thread_wrapper(const std::string& symbol,
                                       const std::string& object,
                                       lowir_model::SymbolBindingMode binding)
{
	lowir_model::FunctionDeclaration wrapper;
	wrapper.name = "__cppgm_tls_wrapper__" + symbol;
	wrapper.return_type = low("ptr");
	wrapper.metadata.binding = binding;
	wrapper.metadata.object_symbol = object;
	wrapper.metadata.tls_for_symbol = symbol;
	program_.function_declarations.push_back(wrapper);
}

// 3.7.2p2 and 3.6.2p2: what an object with thread storage duration asks a
// thread to run - an initializer the translation does not settle, the handing
// of 12.4p11's destruction to the runtime, or both - belongs to a body of that
// object's own rather than to the program's one startup function.  The flag
// that says a thread has run it is itself an object of that thread, and both it
// and the object it guards are reached the way any other thread-local global
// is.
void LowirUnitLowering::thread_initializer(const std::string& symbol,
                                           const DumpNode* node, TypeId type,
                                           const DumpNode* destruction)
{
	lowir_model::GlobalDefinition guard;
	guard.name = guard_of(symbol);
	guard.type = low("i64");
	guard.storage = lowir_model::GSM_THREAD_LOCAL;
	guard.metadata.binding = lowir_model::SBM_INTERNAL;
	guard.init_kind = lowir_model::GlobalDefinition::INIT_ZERO;
	thread_wrapper(guard.name, std::string(), lowir_model::SBM_INTERNAL);
	program_.globals.push_back(guard);

	ThreadBody held;
	held.symbol = symbol;
	held.body = "__cppgm_tls_init__" + symbol;
	held.initialization = node;
	held.type = type;
	held.destruction = destruction;
	thread_bodies_.push_back(held);
	// The name is what a use of the object calls, and it is known as soon as
	// the definition has asked for a body - before the body is written, so that
	// one body naming another object of the same thread reaches it.
	builder_.thread_initializers_[symbol] = held.body;
}

// 3.7.2p2: the flag of the same thread that says a thread has run what one
// thread-local object asked it to, named after that object.
std::string LowirUnitLowering::guard_of(const std::string& symbol)
{
	return "__cppgm_tls_guard__" + symbol;
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
			else if (image_value(*child.children[0], child.fact.type,
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
		global.data_items.push_back(item);
		at = offset + stride;
	}
	return true;
}

// 3.6.2p2: the value an object with static storage duration holds before the
// program runs, where a constructor is what initializes it.  A constructor
// whose whole definition is 12.6.2's member initializations, each of a value
// the translation knows once the parameters hold the arguments the call passed,
// leaves the object holding one image - so the object holds it and no startup
// body writes it.  The definition is read once per call, and only the calls a
// constant initializer holds ever ask.
bool LowirUnitLowering::global_constructed(
	lowir_model::GlobalDefinition& global, const DumpNode& action,
	unsigned long long base, unsigned long long& at)
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
	// once and every value read below is one probe of that map.
	std::unordered_map<std::uint32_t, const DumpNode*> bound;
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
		bound[child.fact.entity->id] = call.children[argument];
		++argument;
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
		if (child.fact.kind != FactKind::MemberInitialization ||
		    child.fact.entity == nullptr || child.children.size() < 2)
		{
			// 12.6.2p5's base subobject, an array member's per-element
			// construction, and a member left default-initialized are each a
			// shape this does not fold.
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
		if (value->fact.entity != nullptr)
		{
			const std::unordered_map<std::uint32_t, const DumpNode*>::const_iterator
				passed = bound.find(value->fact.entity->id);
			if (passed != bound.end())
			{
				value = passed->second;
			}
		}
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
		else if (image_value(*value, type, item.literal_operand.text))
		{
			item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
			item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
		}
		else
		{
			// 3.6.2p2: the value is not one the translation knows, so what the
			// constructor does is what the program runs.
			return false;
		}
		global.data_items.push_back(item);
		at = offset + types_.object_size(types_.strip_cv(type));
	}
	if (!constructor.member_entry)
	{
		// 3.2p2: the initialization named the constructor the program wrote,
		// which odr-uses it however the translation then works out what it
		// leaves the object holding.  The one 8.5.1 gave an aggregate is named
		// by nothing the program wrote, so folding its call leaves no use of it
		// at all.
		demand_definition(constructor);
	}
	return true;
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
		return false;
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
	if (!image_value(node, type, global.init_operand.text))
	{
		return false;
	}
	global.init_kind = lowir_model::GlobalDefinition::INIT_INTEGER;
	global.init_operand.kind = lowir_model::Operand::OP_INTEGER;
	return true;
}

void LowirUnitLowering::dynamic_initializer(const SemaEntity& entity,
                                            const DumpNode& node, TypeId type)
{
	if (startup_ == nullptr)
	{
		if (!builder_.has_startup_)
		{
			builder_.has_startup_ = true;
			builder_.startup_.name = "__cppgm_init";
			builder_.startup_.return_type = low("void");
			builder_.startup_.metadata.role = lowir_model::SR_INIT;
			builder_.startup_.metadata.binding = lowir_model::SBM_INTERNAL;
		}
		startup_ = new LowirFunctionLowering(*this, builder_.startup_);
		startup_->open_generated(builder_.startup_body_);
	}
	lowir_model::Operand storage;
	storage.kind = lowir_model::Operand::OP_GLOBAL;
	storage.text = global_symbol(entity);
	startup_->add_initialization(storage, type, node);
}

// 3.6.3p1: the destructor of an object with static storage duration runs when
// the program ends, which is one body of the program however many units add to
// it.
void LowirUnitLowering::static_destructor(const DumpNode& node)
{
	if (shutdown_ == nullptr)
	{
		if (!builder_.has_shutdown_)
		{
			builder_.has_shutdown_ = true;
			builder_.shutdown_.name = "__cppgm_fini";
			builder_.shutdown_.return_type = low("void");
			builder_.shutdown_.metadata.role = lowir_model::SR_FINI;
			builder_.shutdown_.metadata.binding = lowir_model::SBM_INTERNAL;
		}
		shutdown_ = new LowirFunctionLowering(*this, builder_.shutdown_);
		shutdown_->open_generated(builder_.shutdown_body_);
	}
	shutdown_->add_destruction(node);
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
		if (!image_value(*node->children[index], element,
		                 item.literal_operand.text))
		{
			// 3.6.2p2: the element's value is not one the translation knows,
			// so the whole array is given what it holds before the program
			// runs.
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

std::string LowirUnitLowering::string_literal(const std::string& data,
                                              TypeId array)
{
	const TypeId element = types_.strip_cv(types_.target(types_.strip_cv(array)));
	// 2.14.5p8: the object a literal is, is its code units read at the width of
	// its element type, so two literals are one object only when both agree.
	const std::string key = low_type(element).text + ":" + data;
	const std::unordered_map<std::string, std::string>::const_iterator found =
		strings_.find(key);
	if (found != strings_.end())
	{
		return found->second;
	}
	const unsigned long long stride = types_.object_size(element);
	const std::string symbol =
		"__strlit__" + decimal(strings_.size() + 1);
	strings_[key] = symbol;
	lowir_model::GlobalDefinition global;
	global.name = symbol;
	global.structured = true;
	global.metadata.binding = lowir_model::SBM_INTERNAL;
	for (std::size_t at = 0; at + stride <= data.size(); at += stride)
	{
		unsigned long long bits = 0;
		for (std::size_t byte = stride; byte-- > 0;)
		{
			bits = (bits << 8) |
				static_cast<unsigned char>(data[at + byte]);
		}
		lowir_model::GlobalDefinition::DataItem item;
		item.type = low_type(element);
		item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
		// 2.14.5: the array holds code units, which are the values the
		// execution character set gives them and not a signed reading of them.
		item.literal_operand.text = decimal(bits);
		global.data_items.push_back(item);
	}
	program_.globals.push_back(global);
	defined_.insert(symbol);
	builder_.emitted_globals_.insert(symbol);
	return symbol;
}

// 3.2p2: the function names a body reads, which is what odr-uses the function
// it names.  A constructor or a destructor is left out: 12.1p5 gives a class
// one whether or not a program ever runs it, and this milestone writes such a
// helper only where a lifetime the unit lowers asks for it.
//
// 3.2p3 and 7.1.2p4: a definition no one unit owns is part of the program only
// where the program uses it, and a name written inside such a definition is a
// use only if that definition is itself one - so the walk stops at a deferred
// body rather than reading it.  What a deferred body names is asked for as it
// is lowered, which happens exactly when a use reaches it, so the definitions
// this unit writes are the ones a root of the unit reaches and no others.
//
// 11.3p5's friend definition is the one this walk still reads: it defines a
// member of the enclosing namespace, and the class that wrote it is the only
// place this unit reads it, so what it names is read where it is.
void LowirUnitLowering::demand_referenced(const DumpNode& node)
{
	if (node.fact.kind == FactKind::FunctionDefinition &&
	    node.fact.entity != nullptr && !node.fact.entity->friend_definition &&
	    deferred_.find(node.fact.entity->id) != deferred_.end())
	{
		return;
	}
	if (node.fact.entity != nullptr &&
	    node.fact.entity->kind == SemaKind::Function &&
	    node.fact.entity->special == kOrdinaryFunction &&
	    (node.fact.kind == FactKind::Callee || node.fact.kind == FactKind::Id))
	{
		referenced_.push_back(node.fact.entity->id);
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		demand_referenced(*node.children[index]);
	}
}

namespace
{

const unsigned char kEmptyUnknown = 0;
const unsigned char kEmptyOpen = 1;
const unsigned char kEmptyYes = 2;
const unsigned char kEmptyNo = 3;

// 12.6.2: the constructor one step of a constructor's body runs, which is the
// callee of the call that step is written as.  Null for a step that is not one.
const SemaEntity* step_constructor(const DumpNode& step)
{
	if (step.fact.kind != FactKind::ConstructorAction || step.children.empty() ||
	    step.children[0]->children.empty())
	{
		return nullptr;
	}
	return step.children[0]->children[0]->fact.entity;
}

}  // namespace

// 12.1p5 asks what a class *wrote*; this asks what running the constructor
// *does*, which is the question a call of it has to answer.  A constructor the
// program wrote is its body: an empty one does nothing however non-trivial
// 12.1p5 calls it, and anything written in it does something.  One the standard
// gave the class is the steps 12.6.2 gives it, so it does nothing when each of
// its bases and members does nothing - which is this same question asked one
// class further in, and answered once per constructor.
bool LowirUnitLowering::construction_writes_nothing(const SemaEntity& constructor)
{
	unsigned char& held = empty_construction_[constructor.id];
	if (held != kEmptyUnknown)
	{
		// A class holds no subobject of its own type, so the walk cannot come
		// back to a constructor it is already answering; if it ever did, the
		// answer that keeps a call written is the safe one.
		return held == kEmptyYes;
	}
	held = kEmptyOpen;
	const std::unordered_map<std::uint32_t, const DumpNode*>::const_iterator
		found = bodies_.find(constructor.id);
	bool nothing = found != bodies_.end();
	if (nothing)
	{
		const DumpNode& body = *found->second;
		for (std::size_t index = 0; index < body.children.size() && nothing;
		     ++index)
		{
			const DumpNode& child = *body.children[index];
			switch (child.fact.kind)
			{
			case FactKind::Parameter:
				break;

			case FactKind::Compound:
				// 12.6.2p10: the body runs after the subobjects are built, and
				// a body the program wrote nothing in runs nothing.
				nothing = child.children.empty();
				break;

			case FactKind::ConstructorAction:
			{
				// 12.6.2p2: a step written with arguments carries values the
				// program named into the subobject, so it is a step that does
				// something whatever the constructor it names comes to - which
				// is what tells 12.9's inherited constructor, forwarding its
				// parameters to the base, from a default constructor whose
				// steps carry nothing.
				const SemaEntity* const built = step_constructor(child);
				nothing = built != nullptr && !child.fact.zero_initialized &&
					child.children[0]->children.size() <= 2 &&
					(built->trivial || construction_writes_nothing(*built));
				break;
			}

			default:
				// 12.6.2p8's member initialization, 8.5.1's clauses and every
				// other step store something into the object.
				nothing = false;
				break;
			}
		}
	}
	// The map may have grown while this was being answered, so the entry is
	// found again rather than held across the recursion.
	empty_construction_[constructor.id] = nothing ? kEmptyYes : kEmptyNo;
	return nothing;
}

void LowirUnitLowering::owe_elided_construction(const SemaEntity& constructor,
                                                unsigned depth)
{
	const std::unordered_map<std::uint32_t, const DumpNode*>::const_iterator
		found = bodies_.find(constructor.id);
	if (found == bodies_.end() || depth > 64)
	{
		return;
	}
	const DumpNode& body = *found->second;
	for (std::size_t index = 0; index < body.children.size(); ++index)
	{
		const DumpNode& child = *body.children[index];
		const SemaEntity* const built = step_constructor(child);
		if (built == nullptr)
		{
			continue;
		}
		if (built->trivial)
		{
			// 12.1p5: nothing at all was ever going to run, so no other unit
			// is owed a definition either - unless 3.5p4 leaves this one no
			// other unit may hold.
			owe_internal_definition(*built);
			continue;
		}
		// 8.4.2p5: the question is who wrote the *definition*, which is the
		// standard wherever the declaration was defaulted - on its first
		// declaration or on a later one - so this reads what
		// `constructor_call` reads and the two never disagree about whether a
		// call was left out.
		if (!(built->user_provided && !built->defaulted) &&
		    construction_writes_nothing(*built))
		{
			owe_internal_definition(*built);
			owe_elided_construction(*built, depth + 1);
			continue;
		}
		// 3.2p2: the elided body would have called this one, which is a use of
		// it however the call came to be left out.
		declare_call_target(*built, child.fact.base_subobject);
	}
}

void LowirUnitLowering::owe_internal_definition(const SemaEntity& entity)
{
	if (entity.internal_linkage)
	{
		demand_definition(entity);
	}
}

void LowirUnitLowering::demand_definition(const SemaEntity& entity)
{
	demand_definition_by_id(entity.id);
}

void LowirUnitLowering::demand_definition_by_id(std::uint32_t entity)
{
	const std::unordered_map<std::uint32_t, const DumpNode*>::iterator found =
		deferred_.find(entity);
	if (found == deferred_.end())
	{
		return;
	}
	demanded_.push_back(found->second);
	deferred_.erase(found);
}

void LowirUnitLowering::drain_demanded()
{
	while (!demanded_.empty())
	{
		const DumpNode& node = *demanded_.back();
		demanded_.pop_back();
		function_definition(node);
	}
}

// 3.7.2p2: the runtime function a destruction that runs at the end of a thread
// is handed to.  It is the implementation's own, not this program's, so it is
// named as the implementation names it and declared once per program.
const std::string& LowirUnitLowering::thread_atexit_symbol()
{
	static const std::string kSymbol = "__external_runtime____cxa_thread_atexit";
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
		declaration.metadata.object_symbol = "__cxa_thread_atexit";
		program_.function_declarations.push_back(declaration);
	}
	return kSymbol;
}

// 3.6.3p3 and the ABI: the handle that tells the runtime which loaded image a
// registered destruction belongs to, so that unloading one runs its own.
const std::string& LowirUnitLowering::image_handle_symbol()
{
	static const std::string kSymbol = "__external_runtime____dso_handle";
	if (declared_.insert(kSymbol).second)
	{
		lowir_model::GlobalDeclaration declaration;
		declaration.name = kSymbol;
		declaration.metadata.binding = lowir_model::SBM_STRONG;
		declaration.metadata.object_symbol = "__dso_handle";
		program_.global_declarations.push_back(declaration);
	}
	return kSymbol;
}

const std::string* LowirUnitLowering::thread_initializer_of(
	const SemaEntity& entity)
{
	if (!entity.thread_storage)
	{
		return nullptr;
	}
	const std::string& symbol = global_symbol(entity);
	if (symbol == writing_thread_body_)
	{
		return nullptr;
	}
	const std::unordered_map<std::string, std::string>::const_iterator found =
		builder_.thread_initializers_.find(symbol);
	return found == builder_.thread_initializers_.end() ? nullptr
	                                                    : &found->second;
}

void LowirUnitLowering::declare_entity(const SemaEntity& entity)
{
	if (entity.kind == SemaKind::Function)
	{
		demand_definition(entity);
		add_function_declaration(entity);
		return;
	}
	const std::string symbol = global_symbol(entity);
	if (defined_.count(symbol) != 0 || !declared_.insert(symbol).second)
	{
		return;
	}
	lowir_model::GlobalDeclaration declaration;
	declaration.name = symbol;
	// 3.9p6: an array of unknown bound has no layout here, and a declaration
	// that gives none is what says so.
	const TypeId type = types_.strip_cv(entity.type);
	declaration.has_type = types_.kind(type) != TypeKind::Array &&
		types_.kind(type) != TypeKind::Class;
	if (declaration.has_type)
	{
		declaration.type = low_type(entity.type);
	}
	describe_symbol(entity, declaration.metadata, symbol);
	if (entity.thread_storage)
	{
		// 3.7.2p1: the object another translation unit defines is one per
		// thread there too, and a use of it here reaches it through the same
		// surface 3.7.2p2 gives one this unit defines.
		declaration.storage = lowir_model::GSM_THREAD_LOCAL;
		thread_wrapper(symbol, abi_thread_wrapper_of(entity),
		               declaration.metadata.binding);
	}
	program_.global_declarations.push_back(declaration);
}

void LowirUnitLowering::declare_call_target(const SemaEntity& entity,
                                            bool base_entry)
{
	demand_definition(entity);
	add_function_declaration(entity, base_entry);
}

void LowirUnitLowering::add_function_declaration(const SemaEntity& entity)
{
	add_function_declaration(entity, false);
	if (writes_base_entry(entity))
	{
		// 12.1 and 12.4: a complete object and a base class subobject each
		// asked this unit for the constructor or destructor, so calls here name
		// both of the ABI's entry points.  A name a call writes is one the unit
		// owes the program a declaration of, whether or not it holds the body.
		add_function_declaration(entity, true);
	}
}

void LowirUnitLowering::add_function_declaration(const SemaEntity& entity,
                                                 bool base_entry)
{
	const std::string symbol = function_symbol(entity, base_entry);
	if (defined_.count(symbol) != 0 || !declared_.insert(symbol).second)
	{
		return;
	}
	lowir_model::FunctionDeclaration declaration;
	declaration.name = symbol;
	open_signature(types_.target(entity.type), declaration.params,
	               declaration.return_type);
	const std::vector<TypeId>& parameters = types_.parameters(entity.type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		lowir_model::Parameter parameter;
		parameter.name = "arg" + decimal(index);
		describe_parameter(parameters[index], parameter);
		declaration.params.push_back(parameter);
	}
	if (types_.variadic(entity.type))
	{
		declaration.boundary.arity = lowir_model::CAM_VARIADIC;
	}
	describe_builtin(entity, declaration);
	describe_symbol(entity, declaration.metadata, symbol, base_entry);
	program_.function_declarations.push_back(declaration);
}

// 1.4p8: what a backend may assume about a call of a function the
// implementation provides.  These are facts of the reserved function rather
// than of anything the program wrote, so they are stated here once per name and
// not worked out again from the call.
void LowirUnitLowering::describe_builtin(
	const SemaEntity& entity, lowir_model::FunctionDeclaration& declaration)
{
	if (entity.builtin == kNotBuiltin)
	{
		return;
	}
	// 15.4p14 and 17.6.5.12: whether a call of one of them may unwind is 15.4p1
	// read of a declaration this unit made rather than of one the program
	// wrote, which is why it is stated where a declaration alone is written: a
	// program's own is stated over the body, because that is where the
	// exception-specification it wrote has to hold.  3.7.4.1p3 is what leaves
	// an allocation function free to throw where the rest are not.
	declaration.boundary.unwind = entity.nonthrowing ? lowir_model::CUM_NO
	                                                 : lowir_model::CUM_DEFAULT;
	switch (entity.builtin)
	{
	case kBuiltinOperatorNew:
	case kBuiltinOperatorNewArray:
	case kBuiltinOperatorDelete:
	case kBuiltinOperatorDeleteArray:
		// 3.7.4.1p2: what the storage a call of one of these obtained is worth
		// is nothing a caller may assume, so no further fact is stated of it.
		return;

	case kBuiltinMemcpy:
	case kBuiltinMemmove:
		// 17.6.5.6: the copy reads and writes the storage its two pointers
		// name and keeps neither past the call.  `memcpy` alone is given
		// storage the caller promises does not overlap.
		declaration.boundary.effects = lowir_model::CFXM_READWRITE;
		declaration.params[0].metadata.capture = lowir_model::PCM_NOCAPTURE;
		declaration.params[0].metadata.access =
			entity.builtin == kBuiltinMemcpy ? lowir_model::PAM_WRITE
			                                 : lowir_model::PAM_READWRITE;
		declaration.params[1].metadata.capture = lowir_model::PCM_NOCAPTURE;
		declaration.params[1].metadata.access = lowir_model::PAM_READ;
		if (entity.builtin == kBuiltinMemcpy)
		{
			declaration.params[0].metadata.alias = lowir_model::PALM_NOALIAS;
			declaration.params[1].metadata.alias = lowir_model::PALM_NOALIAS;
		}
		return;

	case kBuiltinStrlen:
		declaration.boundary.effects = lowir_model::CFXM_READONLY;
		declaration.params[0].metadata.capture = lowir_model::PCM_NOCAPTURE;
		declaration.params[0].metadata.access = lowir_model::PAM_READ;
		return;

	case kBuiltinUnreachable:
		// 1.9p4: control never reaches the call, so it reads nothing, writes
		// nothing and does not come back.
		declaration.boundary.effects = lowir_model::CFXM_READNONE;
		declaration.boundary.returns = lowir_model::CRM_NORETURN;
		return;

	default:
		return;
	}
}

void LowirUnitLowering::function_definition(const DumpNode& node)
{
	SemaEntity& entity = *node.fact.entity;
	const std::string symbol = function_symbol(entity);
	if (!builder_.emitted_functions_.insert(symbol).second)
	{
		return;
	}
	program_.functions.push_back(lowir_model::Function());
	const std::size_t at = program_.functions.size() - 1;
	lowir_model::Function& out = program_.functions.back();
	out.name = symbol;
	const TypeId type = node.fact.type;
	open_signature(types_.target(type), out.params, out.return_type);
	if (types_.variadic(type))
	{
		out.boundary.arity = lowir_model::CAM_VARIADIC;
	}
	// 15.4p1: the definition says the same of the function its declaration
	// does, so the boundary a caller reads and the one written over the body
	// are the one fact.
	if (entity.nonthrowing)
	{
		out.boundary.unwind = lowir_model::CUM_NO;
	}
	describe_symbol(entity, out.metadata, symbol);
	if (entity.special != kOrdinaryFunction &&
	    abi_variant(entity) == kCompleteObjectAbi && !writes_base_entry(entity))
	{
		// 12.1 and 12.4: the ABI gives a constructor and a destructor one entry
		// point for a complete object and one for a base subobject.  This
		// milestone has no virtual base, so the two do the same thing, and a
		// body only a complete object asked for is named twice rather than
		// emitted twice.  A body only a base subobject asked for stands under
		// the base-object name alone: nothing named the other, and a symbol
		// nothing asked for is one this unit does not owe the program.
		lowir_model::ObjectAlias alias;
		alias.object_symbol = abi_symbol_of(entity, types_, kBaseObjectAbi);
		alias.target = symbol;
		program_.object_aliases.push_back(alias);
	}
	if (entity.dump_name == "main")
	{
		// 3.6.1: `main` is where the program starts, which the backend needs to
		// know as a role rather than as a name it recognises.
		out.metadata.role = lowir_model::SR_ENTRY;
		out.metadata.keep_internal_alias = true;
	}
	LowirFunctionLowering body(*this, out);
	body.run(node, type);
	if (!writes_base_entry(entity))
	{
		return;
	}
	// 12.1 and 12.4: a base class subobject asked for this constructor or
	// destructor as well as a complete object did, so the object file has to
	// hold both of the ABI's entry points.  With no virtual base the two run the
	// same body, which is the one just lowered, under the base-object name.
	const std::string& base = function_symbol(entity, true);
	if (!builder_.emitted_functions_.insert(base).second)
	{
		return;
	}
	lowir_model::Function second = program_.functions[at];
	second.name = base;
	second.metadata = lowir_model::SymbolMetadata();
	describe_symbol(entity, second.metadata, base, true);
	program_.functions.push_back(second);
}
