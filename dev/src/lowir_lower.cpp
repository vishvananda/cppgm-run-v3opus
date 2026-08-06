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
                                              const std::string& signature)
{
	const std::string base = flatten_name(entity.dump_name);
	std::unordered_map<std::string, std::size_t>& seen = overloads_[base];
	// 13.1 lets one name have as many declarations as the program writes, so
	// which of them a signature is, is a probe rather than a walk of the ones
	// already named.
	const std::pair<std::unordered_map<std::string, std::size_t>::iterator, bool>
		found = seen.insert(std::make_pair(signature, seen.size()));
	const std::size_t index = found.first->second;
	return index == 0 ? base : base + "__ov" + decimal(index + 1);
}

std::string LowirSymbolTable::object_symbol(const SemaEntity& entity)
{
	return flatten_name(entity.dump_name);
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
	case FT_NULLPTR_T: return low("ptr");
	default: break;
	}
	// 3.9.1p2: every remaining integral type is one of the two 8 byte ones, and
	// LowIR spells both as `i64`; the operator says which is unsigned.
	return low("i64");
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

const std::string& LowirUnitLowering::function_symbol(const SemaEntity& entity)
{
	std::string& held = entity_symbols_[entity.id];
	if (held.empty())
	{
		held = symbols_.function_symbol(entity, types_.description(entity.type));
	}
	return held;
}

void LowirUnitLowering::describe_symbol(const SemaEntity& entity,
                                        lowir_model::SymbolMetadata& metadata,
                                        const std::string& symbol)
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
	const std::string object = abi_symbol_of(entity, types_);
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
	for (std::size_t index = 0; index < unit.children.size(); ++index)
	{
		declaration(*unit.children[index]);
		drain_demanded();
	}
	drain_demanded();
	if (startup_ != nullptr)
	{
		startup_->suspend_generated(builder_.startup_body_);
	}
	if (shutdown_ != nullptr)
	{
		shutdown_->suspend_generated(builder_.shutdown_body_);
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
			if (child.fact.entity->inline_function)
			{
				// 7.1.2p4: the definition is the program's rather than this
				// unit's, and 3.2p3 puts it in the program only where a use
				// asks for it.
				deferred_[child.fact.entity->id] = &child;
			}
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
		if (node.fact.entity != nullptr && node.fact.entity->inline_function)
		{
			// 7.1.2p4: the definition waits for a use of it, which
			// `collect_definitions` has already recorded it against.
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

// 5.19 over the resolved tree.  Every operand a namespace-scope initializer of
// the PA15 subset is written from is a value the analysis already knows or an
// operator over ones that are, so the fold is one walk of what is written and
// asks the syntax nothing.
bool LowirUnitLowering::folded(const DumpNode& node, unsigned long long& bits)
{
	const SemaFact& fact = node.fact;
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
	if (!entity.object_definition)
	{
		declare_entity(entity);
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
		dynamic = written;
		written = nullptr;
	}
	else if (types_.kind(types_.strip_cv(type)) == TypeKind::Array ||
	         types_.kind(types_.strip_cv(type)) == TypeKind::Class)
	{
		global.structured = true;
		global_array_initializer(global, written, type);
	}
	else if (written != nullptr && !global_initializer(global, *written, type))
	{
		// 3.6.2p2: an object whose initializer is not a constant starts as
		// zero and is given its value before the program runs.
		global.init_kind = lowir_model::GlobalDefinition::INIT_ZERO;
		dynamic = written;
	}
	program_.globals.push_back(global);
	if (dynamic != nullptr)
	{
		dynamic_initializer(entity, *dynamic, type);
	}
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
		add_zero_item(global, offset - at);
		lowir_model::GlobalDefinition::DataItem item;
		item.type = low_type(child.fact.type);
		if (child.children.empty())
		{
			item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
			item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
			item.literal_operand.text = "0";
		}
		else
		{
			std::string symbol;
			long long addend = 0;
			unsigned long long bits = 0;
			if (global_address(*child.children[0], symbol, addend))
			{
				item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ADDR;
				item.symbol = symbol;
				item.addr_addend = addend;
			}
			else if (folded(*child.children[0], bits))
			{
				item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
				item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
				item.literal_operand.text = spell_value(child.fact.type, bits);
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
	unsigned long long bits = 0;
	if (!folded(node, bits))
	{
		return false;
	}
	global.init_kind = lowir_model::GlobalDefinition::INIT_INTEGER;
	global.init_operand.kind = lowir_model::Operand::OP_INTEGER;
	global.init_operand.text = spell_value(type, bits);
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

void LowirUnitLowering::global_array_initializer(
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
	for (std::size_t index = 0; index < clauses; ++index)
	{
		if (aggregate)
		{
			// 8.5.1p3: an element that is itself an aggregate holds what its own
			// list says, so its items are this one's items in the same order.
			global_array_initializer(global, node->children[index], element);
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
		unsigned long long bits = 0;
		if (!folded(*node->children[index], bits))
		{
			throw std::runtime_error("an array element is initialized by an "
			                         "expression PA15 does not lower");
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
		item.literal_operand.text = spell_value(element, bits);
		global.data_items.push_back(item);
	}
	const unsigned long long left = (bound - clauses) * stride;
	if (left == 0)
	{
		return;
	}
	lowir_model::GlobalDefinition::DataItem zero;
	zero.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ZERO;
	zero.zero_bytes = static_cast<std::size_t>(left);
	global.data_items.push_back(zero);
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

void LowirUnitLowering::demand_definition(const SemaEntity& entity)
{
	const std::unordered_map<std::uint32_t, const DumpNode*>::iterator found =
		deferred_.find(entity.id);
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
	program_.global_declarations.push_back(declaration);
}

void LowirUnitLowering::add_function_declaration(const SemaEntity& entity)
{
	const std::string symbol = function_symbol(entity);
	if (defined_.count(symbol) != 0 || !declared_.insert(symbol).second)
	{
		return;
	}
	lowir_model::FunctionDeclaration declaration;
	declaration.name = symbol;
	const std::vector<TypeId>& parameters = types_.parameters(entity.type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		lowir_model::Parameter parameter;
		parameter.name = "arg" + decimal(index);
		parameter.type = low_type(parameters[index]);
		if (types_.is_reference(parameters[index]))
		{
			parameter.metadata.passing = lowir_model::PPM_REFERENCE;
		}
		declaration.params.push_back(parameter);
	}
	declaration.return_type = low_type(types_.target(entity.type));
	if (types_.variadic(entity.type))
	{
		declaration.boundary.arity = lowir_model::CAM_VARIADIC;
	}
	describe_symbol(entity, declaration.metadata, symbol);
	program_.function_declarations.push_back(declaration);
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
	lowir_model::Function& out = program_.functions.back();
	out.name = symbol;
	const TypeId type = node.fact.type;
	out.return_type = low_type(types_.target(type));
	if (types_.variadic(type))
	{
		out.boundary.arity = lowir_model::CAM_VARIADIC;
	}
	describe_symbol(entity, out.metadata, symbol);
	if (entity.special != kOrdinaryFunction)
	{
		// 12.1 and 12.4: the ABI gives a constructor and a destructor one entry
		// point for a complete object and one for a base subobject.  This
		// milestone has no virtual base, so the two do the same thing, and the
		// program names one body twice rather than emitting it twice.
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
}
