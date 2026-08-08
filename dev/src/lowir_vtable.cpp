#include "lowir_lower.h"

#include <sstream>

#include "lowir_abi.h"
#include "sema_scope.h"

// 10.3p10, 10.4p2 and 5.2.8: the data a polymorphic class owns.
//
// A class that dispatches holds one table of its final overriders and one
// record naming its type, and every object of it holds a pointer to the first.
// None of the three is a declaration a program wrote, so all of them are named
// from the class alone - and all of them are written here, on demand, so a
// program that creates no object of a polymorphic class holds no table at all.

namespace
{

typedef lowir_model::GlobalDefinition::DataItem DataItem;

std::string decimal(unsigned long long value)
{
	std::ostringstream out;
	out << value;
	return out.str();
}

lowir_model::LowType low(const char* text)
{
	lowir_model::LowType type;
	type.text = text;
	return type;
}

// One `ptr addr @symbol + addend` of a structured global.
DataItem address_item(const std::string& symbol, long long addend = 0)
{
	DataItem item;
	item.kind = DataItem::ITEM_ADDR;
	item.type = low("ptr");
	item.symbol = symbol;
	item.addr_addend = addend;
	return item;
}

DataItem integer_item(const char* width, unsigned long long value)
{
	DataItem item;
	item.kind = DataItem::ITEM_INTEGER;
	item.type = low(width);
	item.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
	item.literal_operand.text = decimal(value);
	return item;
}

// The ABI's three type-information classes, by how much a record has to say
// about where the bases of the type stand.
const char* const kRttiClassNames[] = {
	"__class_type_info",
	"__si_class_type_info",
	"__vmi_class_type_info",
};
const char* const kRttiClassObjects[] = {
	"_ZTVN10__cxxabiv117__class_type_infoE",
	"_ZTVN10__cxxabiv120__si_class_type_infoE",
	"_ZTVN10__cxxabiv121__vmi_class_type_infoE",
};

}  // namespace

// 3.4.3p1: a class is named from outside every region enclosing it, so a name
// reaches it only where every one of those regions is a namespace or a class.
// One a function declares is reached by no such name at all, and two functions
// may each declare one of the same spelling.
bool LowirUnitLowering::named_from_namespace_scope(const SemaEntity& owner)
{
	for (const Scope* at = owner.region; at != nullptr; at = at->parent)
	{
		if (at->kind == ScopeKind::Function || at->kind == ScopeKind::Block)
		{
			return false;
		}
	}
	return true;
}

std::string LowirUnitLowering::class_tag(const SemaEntity& owner)
{
	if (!named_from_namespace_scope(owner))
	{
		// The encoding of the type is the one spelling that tells two classes
		// of one name apart, because it is what the object file already names
		// them by.
		return "type_" + abi_type_name_of(owner.type, types_);
	}
	const char* key = "struct";
	switch (types_.class_tag(owner.type))
	{
	case ClassTag::Class: key = "class"; break;
	case ClassTag::Union: key = "union"; break;
	default: break;
	}
	return std::string(key) + "_" +
		flatten_symbol_name(types_.user_qualified_name(owner.type));
}

const std::string& LowirUnitLowering::external_rtti_vtable(unsigned bases)
{
	static const std::string kNames[] = {
		std::string("__external_rtti_vtable__") + kRttiClassNames[0],
		std::string("__external_rtti_vtable__") + kRttiClassNames[1],
		std::string("__external_rtti_vtable__") + kRttiClassNames[2],
	};
	const std::string& symbol = kNames[bases];
	if (declared_.insert(symbol).second)
	{
		lowir_model::GlobalDeclaration declaration;
		declaration.name = symbol;
		declaration.metadata.binding = lowir_model::SBM_STRONG;
		declaration.metadata.object_symbol = kRttiClassObjects[bases];
		program_.global_declarations.push_back(declaration);
	}
	return symbol;
}

