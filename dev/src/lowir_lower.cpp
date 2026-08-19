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

}  // namespace

// The internal LowIR symbol of a namespace-scope name.  13.5's `operator+` and
// `operator-` are two functions of one arity and one parameter list, so one `_`
// per character rather than none is what keeps two names two symbols - and it
// is what the references write.
std::string flatten_symbol_name(const std::string& name)
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

std::string LowirSymbolTable::function_symbol(const SemaEntity& entity,
                                              const std::string& identity)
{
	const std::string base = flatten_symbol_name(abi_qualified_name(entity));
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
	return flatten_symbol_name(abi_qualified_name(entity));
}

LowirProgramBuilder::LowirProgramBuilder()
	: has_startup_(false)
	, startup_owed_(false)
	, has_shutdown_(false)
{}

LowirUnitLowering::LowirUnitLowering(TypeTable& types,
                                     LowirProgramBuilder& builder,
                                     const SourcePositionTable* positions,
                                     const AddressTable* addresses)
	: types_(types)
	, builder_(builder)
	, program_(builder.program_)
	, symbols_(builder.symbols_)
	, defined_(builder.defined_)
	, declared_(builder.declared_)
	, startup_(nullptr)
	, shutdown_(nullptr)
	, strings_(builder.strings_)
	, positions_(positions)
	, addresses_(addresses)
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
	if (types_.kind(types_.strip_cv(written)) == TypeKind::Array)
	{
		// 8.3.5p5 leaves an array no by-value form at all, so the one place
		// that declares such a parameter - 8.5.1p2's constructor of an
		// aggregate with an array member - carries the address of the caller's
		// array and copies the elements into the member itself.  4.2's
		// adjustment is the boundary's rather than the declaration's here,
		// which is what `decay` says.
		parameter.type = low("ptr");
		parameter.metadata.passing = lowir_model::PPM_DECAY;
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
		// 3.7.1p3: a block declares no name that reaches the object outside it,
		// so what the image calls its storage is not the flattening of a
		// qualified name the program could write.
		held = entity.local_static ? local_static_symbol(entity)
		                           : LowirSymbolTable::object_symbol(entity);
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
bool LowirUnitLowering::shared_definition(const SemaEntity& entity)
{
	return entity.inline_function || abi_instantiated(entity, types_);
}

bool LowirUnitLowering::writes_base_entry(const SemaEntity& entity)
{
	if (entity.special == kOrdinaryFunction)
	{
		return false;
	}
	if (entity.defined && (!shared_definition(entity) || entity.internal_linkage))
	{
		// 3.5p4 and 3.5p3: a definition with internal linkage is this unit's
		// alone for the same reason one written outside its class is - no other
		// unit may hold it - so the object file owes both of the ABI's names
		// whichever of them a use here happened to write.
		return true;
	}
	if (entity.defined && entity.base_object_entry &&
	    (entity.source_base_entry || entity.instantiated_use) &&
	    (abi_instantiated_class(entity, types_) ||
	     entity.out_of_class_definition))
	{
		// 14.7.1p1: what the use that made this instantiated definition asked
		// for is what the object file holds.  A base subobject the program
		// wrote out asks for the whole function, so both of the ABI's entry
		// points stand here; so does a use whose call 12.8p12 left out, because
		// what that use asked for was the definition and not any entry of it.
		// A base subobject written inside another instantiation asks for the
		// entry it names alone - the instantiation that wrote it is what owes
		// the rest - and a member defined in the body of a class the program
		// wrote out is 9.3p2's: the names it owes are the names this unit's own
		// code wrote.
		//
		// 14.5.2p1 asks it of the *class*: what one argument list makes of a
		// constructor template of a class the program itself wrote is the one
		// declaration the use named, and 12.1's other entry of it is a
		// declaration nothing here made - where the class is what a template-id
		// named, instantiating it is what makes its constructors and the unit
		// owes them whole.
		return true;
	}
	if (entity.defined && entity.base_object_entry && entity.constexpr_function &&
	    entity.user_provided && entity.primary == nullptr)
	{
		// 7.1.5p2 with 3.2p2: a constructor the program declared `constexpr` is
		// implicitly inline, and 5.19 may name it in any unit that reads its
		// class to build a *complete* object of it - a use no call in this one
		// spells.  So a unit that writes such a definition out at all owes both
		// of the ABI's entry points and not only the base-subobject one a call
		// here happened to name.  A constructor the standard declared or `=
		// default` left trivial writes nothing this unit's own code has not
		// already asked for.
		//
		// 14.7.1p1 leaves a *specialization* of a constructor template out of
		// it: a unit that names one in a constant expression instantiates the
		// declaration and the definition for itself, so no other unit's object
		// file is what it reads - which is the answer the arm above already
		// gives every other instantiated definition, and repeating it here owed
		// an entry point nothing names.
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
			entity, object_symbol_of(entity, kCompleteObjectAbi));
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
	//
	// 14.7.1p1 leaves an instantiated definition in exactly that position: the
	// template is what the program declared once, and every unit that names a
	// specialization writes the definition its own reading made - so two units
	// naming `Box<int>::get` each hold one, and neither owns it.  A member
	// defined *in* its class is already inline and would come out weak either
	// way; one defined outside it is not, and a function template's
	// specialization is not, which is why this is asked of the declaration
	// rather than left to the specifier it was written with.
	metadata.binding = entity.internal_linkage
		? lowir_model::SBM_INTERNAL
		: (shared_definition(entity) ? lowir_model::SBM_WEAK
		                             : lowir_model::SBM_STRONG);
	// 14.7.2p8 and 3.2p3: a definition every unit may hold is one the object
	// file writes only where a use asks for it, and an explicit instantiation
	// is the one declaration that asks with no use to point at - so the symbol
	// is a root of this object file however unreachable the rest of it leaves
	// it, which is a fact the backend needs and nothing in the body says.
	metadata.object_output_root = entity.explicitly_instantiated;
	// 7.5p1: the language linkage a backend needs is a fact about the
	// declaration, which no LowIR type says.
	if (entity.c_linkage)
	{
		metadata.linkage = lowir_model::LLM_C;
	}
	// 3.5p9: the object file names the entity, and PA14's encoder is what says
	// how.  The internal LowIR symbol is a spelling of this program alone, so
	// the object name is carried only where the two differ.
	const std::string& object =
		object_symbol_of(entity, base_entry ? kBaseObjectAbi : abi_variant(entity));
	if (object != symbol)
	{
		metadata.object_symbol = object;
	}
}

const std::string& LowirUnitLowering::object_symbol_of(const SemaEntity& entity,
                                                       unsigned variant)
{
	const std::uint64_t key =
		(static_cast<std::uint64_t>(entity.id) << 8) | variant;
	std::string& held = object_symbols_[key];
	if (held.empty())
	{
		held = abi_symbol_of(entity, types_, variant);
	}
	return held;
}

void LowirProgramBuilder::add_unit(const DumpNode& unit, TypeTable& types,
                                   const SourcePositionTable* positions,
                                   const AddressTable* addresses)
{
	LowirUnitLowering lowering(types, *this, positions, addresses);
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
		// 14.7.1p6 is the other: a definition an instantiation made owes the
		// entry only where something runs, so a body every one of whose
		// initializations is one of those is no body of this unit's.
		if (startup_owed_)
		{
			program_.functions.push_back(startup_);
		}
		has_startup_ = false;
		startup_owed_ = false;
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
	settle_vtable_names();
	settle_external_declarations();
}

// 3.2p1: a declaration of a name with external linkage says another unit has
// the definition, and a unit is read before the ones after it - so a unit that
// only used the name wrote that declaration whether or not a later unit turned
// out to define it here.  One entity is one top-level entry, so where the
// program has the definition the declaration in front of it is dropped.
void LowirProgramBuilder::settle_external_declarations()
{
	std::unordered_set<std::string> defined;
	for (std::size_t index = 0; index < program_.functions.size(); ++index)
	{
		defined.insert(program_.functions[index].name);
	}
	std::vector<lowir_model::FunctionDeclaration> functions;
	for (std::size_t index = 0; index < program_.function_declarations.size();
	     ++index)
	{
		if (defined.count(program_.function_declarations[index].name) == 0)
		{
			functions.push_back(program_.function_declarations[index]);
		}
	}
	program_.function_declarations.swap(functions);
	defined.clear();
	for (std::size_t index = 0; index < program_.globals.size(); ++index)
	{
		defined.insert(program_.globals[index].name);
	}
	std::vector<lowir_model::GlobalDeclaration> globals;
	for (std::size_t index = 0; index < program_.global_declarations.size();
	     ++index)
	{
		if (defined.count(program_.global_declarations[index].name) == 0)
		{
			globals.push_back(program_.global_declarations[index]);
		}
	}
	program_.global_declarations.swap(globals);
}

namespace
{

// One operand of an instruction, where it names a global the program renamed.
void rename_global(lowir_model::Operand& operand,
                   const std::unordered_map<std::string, std::string>& renamed)
{
	if (operand.kind != lowir_model::Operand::OP_GLOBAL)
	{
		return;
	}
	const std::unordered_map<std::string, std::string>::const_iterator found =
		renamed.find(operand.text);
	if (found != renamed.end())
	{
		operand.text = found->second;
	}
}

}  // namespace

// 3.2p3: a class's table is one object of the program, so the units that use it
// and the unit that owns it have to name one symbol.  A unit that does not hold
// the definition of the key function cannot know whether another one will, so it
// writes the name a table nobody defines has and records what it did; where a
// later unit turned out to own the table, the program has one name too many and
// this is where the two become one.
//
// A program whose units agree - which is every program written as a single
// translation unit - has nothing here to do, and the walk is not made at all.
void LowirProgramBuilder::settle_vtable_names()
{
	std::unordered_map<std::string, std::string> renamed;
	std::unordered_map<std::string, std::string>::const_iterator at =
		vtable_external_.begin();
	for (; at != vtable_external_.end(); ++at)
	{
		const std::unordered_map<std::string, std::string>::const_iterator owned =
			vtable_owned_.find(at->first);
		if (owned != vtable_owned_.end() && owned->second != at->second)
		{
			renamed[at->second] = owned->second;
		}
	}
	if (renamed.empty())
	{
		return;
	}
	// The declaration of a table the program defines is one declaration too
	// many, so it goes rather than being renamed onto the definition.
	std::vector<lowir_model::GlobalDeclaration> kept;
	for (std::size_t index = 0; index < program_.global_declarations.size();
	     ++index)
	{
		if (renamed.count(program_.global_declarations[index].name) == 0)
		{
			kept.push_back(program_.global_declarations[index]);
		}
	}
	program_.global_declarations.swap(kept);
	for (std::size_t index = 0; index < program_.globals.size(); ++index)
	{
		std::vector<lowir_model::GlobalDefinition::DataItem>& items =
			program_.globals[index].data_items;
		for (std::size_t item = 0; item < items.size(); ++item)
		{
			const std::unordered_map<std::string, std::string>::const_iterator
				found = renamed.find(items[item].symbol);
			if (found != renamed.end())
			{
				items[item].symbol = found->second;
			}
		}
	}
	for (std::size_t index = 0; index < program_.functions.size(); ++index)
	{
		std::vector<lowir_model::Block>& blocks = program_.functions[index].blocks;
		for (std::size_t block = 0; block < blocks.size(); ++block)
		{
			std::vector<lowir_model::Instruction>& written =
				blocks[block].instructions;
			for (std::size_t step = 0; step < written.size(); ++step)
			{
				lowir_model::Instruction& out = written[step];
				rename_global(out.first, renamed);
				rename_global(out.second, renamed);
				rename_global(out.third, renamed);
				for (std::size_t arg = 0; arg < out.args.size(); ++arg)
				{
					rename_global(out.args[arg], renamed);
				}
			}
		}
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
	// 12.4p8's body for an array is asked for while the definition that ends
	// one is being lowered, so it is written once no definition is left to ask.
	while (!pending_functions_.empty())
	{
		write_pending_functions();
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
		if (child.fact.entity != nullptr &&
		    (child.fact.kind == FactKind::FunctionDefinition ||
		     child.fact.kind == FactKind::Variable) &&
		    instantiation_is_suppressed(*child.fact.entity))
		{
			// 14.7.2p10: the reading that made this definition ran before the
			// declaration that says another unit holds it, so the line stands
			// in the dump and the object file writes neither it nor the symbol
			// it defines - a use of the name writes a declaration instead.
			deferred_[child.fact.entity->id] = &child;
			continue;
		}
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
			// 12.4, 5.3.5p3 and 9.3p2: the deleting entry is a second body over
			// this one definition, so a definition no other unit may hold is one
			// only this unit can write it from.  A table this unit emits asks
			// for it too, but the table is a fact of the class and the entry is
			// a fact of the definition: 10.4p2's pure slots name the runtime's
			// own function and ask for nothing, and a class whose key function
			// stands elsewhere has its table in the unit that holds that
			// function rather than in the one that holds this body.
			if (child.fact.entity->special == kDestructorFunction &&
			    child.fact.entity->virtual_function &&
			    !shared_definition(*child.fact.entity))
			{
				owe_deleting_entry(*child.fact.entity);
			}
			if (shared_definition(*child.fact.entity) &&
			    !child.fact.entity->explicitly_instantiated &&
			    !(child.fact.entity->friend_definition &&
			      child.fact.entity->internal_linkage) &&
			    !(child.fact.entity->out_of_class_definition &&
			      child.fact.entity->own_source_definition &&
			      !abi_instantiated(*child.fact.entity, types_)))
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
			//
			// 9.3p2, 3.2p4 and 2.2p1: neither does a member function this
			// unit's own source defined outside its class.  `inline` says every
			// unit that needs the definition may hold one, and 3.2p4 still
			// leaves this unit holding the one it was told to write - which is
			// what 12.8p12's copy of an object's bytes would otherwise unwrite,
			// by leaving no call of the member the program defined.  One read
			// from an included file is a definition every unit including that
			// file holds, so it waits for a use as a body written in the class
			// does; and a specialization's is not one the program wrote at all,
			// because 14.7.1p1 makes it the use that requires it.
		}
		else if (child.fact.kind == FactKind::Variable &&
		         child.fact.entity != nullptr &&
		         child.fact.entity->object_definition)
		{
			defined_.insert(global_symbol(*child.fact.entity));
			const SemaEntity& object = *child.fact.entity;
			if (object.instantiated_definition && !object.thread_storage &&
			    !object.definition_required && object.region != nullptr &&
			    object.region->kind == ScopeKind::Class)
			{
				// 14.7.1p1 and 3.2p3: the storage a static data member of a
				// class template specialization stands in is laid out by a
				// definition no unit wrote for these arguments, so it belongs
				// to the program where the program reaches it - exactly as the
				// body of a member function of the same specialization does.
				// A member 9.4.2p3 makes a constant is then storage no unit
				// holds until one reads it as an object, and an object of the
				// class is what has already asked (`definition_required`).
				deferred_[object.id] = &child;
			}
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
		if (node.fact.entity != nullptr &&
		    deferred_.find(node.fact.entity->id) != deferred_.end())
		{
			// 14.7.1p1: the storage an instantiation would lay out waits for
			// the use that reaches it, which `collect_definitions` recorded it
			// against.
			return;
		}
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
	const DumpNode* const dynamic = global_image(global, node, type);
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
	const DumpNode* const destruction = declared_destruction(node);
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
const DumpNode* LowirUnitLowering::declared_destruction(const DumpNode& node)
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
	// 14.7.1p1 and 14.7.1p6: the definition of a static data member an
	// instantiation made is the template's rather than this unit's, and the
	// initialization it carries is one the use that names the member asks for -
	// so what this unit owes the program before it starts is what that
	// initialization *runs*, and an initialization that writes nothing leaves
	// this unit owing no body at all.  A definition the program itself wrote
	// owes 3.6.2p2's entry however little it does.
	const std::pair<std::size_t, std::size_t> written = startup_mark();
	startup_->add_initialization(storage, type, node);
	if (!abi_instantiated(entity, types_) || startup_mark() != written)
	{
		builder_.startup_owed_ = true;
	}
}

// Where the startup body stands, which is what says whether an initialization
// added to it came to anything.
//
// The body only ever grows, so the block it is open at and how much that block
// holds are the whole of the answer - which costs the same whatever the body
// has already accumulated.
std::pair<std::size_t, std::size_t> LowirUnitLowering::startup_mark() const
{
	const std::size_t blocks = builder_.startup_.blocks.size();
	return std::make_pair(blocks,
	                      blocks == 0
	                          ? 0
	                          : builder_.startup_.blocks.back().instructions.size());
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
	// 3.6.3p1 and 3.6.2p2: the end of the lifetime of an object with static
	// storage duration is registered where the program starts, so a unit that
	// owes the program that end owes it the entry the registration stands in -
	// however little the initialization beside it came to.
	builder_.startup_owed_ = true;
	shutdown_->add_destruction(node);
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
void LowirUnitLowering::demand_referenced(const DumpNode& node, bool running,
                                          bool befriended)
{
	if (node.fact.kind == FactKind::FunctionDefinition &&
	    node.fact.entity != nullptr && !node.fact.entity->friend_definition &&
	    deferred_.find(node.fact.entity->id) != deferred_.end())
	{
		return;
	}
	befriended = befriended ||
		(node.fact.kind == FactKind::FunctionDefinition &&
		 node.fact.entity != nullptr && node.fact.entity->friend_definition);
	if (node.fact.kind == FactKind::Variable && node.fact.entity != nullptr &&
	    node.fact.entity->constant && valued_type(node.fact.type) &&
	    (!running || node.fact.entity->local_static))
	{
		// 3.6.2p2 again, asked of the *object* rather than of one line of its
		// initializer.  Where the storage of an object of static duration holds
		// the constant the analysis folded, the whole initializer is a value the
		// image writes and nothing in it is run - so no name written there is a
		// use of anything.  The rule below is this same one for a line that
		// carries the fold on itself; this is it for the lines that do not, and
		// for the object those lines belonged to: a member of an object a call
		// handed back, an element of an array, what 12.3.2p1's conversion
		// function of a class prvalue returns.  An object of class or array type
		// is left out: its image is what running its constructor comes to, and
		// 3.2p2 makes that a use of the constructor.
		return;
	}
	if (node.fact.kind == FactKind::Call && node.fact.constant && !running)
	{
		// 3.2p2 with 5.19p2 and 3.6.2p2: an initializer written outside every
		// body is one the program image holds, and a call 5.19 folded there is
		// a value the image writes rather than a call anything makes - so the
		// function it would have run is named nowhere.  A folded call written
		// *inside* a body is a call the program still makes, so the walk reads
		// it there as it reads any other.
		return;
	}
	if (node.fact.entity != nullptr &&
	    node.fact.entity->kind == SemaKind::Function &&
	    (node.fact.entity->special == kOrdinaryFunction ||
	     (befriended && node.fact.kind == FactKind::Callee &&
	      node.fact.entity->special == kConstructorFunction)) &&
	    (node.fact.kind == FactKind::Callee || node.fact.kind == FactKind::Id))
	{
		// 3.2p2 and 11.3p6: a friend defined inside a class is a definition of
		// the namespace it belongs to that no declaration there wrote, so the
		// walk reads it for uses whether or not the definition itself is one -
		// and 12.1p1 gives a constructor no name a program can write, so a call
		// of one is the whole of what such a body says about it.  Every other
		// body is read only where this unit writes it, and the constructor a
		// call of it names is asked for as that call is lowered.
		referenced_.push_back(node.fact.entity->id);
	}
	// 3.7.1p3 and 3.6.2p1: an object a block declares `static` has the storage
	// duration of the program, so an initializer of it that 5.19 folded is a
	// value the image holds exactly as a namespace-scope object's is - the
	// fold is what the storage starts as, and the function it would have run is
	// named nowhere.  An object of class type is left inside: 12.1p5 makes its
	// initialization a call whatever its clauses came to, so a call written
	// among them is one the program still makes.
	const bool image =
		node.fact.kind == FactKind::Variable && node.fact.entity != nullptr &&
		node.fact.entity->local_static &&
		!types_.is_class(types_.element_of(types_.strip_cv(node.fact.type)));
	const bool inside =
		(running || node.fact.kind == FactKind::FunctionDefinition) && !image;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		demand_referenced(*node.children[index], inside, befriended);
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
	// 12.1p11: constructing an object of a polymorphic class writes the
	// vpointer of the class whose constructor is running, whatever its body
	// comes to - so a call of one is never a call that does nothing.
	bool nothing = found != bodies_.end() &&
		!(constructor.region != nullptr &&
		  constructor.region->owner != nullptr &&
		  constructor.region->owner->polymorphic);
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

// 3.2p2 and 8.4.2p1: the definition an initialization the *image* holds owes.
//
// The reading above is about a call a body left out, where nothing anywhere read
// the constructor's definition - so 3.5p4's narrower question is the right one
// there, and a definition every unit may hold is nobody's to owe.  An image is
// not always that.  `read` says the definition 8.4.2p1 gives this constructor was
// gone through to work the image out: the lowering walks it wherever the storage
// is the member initializations it writes, and 5.19 walks it wherever the
// analysis folded the object to a value.  A definition this unit went through is
// one its object file holds, however little of the constructor's work the image
// then kept.
//
// One the standard declared as well as defined is named by nothing the program
// wrote and stays unwritten either way, which is what keeps `struct B { int x;
// }; constexpr B b = B();` storage and nothing else, and 8.5.1's own entry for
// an aggregate with it.
void LowirUnitLowering::owe_folded_construction(const SemaEntity& constructor,
                                                bool read)
{
	if (!read || constructor.implicit_declaration || constructor.member_entry)
	{
		owe_internal_definition(constructor);
		return;
	}
	demand_definition(constructor);
}

void LowirUnitLowering::owe_internal_definition(const SemaEntity& entity)
{
	if (entity.internal_linkage || entity.instantiated_use)
	{
		// 14.7.1p1: a use inside a body an instantiation made is what made this
		// definition, so the unit holds it whether or not the call it stood for
		// was written - which is the same reason 3.5p4's internal linkage keeps
		// one: a definition this unit made is one no other unit will make.
		//
		// A definition the program wrote outside its class never waited for a
		// use at all - `collect_definitions` reads 9.3p2 of it where the unit's
		// definitions are gathered - so there is nothing for an elided call to
		// ask of it here.
		demand_definition(entity);
	}
}

// 12.4 and 5.3.5p3: this unit writes the destructor's deleting entry, once
// however many things ask for it - a table it emits naming the slot, and the
// definition it holds of a destructor no other unit may define.
void LowirUnitLowering::owe_deleting_entry(const SemaEntity& entity)
{
	if (deleting_entries_.insert(entity.id).second)
	{
		deleting_owed_.push_back(&entity);
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
	if (found->second->fact.entity != nullptr &&
	    instantiation_is_suppressed(*found->second->fact.entity))
	{
		// 14.7.2p10: the use asking for the definition is exactly what p9's
		// declaration answered - another unit holds it - so the demand leaves
		// the line where it is and the name goes out as a declaration.
		return;
	}
	demanded_.push_back(found->second);
	deferred_.erase(found);
}

void LowirUnitLowering::drain_demanded()
{
	while (!demanded_.empty() || !deleting_owed_.empty())
	{
		if (!demanded_.empty())
		{
			const DumpNode& node = *demanded_.back();
			demanded_.pop_back();
			if (node.fact.kind == FactKind::Variable)
			{
				global_variable(node);
			}
			else
			{
				function_definition(node);
			}
			continue;
		}
		// 12.4 and 5.3.5p3: a table named the ABI's third entry point over a
		// destructor's definition - the one that runs it on a complete object
		// and then gives the storage back.  No call in the program names it, so
		// the table is the only thing that asks, and it asks here rather than
		// beside the definition because the two can be written in either order.
		const SemaEntity& destructor = *deleting_owed_.back();
		deleting_owed_.pop_back();
		deleting_definition(destructor);
	}
}

// 12.4 and 5.3.5p3: the deleting entry, which is the destructor's own
// definition with one more step in 12.4p8's suffix.  It is a second body over
// one definition rather than a second name for one body, so it is lowered
// again; a destructor this unit does not define leaves the entry to the unit
// that does, and the object file names it.
void LowirUnitLowering::deleting_definition(const SemaEntity& entity)
{
	const std::string symbol = function_symbol(entity) + "__deleting_entry";
	const std::unordered_map<std::uint32_t, const DumpNode*>::const_iterator
		found = bodies_.find(entity.id);
	if (found == bodies_.end())
	{
		if (declared_.insert(symbol).second)
		{
			lowir_model::FunctionDeclaration declaration;
			declaration.name = symbol;
			lowir_model::Parameter self;
			self.name = "arg0";
			self.type.text = "ptr";
			declaration.params.push_back(self);
			declaration.return_type.text = "void";
			describe_symbol(entity, declaration.metadata, symbol);
			declaration.metadata.object_symbol =
				abi_symbol_of(entity, types_, kDeletingObjectAbi);
			program_.function_declarations.push_back(declaration);
		}
		return;
	}
	if (!builder_.emitted_functions_.insert(symbol).second)
	{
		return;
	}
	const DumpNode& node = *found->second;
	program_.functions.push_back(lowir_model::Function());
	lowir_model::Function& out = program_.functions.back();
	out.name = symbol;
	const TypeId type = node.fact.type;
	open_signature(types_.target(type), out.params, out.return_type);
	if (entity.nonthrowing)
	{
		out.boundary.unwind = lowir_model::CUM_NO;
	}
	describe_symbol(entity, out.metadata, symbol);
	out.metadata.object_symbol =
		abi_symbol_of(entity, types_, kDeletingObjectAbi);
	LowirFunctionLowering body(*this, out);
	body.run(node, type, kDeletingObjectAbi);
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

void LowirUnitLowering::declare_entity(const SemaEntity& entity, bool used)
{
	if (entity.kind == SemaKind::Function)
	{
		demand_definition(entity);
		add_function_declaration(entity);
		return;
	}
	// 3.2p3: a use is what asks the program for the storage an instantiation
	// lays out, so naming the object here is what writes its definition - and
	// 9.4.2p3's member named for its value alone is the one naming that is no
	// use, which asks when the place itself is read and not before.
	if (used)
	{
		demand_definition(entity);
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

	case kBuiltinExpect:
		// 1.4p8: the hint is worth its first operand and reads nothing at all -
		// the declaration is written only where something names the function
		// without calling it, because a call of it crosses no boundary.
		declaration.boundary.effects = lowir_model::CFXM_READNONE;
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
	if (entity.region != nullptr && entity.region->owner != nullptr &&
	    entity.region->owner->key_function == &entity)
	{
		// 3.2p3 and the ABI: this unit holds the one definition of the class's
		// key function, so it is the unit that owes the program the class's
		// table - whether or not anything here creates an object of the class.
		vtable_symbol(*entity.region->owner);
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