// 5.2.8p14 and the ABI: the record that says what type an object is, as the
// type-information class it belongs to, the string naming the type, and - for a
// class with a base - what that base is and where its subobject stands.
//
// It is asked of the base too, and of the base's base, so one class's record
// costs the derivation once however many classes below it name it.
const std::string& LowirUnitLowering::rtti_symbol(const SemaEntity& owner)
{
	{
		const std::string& held = rtti_symbols_[owner.id];
		if (!held.empty())
		{
			return held;
		}
	}
	const std::string tag = class_tag(owner);
	// The name is recorded before the base's record is asked for, because that
	// question walks the derivation and this map grows under it.
	rtti_symbols_[owner.id] = "__rtti_" + tag;
	// 3.4.3p1: a class a name reaches is spelled by that name, which the
	// references write with one more separator here than in the record above.
	const std::string named = "__typeinfo_name_" +
		(named_from_namespace_scope(owner) ? "_" + tag : tag);
	const std::string spelled = abi_type_name_of(owner.type, types_);
	lowir_model::GlobalDefinition string;
	string.name = named;
	string.structured = true;
	string.storage = lowir_model::GSM_READONLY;
	string.metadata.binding = lowir_model::SBM_WEAK;
	string.metadata.object_symbol =
		abi_typeinfo_name_symbol_of(owner.type, types_);
	for (std::size_t index = 0; index <= spelled.size(); ++index)
	{
		string.data_items.push_back(integer_item(
			"i8", index < spelled.size()
			          ? static_cast<unsigned char>(spelled[index])
			          : 0));
	}
	if (builder_.emitted_globals_.insert(named).second)
	{
		program_.globals.push_back(string);
	}
	// 10p1: the record names the base before this class's own record can be
	// written, so the base is asked first and the two stand in derivation order.
	const std::string base_record =
		owner.base != nullptr ? rtti_symbol(*owner.base) : std::string();
	const bool ordinary = owner.base != nullptr && owner.base_offset == 0 &&
		owner.base_access == kPublicAccess;
	const unsigned kind = owner.base == nullptr ? 0u : (ordinary ? 1u : 2u);
	lowir_model::GlobalDefinition record;
	record.name = rtti_symbols_[owner.id];
	record.structured = true;
	record.storage = lowir_model::GSM_READONLY;
	record.metadata.binding = lowir_model::SBM_WEAK;
	record.metadata.object_symbol = abi_typeinfo_symbol_of(owner.type, types_);
	record.data_items.push_back(
		address_item(external_rtti_vtable(kind),
		             static_cast<long long>(kVtablePrefixBytes)));
	record.data_items.push_back(address_item(named));
	if (kind == 2)
	{
		// The ABI's record for a base that is neither public nor at the start
		// of the object: no diamond and no repeated base, one base, and the
		// byte it begins at together with 11.2's access written as flags.
		record.data_items.push_back(integer_item("i32", 0));
		record.data_items.push_back(integer_item("i32", 1));
		record.data_items.push_back(address_item(base_record));
		record.data_items.push_back(integer_item(
			"i64", (owner.base_offset << 8) |
			           (owner.base_access == kPublicAccess ? 2u : 0u)));
	}
	else if (kind == 1)
	{
		record.data_items.push_back(address_item(base_record));
	}
	if (builder_.emitted_globals_.insert(record.name).second)
	{
		program_.globals.push_back(record);
	}
	return rtti_symbols_[owner.id];
}

// 10.4p2 and the ABI: a slot whose final overrider is a declaration the program
// left pure holds the runtime's own function, whose call is the one thing a
// program that reaches such a slot may be said to have done.  One declaration
// stands for every such slot of the unit, so it is signed from the first.
const std::string& LowirUnitLowering::pure_virtual_symbol(
	const SemaEntity& member)
{
	static const std::string kSymbol = "__cxa_pure_virtual";
	if (!pure_virtual_.empty())
	{
		return pure_virtual_;
	}
	pure_virtual_ = kSymbol;
	lowir_model::FunctionDeclaration declaration;
	declaration.name = kSymbol;
	open_signature(types_.target(member.type), declaration.params,
	               declaration.return_type);
	const std::vector<TypeId>& parameters = types_.parameters(member.type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		lowir_model::Parameter parameter;
		parameter.name = "arg" + decimal(index);
		describe_parameter(parameters[index], parameter);
		declaration.params.push_back(parameter);
	}
	declaration.boundary.effects = lowir_model::CFXM_READNONE;
	declaration.boundary.unwind = lowir_model::CUM_NO;
	declaration.boundary.returns = lowir_model::CRM_NORETURN;
	declaration.metadata.binding = lowir_model::SBM_STRONG;
	program_.function_declarations.push_back(declaration);
	return pure_virtual_;
}

// 10.3p10: the final overriders of the class in ABI slot order, which the
// analysis already settled.  Naming one is a use of it: 3.2p3 puts the
// definition of an inline function in the program only where a use asks, and
// the table is that use however little the unit's own code calls.
void LowirUnitLowering::write_vtable_slots(const SemaEntity& owner,
                                           lowir_model::GlobalDefinition& into)
{
	for (std::size_t slot = 0; slot < owner.vtable.size(); ++slot)
	{
		SemaEntity* const at = owner.vtable[slot].overrider;
		if (at == nullptr)
		{
			continue;
		}
		if (at->pure_virtual)
		{
			into.data_items.push_back(address_item(pure_virtual_symbol(*at)));
			continue;
		}
		if (owner.vtable[slot].deleting)
		{
			// 12.4 and 5.3.5p3: the entry that runs the destructor and then
			// gives the storage back, which no call in the program names
			// directly - so the table is what asks this unit to write it.
			if (deleting_entries_.insert(at->id).second)
			{
				deleting_owed_.push_back(at);
			}
			declare_call_target(*at, false);
			into.data_items.push_back(
				address_item(function_symbol(*at) + "__deleting_entry"));
			continue;
		}
		declare_call_target(*at, false);
		into.data_items.push_back(address_item(function_symbol(*at)));
	}
}

// 3.2p3 and the ABI: which unit holds the table.
//
// The key function is the answer: a class that declares a virtual member that
// is neither pure nor inline has that member defined in exactly one unit, and
// that unit is the one that holds the table.  A class with no such member has
// its table wherever one is needed and the linker keeps one.
const std::string& LowirUnitLowering::vtable_symbol(const SemaEntity& owner)
{
	{
		const std::string& held = vtable_symbols_[owner.id];
		if (!held.empty())
		{
			return held;
		}
	}
	const bool named = named_from_namespace_scope(owner);
	const std::string tag = class_tag(owner);
	const std::string flat =
		flatten_symbol_name(types_.user_qualified_name(owner.type));
	const bool elsewhere = owner.key_function != nullptr &&
		bodies_.count(owner.key_function->id) == 0;
	if (elsewhere)
	{
		const std::string external =
			named ? "__external_vtable__" + flat : "__external_vtable_" + tag;
		vtable_symbols_[owner.id] = external;
		if (declared_.insert(external).second)
		{
			lowir_model::GlobalDeclaration declaration;
			declaration.name = external;
			declaration.metadata.binding = lowir_model::SBM_STRONG;
			declaration.metadata.object_symbol =
				abi_vtable_symbol_of(owner.type, types_);
			program_.global_declarations.push_back(declaration);
		}
		return vtable_symbols_[owner.id];
	}
	vtable_symbols_[owner.id] = named ? flat + "__vtable" : "__vtable_" + tag;
	lowir_model::GlobalDefinition table;
	table.name = vtable_symbols_[owner.id];
	table.structured = true;
	table.storage = lowir_model::GSM_READONLY;
	// 3.2p3: a table this unit alone defines is the program's one definition of
	// it; one every unit that needs the class may hold is nobody's, and the
	// linker keeps whichever it saw.
	table.metadata.binding = owner.key_function != nullptr
		? lowir_model::SBM_STRONG
		: lowir_model::SBM_WEAK;
	table.metadata.object_symbol = abi_vtable_symbol_of(owner.type, types_);
	table.data_items.push_back(integer_item("i64", 0));
	table.data_items.push_back(address_item(rtti_symbol(owner)));
	write_vtable_slots(owner, table);
	if (builder_.emitted_globals_.insert(table.name).second)
	{
		program_.globals.push_back(table);
	}
	return vtable_symbols_[owner.id];
}
