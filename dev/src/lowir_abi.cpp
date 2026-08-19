#include "lowir_abi.h"

#include <deque>
#include <map>
#include <sstream>

#include "abi_mangle.h"
#include "sema_pack.h"
#include "sema_scope.h"
#include "token_model.h"

// The typed ABI facts of one resolved declaration.
//
// Every fact here comes from the analysis: the regions around the declaration,
// the specifiers it was written with, and the type it declared.  Nothing is
// read out of syntax and nothing is spelled by hand - the words below name the
// encoder's own vocabulary, and the encoder writes the name.

namespace {

using abi_mangle::AbiType;

std::string decimal(unsigned long long value)
{
	std::ostringstream out;
	out << value;
	return out.str();
}

// 3.9.1: the word PA14's encoder knows each fundamental type by.
const char* fundamental_word(EFundamentalType type)
{
	switch (type)
	{
	case FT_VOID: return "void";
	case FT_BOOL: return "bool";
	case FT_CHAR: return "char";
	case FT_SIGNED_CHAR: return "schar";
	case FT_UNSIGNED_CHAR: return "uchar";
	case FT_SHORT_INT: return "short";
	case FT_UNSIGNED_SHORT_INT: return "ushort";
	case FT_INT: return "int";
	case FT_UNSIGNED_INT: return "uint";
	case FT_LONG_INT: return "long";
	case FT_UNSIGNED_LONG_INT: return "ulong";
	case FT_LONG_LONG_INT: return "longlong";
	case FT_UNSIGNED_LONG_LONG_INT: return "ulonglong";
	case FT_WCHAR_T: return "wchar_t";
	case FT_CHAR16_T: return "char16_t";
	case FT_CHAR32_T: return "char32_t";
	case FT_FLOAT: return "float";
	case FT_DOUBLE: return "double";
	case FT_LONG_DOUBLE: return "longdouble";
	case FT_NULLPTR_T: return "nullptr";
	default: break;
	}
	return "void";
}

AbiType builtin(const char* word)
{
	AbiType type;
	type.kind = abi_mangle::ABI_TYPE_BUILTIN;
	type.name = word;
	return type;
}

// `<source-name>`: an identifier written after the count of its characters,
// which is how every component of an encoded name is spelled.
std::string source_name(const std::string& written)
{
	return decimal(written.size()) + written;
}

class LocalContexts;

AbiType abi_type(TypeTable& types, TypeId type, LocalContexts& contexts);
std::vector<const SemaEntity*> owning_classes(const SemaEntity& entity);

// The `<local-name>` contexts one encoded name reads.
//
// 9.8p1's function is what a local entity is named from, and the encoder is
// handed that function's own encoding rather than a spelling of it, so that the
// substitutions the context registers are the ones the components after it
// name.  Each function is described once however many components of one name
// stand under it, and a name that mentions none of them builds nothing at all.
class LocalContexts
{
public:
	explicit LocalContexts(TypeTable& types) : types_(types) {}

	// The identifier the encoder reads `function`'s encoding back under.
	const std::string& context_of(const SemaEntity& function);

	// 14.2 and `<template-args>`: the identifier the encoder reads one
	// template argument back under.  A specialization writes its arguments
	// after the template's name, and each of them is a type the encoder has to
	// be handed rather than a spelling, for the same reason a local context is.
	const std::string& argument_of(TypeId type);
	// 14.1p4 and `<expression>`: the identifier the encoder reads one dependent
	// *expression* back under.  An argument at a value place no substitution
	// has settled is written as the expression it is - the parameter, or
	// 14.5.3p4's expansion of one - and not as the type standing for it.
	const std::string& expression_of(TypeId type);
	// 14.3.2p1 and `<expr-primary>`'s `L <mangled-name> E`: the identifier the
	// encoder reads back the *declaration* an address argument designates.  It
	// is a name of its own with its own substitution table, which is why the
	// encoder is handed the declaration rather than a spelling of it.
	const std::string& entity_of(const SemaEntity& object);

	const abi_mangle::AbiDefinitionMap& definitions() const { return map_; }

private:
	TypeTable& types_;
	// The records the map points at.  A deque is what holds them: describing
	// one function may describe the function whose body declared *its* class,
	// and a reference already handed out has to survive that.
	std::deque<abi_mangle::AbiDefinitionRecord> records_;
	std::map<const SemaEntity*, std::string> ids_;
	std::map<const SemaEntity*, std::string> entities_;
	std::map<TypeId, std::string> arguments_;
	std::map<TypeId, std::string> expressions_;
	abi_mangle::AbiDefinitionMap map_;
};

// 14.1p4: whether an argument no substitution has settled stands at a *value*
// place - a non-type parameter, or 14.5.3p4's expansion of one.  What such an
// argument is written as is 5.19's expression and not 8.1p1's type-id, which is
// the same question the place itself answers for a written argument.
bool written_as_expression(TypeTable& types, TypeId type)
{
	return types.parameter_value_type(
		types.is_pack_expansion(type) ? types.target(type) : type) != kNoType;
}

// 14.5.3p1 and `<template-args>`: the arguments one written list gave a
// specialization, where the ones a pack place took are *one* argument however
// many the run holds.  The list a specialization is keyed by is flat - 14.4p1
// makes two lists of the same arguments one list - so the place the run begins
// at is what splits it back, and a head that declared a pack writes the run
// even where it is empty.
void argument_refs(TypeTable& types, const std::vector<TypeId>& arguments,
                   unsigned pack_at, LocalContexts& contexts,
                   std::vector<std::string>& refs)
{
	const std::size_t fixed =
		pack_at < arguments.size() ? pack_at : arguments.size();
	for (std::size_t index = 0; index < fixed; ++index)
	{
		refs.push_back(contexts.argument_of(arguments[index]));
	}
	if (pack_at == TypeTable::kNoPackPlace)
	{
		return;
	}
	refs.push_back(contexts.argument_of(types.pack_type(
		std::vector<TypeId>(arguments.begin() + fixed, arguments.end()))));
}

AbiType wrapped(abi_mangle::AbiTypeKind kind, TypeTable& types,
                TypeId inner, LocalContexts& contexts)
{
	AbiType out;
	out.kind = kind;
	out.types.push_back(abi_type(types, inner, contexts));
	return out;
}

// One C++ type as the ABI facts that describe it.  3.9.3p2 makes a
// cv-qualifier on an array a qualifier of its elements, which is where the
// type model already holds it, so the walk needs no special case for it.
AbiType abi_type(TypeTable& types, TypeId type, LocalContexts& contexts)
{
	const unsigned cv = types.cv(type);
	if (cv != kCvNone)
	{
		AbiType out;
		out.kind = abi_mangle::ABI_TYPE_CV;
		out.is_const = (cv & kCvConst) != 0;
		out.is_volatile = (cv & kCvVolatile) != 0;
		out.types.push_back(abi_type(types, types.strip_cv(type), contexts));
		return out;
	}
	switch (types.kind(type))
	{
	case TypeKind::Pointer:
		return wrapped(abi_mangle::ABI_TYPE_POINTER, types, types.target(type),
		               contexts);

	case TypeKind::LValueReference:
		return wrapped(abi_mangle::ABI_TYPE_LVALUE_REFERENCE, types,
		               types.target(type), contexts);

	case TypeKind::RValueReference:
		return wrapped(abi_mangle::ABI_TYPE_RVALUE_REFERENCE, types,
		               types.target(type), contexts);

	case TypeKind::Array:
	{
		AbiType out = wrapped(abi_mangle::ABI_TYPE_ARRAY, types,
		                      types.target(type), contexts);
		out.array_bound.kind = abi_mangle::ABI_ARRAY_BOUND_RAW;
		// 8.3.4p1: an array of unknown bound is written with none at all,
		// which its encoding says by leaving the bound empty.
		out.array_bound.value =
			types.bounded(type) ? decimal(types.bound(type)) : std::string();
		return out;
	}

	case TypeKind::Function:
	{
		// 8.3.5: the parameters of a function type are what it takes, and the
		// encoding writes the result before them.
		AbiType out;
		out.kind = abi_mangle::ABI_TYPE_FUNCTION;
		out.variadic = types.variadic(type);
		// 8.3.5p7: the ref-qualifier is part of the function type, so a template
		// argument that writes one is a different argument from the one that does
		// not - `holder<int(char) const>` and `holder<int(char) const &>` are two
		// specializations and two symbols.
		const RefQualifier ref = types.function_ref_qualifier(type);
		out.function_lvalue_ref = ref == RefQualifier::LValue;
		out.function_rvalue_ref = ref == RefQualifier::RValue;
		out.types.push_back(abi_type(types, types.target(type), contexts));
		const std::vector<TypeId>& parameters = types.parameters(type);
		for (std::size_t index = 0; index < parameters.size(); ++index)
		{
			out.types.push_back(abi_type(types, parameters[index], contexts));
		}
		return out;
	}

	case TypeKind::MemberPointer:
	{
		AbiType out;
		out.kind = abi_mangle::ABI_TYPE_MEMBER_POINTER;
		out.types.push_back(abi_type(types, types.member_class(type), contexts));
		out.types.push_back(abi_type(types, types.target(type), contexts));
		return out;
	}

	case TypeKind::TemplateParameter:
	{
		if (types.dependent_owner(type) != kNoType)
		{
			// 14.6.2p1 and the ABI's `<unresolved-name>`: a name written after
			// a prefix that depends on a parameter is a member of that prefix,
			// which the object file writes as the prefix and then the name -
			// so `typename T::car_type` is `NT_8car_typeE` and not the `T_` a
			// parameter standing on its own would be.
			//
			// 14.2p4 and `<unresolved-name>`'s third form: where the member was
			// written as a template-id the list is part of the name, so the ABI
			// writes the prefix, the name and then `<template-args>` - which is
			// what makes `typename T::template rebind<U>` a name two units
			// spell the same way.
			AbiType out;
			out.kind = types.dependent_member_is_template_id(type)
				? abi_mangle::ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION
				: abi_mangle::ABI_TYPE_MEMBER;
			out.name = types.dependent_member(type);
			out.types.push_back(
				abi_type(types, types.dependent_owner(type), contexts));
			if (out.kind == abi_mangle::ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION)
			{
				argument_refs(types, types.dependent_arguments(type),
				              TypeTable::kNoPackPlace, contexts,
				              out.argument_refs);
			}
			return out;
		}
		// 14.1p2 and `<template-param>`: a specialization's own name is
		// encoded from the template's signature, where the parameter stands
		// for itself and is written by its place among the ones its head
		// declared.
		AbiType out;
		out.kind = abi_mangle::ABI_TYPE_TEMPLATE_PARAMETER;
		out.index = types.template_index(type);
		// The ABI's compression makes a `<template-param>` a substitution
		// candidate like any other component, so `T x` written twice in one
		// signature is the parameter and then a reference back to it - which
		// is what the second unit reading the name has to spell the same way.
		out.substitutable = true;
		return out;
	}

	case TypeKind::Pack:
		// 14.5.3p4 and the ABI's `Dp`: a place written `P...` is the pattern
		// under the pack-expansion operator, which is what the *template's*
		// signature holds where its last place is a pack - the specialization's
		// own places are as many as the run and are not what names it.  A
		// settled run never stands where a type does: it is written as an
		// argument, and `argument_of` takes it before this walk is asked.
		if (types.is_pack_expansion(type))
		{
			return wrapped(abi_mangle::ABI_TYPE_PACK_EXPANSION, types,
			               types.target(type), contexts);
		}
		break;

	case TypeKind::Class:
	case TypeKind::Enum:
	{
		AbiType out;
		out.name = types.user_qualified_name(type);
		// 9.8p1 and 3.5p8: a type a function's body declared is reached by no
		// name from outside that function, and two functions may each declare
		// one of the same spelling - so the object file names it after the
		// function, and the regions the name is written with say nothing.
		const SemaEntity* function = types.local_function(type);
		if (function != nullptr)
		{
			out.kind = abi_mangle::ABI_TYPE_LOCAL_TYPE;
			out.context_ref = contexts.context_of(*function);
			if (types.local_unnamed(type))
			{
				// The body wrote no name at all, so the spelling `out.name`
				// carries is one this unit made up and another unit gives to a
				// class of its own: what the object file names it by is its
				// place among the types that function left unnamed.
				out.unnamed_index = decimal(types.local_occurrence(type));
			}
			else
			{
				out.discriminator = decimal(types.local_occurrence(type));
			}
			return out;
		}
		// 14.2 and `<template-args>`: a specialization is named by its
		// template and the arguments that made it, which the ABI writes apart
		// so that a use of the same class in another unit spells it the same
		// way whatever the source called its arguments.
		if (types.is_specialization(type))
		{
			out.kind = abi_mangle::ABI_TYPE_TEMPLATE_SPECIALIZATION;
			out.name = types.template_name(type);
			argument_refs(types, types.template_arguments(type),
			              types.template_pack_place(type), contexts,
			              out.argument_refs);
			return out;
		}
		// 9.1p2 and 14.2: a class or enumeration a specialization declares is
		// named *through* it, and the ABI writes that class as its template and
		// the arguments - which the one spelling the type carries cannot be
		// split back into.  So the enclosing class is asked for its own
		// encoding and this name stands after it, however many classes deep the
		// declaration is.  A type no template stands over is the spelling it
		// always was, which is what a class the program wrote needs.
		const SemaEntity* const declared = types.declaration(type);
		if (declared != nullptr)
		{
			const std::vector<const SemaEntity*> owners = owning_classes(*declared);
			bool specialized = false;
			for (std::size_t index = 0; index < owners.size(); ++index)
			{
				specialized = specialized ||
					types.is_specialization(owners[index]->type);
			}
			if (specialized)
			{
				out.kind = abi_mangle::ABI_TYPE_MEMBER;
				out.name = declared->name;
				out.types.push_back(abi_type(types, owners.front()->type, contexts));
				return out;
			}
		}
		// 3.4.3: the type is named from outside every region that encloses its
		// declaration, which is the spelling an object-file name carries.
		out.kind = abi_mangle::ABI_TYPE_NAMED;
		return out;
	}

	default:
		break;
	}
	return builtin(fundamental_word(types.fundamental_type(type)));
}

// 13.5: the word PA14's encoder knows an operator function's terminal by.
// `+ - * &` are written for one operand as well as for two, and it is the
// number of operands that says which operator was declared.
bool operator_terminal(const std::string& written, std::size_t arity,
                       std::string& terminal, std::string& literal_suffix)
{
	// 13.5.8: a literal-operator-id is `operator ""` and an identifier, which
	// the encoding writes as its own terminal followed by that identifier as a
	// source name rather than as one of the operator codes.
	if (written.size() > 2 && written.compare(0, 2, "\"\"") == 0)
	{
		terminal = "literal";
		literal_suffix = written.substr(2);
		return true;
	}
	static const struct
	{
		const char* spelled;
		const char* unary;
		const char* binary;
	} kOperators[] = {
		{"new", "new", "new"},
		{"new[]", "new-array", "new-array"},
		{"delete", "delete", "delete"},
		{"delete[]", "delete-array", "delete-array"},
		{"+", "unary-plus", "binary-plus"},
		{"-", "unary-minus", "binary-minus"},
		{"*", "deref", "multiply"},
		{"&", "address-of", "bit-and"},
		{"/", "divide", "divide"},
		{"%", "remainder", "remainder"},
		{"^", "bit-xor", "bit-xor"},
		{"|", "bit-or", "bit-or"},
		{"~", "bit-not", "bit-not"},
		{"!", "logical-not", "logical-not"},
		{"=", "assign", "assign"},
		{"<", "less", "less"},
		{">", "greater", "greater"},
		{"+=", "plus-assign", "plus-assign"},
		{"-=", "minus-assign", "minus-assign"},
		{"*=", "multiply-assign", "multiply-assign"},
		{"/=", "divide-assign", "divide-assign"},
		{"%=", "remainder-assign", "remainder-assign"},
		{"^=", "bit-xor-assign", "bit-xor-assign"},
		{"&=", "bit-and-assign", "bit-and-assign"},
		{"|=", "bit-or-assign", "bit-or-assign"},
		{"<<", "shift-left", "shift-left"},
		{">>", "shift-right", "shift-right"},
		{"<<=", "shift-left-assign", "shift-left-assign"},
		{">>=", "shift-right-assign", "shift-right-assign"},
		{"==", "equal", "equal"},
		{"!=", "not-equal", "not-equal"},
		{"<=", "less-equal", "less-equal"},
		{">=", "greater-equal", "greater-equal"},
		{"&&", "logical-and", "logical-and"},
		{"||", "logical-or", "logical-or"},
		{"++", "increment", "increment"},
		{"--", "decrement", "decrement"},
		{",", "comma", "comma"},
		{"->*", "member-pointer", "member-pointer"},
		{"->", "arrow", "arrow"},
		{"()", "call", "call"},
		{"[]", "index", "index"},
	};
	const std::size_t count = sizeof(kOperators) / sizeof(kOperators[0]);
	for (std::size_t index = 0; index < count; ++index)
	{
		if (written == kOperators[index].spelled)
		{
			terminal = arity >= 2 ? kOperators[index].binary
			                      : kOperators[index].unary;
			return true;
		}
	}
	return false;
}

}  // namespace

std::string abi_thread_wrapper_of(const SemaEntity& entity)
{
	abi_mangle::AbiTargetRecord target;
	target.kind = abi_mangle::ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER;
	target.qualified_name = abi_qualified_name(entity);
	return abi_mangle::mangle_target(target,
	                                 std::vector<abi_mangle::AbiFunctionRecord>(),
	                                 abi_mangle::AbiDefinitionMap());
}

namespace
{

// The three names the polymorphic object model gives a class, which differ only
// in the record kind the encoder is handed.
std::string abi_type_target(TypeId type, TypeTable& types,
                            abi_mangle::AbiTargetFactKind kind)
{
	LocalContexts contexts(types);
	abi_mangle::AbiTargetRecord target;
	target.kind = kind;
	target.type = abi_type(types, types.strip_cv(type), contexts);
	return abi_mangle::mangle_target(target,
	                                 std::vector<abi_mangle::AbiFunctionRecord>(),
	                                 contexts.definitions());
}

}  // namespace

std::string abi_vtable_symbol_of(TypeId type, TypeTable& types)
{
	return abi_type_target(type, types, abi_mangle::ABI_TARGET_FACT_VTABLE);
}

std::string abi_typeinfo_symbol_of(TypeId type, TypeTable& types)
{
	return abi_type_target(type, types, abi_mangle::ABI_TARGET_FACT_TYPEINFO);
}

std::string abi_typeinfo_name_symbol_of(TypeId type, TypeTable& types)
{
	return abi_type_target(type, types,
	                       abi_mangle::ABI_TARGET_FACT_TYPEINFO_NAME);
}

// The type's own encoding, which is what the name string holds: 5.2.8p14 makes
// `type_info::name` an implementation-defined spelling, and the ABI's is the
// encoded type - the `_ZTS` name with the four characters that say what the
// name is for taken off the front.
std::string abi_type_name_of(TypeId type, TypeTable& types)
{
	const std::string named = abi_typeinfo_name_symbol_of(type, types);
	return named.compare(0, 4, "_ZTS") == 0 ? named.substr(4) : named;
}

namespace
{

// 7.5p6 and 3.6.1p1: whether the object file names this function by the
// spelling the program wrote rather than by an encoding of its declaration.
// A name another translation unit reaches by its C spelling is that spelling,
// and `main` is the one C++ function every implementation names the same way.
bool named_by_its_spelling(const SemaEntity& entity)
{
	return (entity.c_linkage && !entity.internal_linkage) ||
		(entity.kind == SemaKind::Function && entity.dump_name == "main");
}

// The components one qualified name is written from, outermost first.
//
// 14.2: a template-argument-list is written inside one component and may spell
// a qualified name of its own, so `A<N::B>::f` is three names and not four -
// the `::` a component holds belongs to the argument around it.  A
// template-id therefore only ever stands *last* in a name this splits, because
// the components after it are the classes and members of a specialization,
// which are asked of the declaration rather than of its spelling.
std::vector<std::string> name_components(const std::string& spelled)
{
	std::vector<std::string> parts;
	std::size_t at = 0;
	unsigned depth = 0;
	for (std::size_t index = 0; index < spelled.size(); ++index)
	{
		const char c = spelled[index];
		if (c == '<' || c == '(' || c == '[')
		{
			++depth;
			continue;
		}
		if ((c == '>' || c == ')' || c == ']') && depth != 0)
		{
			--depth;
			continue;
		}
		if (depth == 0 && c == ':' && index + 1 < spelled.size() &&
		    spelled[index + 1] == ':')
		{
			parts.push_back(spelled.substr(at, index - at));
			at = index + 2;
			++index;
		}
	}
	parts.push_back(spelled.substr(at));
	return parts;
}

// 14.2: the classes a declaration is named through, innermost first.
//
// A component of an encoded name is a source name unless the class it names is
// a specialization, which the ABI writes as the template's name and then the
// arguments - so the walk that builds the name has to ask the class rather
// than the spelling, which cannot be split back into the two.
//
// Each class is asked where *its own* declaration stands rather than where the
// region around this one does: 9.1p2 names a member through the class that
// declared it however the definition was written, and 9.7p3's definition of a
// nested class outside its enclosing one opens a region under whatever the
// definition was written in.
std::vector<const SemaEntity*> owning_classes(const SemaEntity& entity)
{
	std::vector<const SemaEntity*> owners;
	for (const SemaEntity* at = &entity; at != nullptr;)
	{
		const Scope* region = at->region;
		while (region != nullptr && region->kind == ScopeKind::TemplateParameters)
		{
			// 14.1p1: the region a specialization's bindings stand in encloses
			// the class alone and names nothing.
			region = region->parent;
		}
		if (region == nullptr || region->kind != ScopeKind::Class ||
		    region->owner == nullptr || region->owner == at)
		{
			break;
		}
		owners.push_back(region->owner);
		at = region->owner;
	}
	return owners;
}

// One component of an encoded name: the class it names, or null where it names
// a namespace.
struct NameRegion
{
	const SemaEntity* owner;
	std::string name;
};

// The regions one declaration is named through, outermost first, with the
// declaration's own name left off - which is where a terminal, an operator or
// a constructor stands instead.
//
// The classes come from the walk above rather than from a spelling, because a
// specialization is a component no spelling can be split back into.  What is
// left in front of them is namespaces, and 7.3.1p1 gives a namespace one
// identifier for a name - so that prefix is the one part of a qualified name
// `::` does split.
std::vector<NameRegion> name_regions(const SemaEntity& entity, TypeTable& types,
                                     const std::vector<const SemaEntity*>& owners)
{
	// The outermost class carries its own qualified spelling on its type; a
	// declaration no class stands over carries it as its name.
	std::vector<std::string> spaces = name_components(
		owners.empty() ? abi_qualified_name(entity)
		               : types.user_qualified_name(owners.back()->type));
	// The outermost declaration's own component is the class the walk already
	// holds, or the declaration itself, and neither stands here.
	spaces.pop_back();
	std::vector<NameRegion> regions;
	regions.reserve(spaces.size() + owners.size());
	for (std::size_t index = 0; index < spaces.size(); ++index)
	{
		NameRegion region;
		region.owner = nullptr;
		region.name = spaces[index];
		regions.push_back(region);
	}
	for (std::size_t index = owners.size(); index-- > 0;)
	{
		NameRegion region;
		region.owner = owners[index];
		// 9.5p1 and 9.1p2: a class the program left unnamed still stands over
		// the members declared in it, and the object file has to write a
		// component for it - so the name the translation gave the *type* stands
		// where the declaration has none of its own.  It is one identifier by
		// construction, which is what a `<source-name>` is.
		region.name = owners[index]->name.empty()
			? types.user_name(owners[index]->type)
			: owners[index]->name;
		regions.push_back(region);
	}
	return regions;
}

// Whether any class this declaration is named through is a specialization,
// which is what makes its spelling one the encoder cannot split for itself.
bool names_a_specialization(const std::vector<NameRegion>& regions,
                            TypeTable& types)
{
	for (std::size_t index = 0; index < regions.size(); ++index)
	{
		if (regions[index].owner != nullptr &&
		    types.is_specialization(regions[index].owner->type))
		{
			return true;
		}
	}
	return false;
}

// The record one region of an encoded name is written as: a source name, or -
// where the class a template made stands there - the template's own name and
// the arguments that made it.
abi_mangle::AbiFunctionRecord region_record(const NameRegion& region,
                                            TypeTable& types,
                                            LocalContexts& contexts)
{
	abi_mangle::AbiFunctionRecord record;
	if (region.owner != nullptr && types.is_specialization(region.owner->type))
	{
		record.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_TEMPLATE;
		record.name = name_components(types.template_name(region.owner->type)).back();
		argument_refs(types, types.template_arguments(region.owner->type),
		              types.template_pack_place(region.owner->type), contexts,
		              record.argument_refs);
		return record;
	}
	record.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_SOURCE;
	record.name = region.name;
	return record;
}

// 9.4.2p1: the components a static data member's name is written from, where
// one of the classes it is named through is a specialization.
//
// A data name is otherwise one spelling the encoder splits for itself, and
// that is what a class the program wrote needs.  A specialization cannot be
// split back out of its spelling, so the components are handed over instead,
// with the member's own name standing where a function's terminal does.
void build_data_name(const SemaEntity& entity, TypeTable& types,
                     LocalContexts& contexts,
                     abi_mangle::AbiTargetRecord& target,
                     std::vector<abi_mangle::AbiFunctionRecord>& records)
{
	const std::vector<NameRegion> regions =
		name_regions(entity, types, owning_classes(entity));
	if (!names_a_specialization(regions, types))
	{
		return;
	}
	target.function.kind = abi_mangle::ABI_FUNCTION_TARGET_ENCODING;
	for (std::size_t index = 0; index < regions.size(); ++index)
	{
		records.push_back(region_record(regions[index], types, contexts));
	}
	// The encoder drops the last *name* component where a terminal names the
	// entity, so the components before it stay the regions they are.
	abi_mangle::AbiFunctionRecord placeholder;
	placeholder.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_SOURCE;
	placeholder.name = entity.name;
	records.push_back(placeholder);
	abi_mangle::AbiFunctionRecord written;
	written.kind = abi_mangle::ABI_FUNCTION_RECORD_TERMINAL_SOURCE;
	written.source_name = entity.name;
	records.push_back(written);
}

// The typed facts one function declaration is encoded from: what names it, the
// regions it is named through, its cv-qualifiers and its parameter types.
//
// It is asked of the function whose name is wanted and of the function whose
// body declared that function's class, so the two spellings of one declaration
// are built by one reading of it.
void build_function_name(const SemaEntity& entity, TypeTable& types,
                         unsigned variant, LocalContexts& contexts,
                         abi_mangle::AbiTargetRecord& target,
                         std::vector<abi_mangle::AbiFunctionRecord>& records)
{
	target.kind = abi_mangle::ABI_TARGET_FACT_FUNCTION;
	// 14.2 and `<template-args>`: a function-template specialization is named
	// by the template, the arguments that made it, its result type and the
	// *template's* signature - where a parameter the head declared stands for
	// itself.  8.3.5p5 drops a top-level cv-qualifier from a parameter, so two
	// specializations can have one function type and only the arguments tell
	// them apart, which is exactly why the ABI writes them.
	const SemaEntity* const templated =
		entity.kind == SemaKind::Function && entity.primary != nullptr &&
			entity.primary->template_parameters != nullptr
		? entity.primary
		: nullptr;
	const std::vector<TypeId>& parameters =
		types.parameters(templated != nullptr ? templated->type : entity.type);
	// 9.3.1p3: the object parameter is not one the declarator wrote, so it is
	// not one the name encodes; 8.3.5p7's cv-qualifier-seq on the function is
	// what the object it points to carries, and the encoding writes that as a
	// qualifier of the name rather than as a parameter.
	const std::size_t first = entity.object_member ? 1u : 0u;
	std::string terminal;
	std::string literal_suffix;
	// 12.1 and 12.4: a constructor and a destructor are named by what they are
	// rather than by a source name, and `abi_variant` says which of the
	// entry points of one this symbol is.
	const bool special = entity.special != kOrdinaryFunction;
	// 13.5p1: what tells `-` written for one operand from `-` written for two
	// is how many operands the declaration takes, and 9.3.1p3 made the object
	// one of them - so a member `operator-(const T&)` is the binary operator
	// however few parameters its declarator wrote.  This is the one question
	// the object parameter is counted in; the encoding still leaves it out.
	const bool named_by_terminal = special || entity.conversion_function ||
		(entity.name.compare(0, 8, "operator") == 0 &&
		 operator_terminal(entity.name.substr(8), parameters.size(), terminal,
		                   literal_suffix));
	// 14.2: a member of a specialization is named through the template and the
	// arguments that made it, which no path built from one spelling can write.
	const std::vector<NameRegion> regions =
		name_regions(entity, types, owning_classes(entity));
	// 9.8p1: a member of a class a function's body declared is named through
	// that class and the class through the function, so the whole name stands
	// inside the context and the regions the definition was written in say
	// nothing at all.
	const bool local = entity.local_function != nullptr && !regions.empty();
	const bool specialized = names_a_specialization(regions, types);
	if (named_by_terminal || local || specialized)
	{
		// 13.5: an operator function is named by the operator it overloads,
		// which the encoding spells as its own terminal rather than as a source
		// name.  The regions around the declaration still precede it, with one
		// component standing where that terminal goes.
		target.function.kind = abi_mangle::ABI_FUNCTION_TARGET_ENCODING;
		for (std::size_t index = 0; index < regions.size(); ++index)
		{
			abi_mangle::AbiFunctionRecord region;
			if (index == 0 && local)
			{
				// The entity the function itself declared is the one component
				// the occurrence number belongs to; a class it holds follows it
				// the way a member of any class does.
				region.kind = abi_mangle::ABI_FUNCTION_RECORD_LOCAL_CONTEXT;
				region.context_ref = contexts.context_of(*entity.local_function);
				if (entity.local_unnamed)
				{
					// The class this member is named through has no spelling of
					// its own, so what stands here is its place among the types
					// the function left unnamed.
					region.unnamed_index = decimal(entity.local_occurrence);
				}
				else
				{
					region.source_name = regions[index].name;
					region.discriminator = decimal(entity.local_occurrence);
				}
			}
			else
			{
				region = region_record(regions[index], types, contexts);
			}
			records.push_back(region);
		}
		if (!regions.empty())
		{
			abi_mangle::AbiFunctionRecord placeholder;
			placeholder.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_SOURCE;
			placeholder.name = entity.name;
			records.push_back(placeholder);
		}
		abi_mangle::AbiFunctionRecord written;
		if (special)
		{
			written.kind = abi_mangle::ABI_FUNCTION_RECORD_TERMINAL;
			written.terminal = entity.special == kConstructorFunction
				? (variant == kBaseObjectAbi ? "constructor-base"
				                             : "constructor-complete")
				: (variant == kBaseObjectAbi
				       ? "destructor-base"
				       : (variant == kDeletingObjectAbi
				              ? "destructor-deleting"
				              : "destructor-complete"));
		}
		else if (entity.conversion_function)
		{
			// 12.3.2p1: a conversion function is named by the type it converts
			// to, which the encoding spells as `cv` and that type - and, having
			// written it as the name, does not write it again as the result.
			// 14.5.2p1's conversion function template is named by the
			// conversion-type-id its *head* was written over, exactly as its
			// signature below is - so the place stands for itself here and the
			// arguments that bound it are written once, after the name.
			written.kind = abi_mangle::ABI_FUNCTION_RECORD_CONVERSION_TERMINAL;
			written.type = abi_type(
				types, types.target(templated != nullptr ? templated->type
				                                         : entity.type),
				contexts);
		}
		else if (named_by_terminal)
		{
			written.kind = abi_mangle::ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
			written.terminal = terminal;
			written.literal_suffix = literal_suffix;
		}
		else
		{
			// An ordinary name under a local context stands where a terminal
			// does, because the components before it are the context's and not
			// a path the encoder splits for itself.
			written.kind = abi_mangle::ABI_FUNCTION_RECORD_TERMINAL_SOURCE;
			written.source_name = entity.name;
		}
		records.push_back(written);
	}
	else
	{
		target.function.kind = abi_mangle::ABI_FUNCTION_TARGET_PATH;
		target.function.qualified_name = abi_qualified_name(entity);
	}
	if (first != 0 && entity.special != kDestructorFunction)
	{
		// 12.4p1: a destructor writes no cv-qualifier-seq, so the qualifiers
		// 12.4p12 put on its object parameter are none the name encodes.
		const unsigned cv = types.cv(types.target(parameters[0]));
		if ((cv & kCvConst) != 0)
		{
			abi_mangle::AbiFunctionRecord qualifier;
			qualifier.kind = abi_mangle::ABI_FUNCTION_RECORD_QUALIFIER;
			qualifier.qualifiers.push_back(abi_mangle::ABI_FUNCTION_QUALIFIER_CONST);
			records.push_back(qualifier);
		}
		if ((cv & kCvVolatile) != 0)
		{
			abi_mangle::AbiFunctionRecord qualifier;
			qualifier.kind = abi_mangle::ABI_FUNCTION_RECORD_QUALIFIER;
			qualifier.qualifiers.push_back(
				abi_mangle::ABI_FUNCTION_QUALIFIER_VOLATILE);
			records.push_back(qualifier);
		}
		// 8.3.5p1: the ref-qualifier is part of the function type, so two
		// members that differ only in it are two functions the object file has
		// to name apart.
		const RefQualifier ref = types.function_ref_qualifier(entity.type);
		if (ref != RefQualifier::None)
		{
			abi_mangle::AbiFunctionRecord qualifier;
			qualifier.kind = abi_mangle::ABI_FUNCTION_RECORD_QUALIFIER;
			qualifier.qualifiers.push_back(
				ref == RefQualifier::RValue
					? abi_mangle::ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE
					: abi_mangle::ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE);
			records.push_back(qualifier);
		}
	}
	if (templated != nullptr)
	{
		abi_mangle::AbiFunctionRecord written;
		written.kind = abi_mangle::ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
		// 14.5.3p1: a function template's places are declarations rather than a
		// `TemplateInfo`, so the place its run begins at is read off them - the
		// same question `specialize` asked to bind the run in the first place.
		// A run at any earlier place already stands as one entry of the list,
		// so it is written `J...E` by the walk over the entries themselves.
		const std::vector<SemaEntity*>& places =
			templated->template_parameters->declarations;
		const std::size_t packed = trailing_pack_place(types, places);
		argument_refs(types, types.type_list_at(entity.template_arguments),
		              packed < places.size() ? static_cast<unsigned>(packed)
		                                     : TypeTable::kNoPackPlace,
		              contexts, written.argument_refs);
		records.push_back(written);
		// The ABI writes a function template's result type, because two
		// specializations of one template can differ in it alone.  12.1p1 and
		// 12.4p1 leave a constructor and a destructor no return type to write,
		// and 12.3.2p1 wrote a conversion function's as the name above - so
		// 14.5.2p1's member template of one of those three writes none here.
		if (!special && !entity.conversion_function)
		{
			abi_mangle::AbiFunctionRecord result;
			result.kind = abi_mangle::ABI_FUNCTION_RECORD_RESULT;
			result.type =
				abi_type(types, types.target(templated->type), contexts);
			records.push_back(result);
		}
	}
	for (std::size_t index = first; index < parameters.size(); ++index)
	{
		abi_mangle::AbiFunctionRecord parameter;
		parameter.kind = abi_mangle::ABI_FUNCTION_RECORD_PARAMETER;
		parameter.type = abi_type(types, parameters[index], contexts);
		records.push_back(parameter);
	}
	if (types.variadic(entity.type))
	{
		abi_mangle::AbiFunctionRecord ellipsis;
		ellipsis.kind = abi_mangle::ABI_FUNCTION_RECORD_VARIADIC;
		records.push_back(ellipsis);
	}
}

const std::string& LocalContexts::argument_of(TypeId type)
{
	const std::map<TypeId, std::string>::iterator held = arguments_.find(type);
	if (held != arguments_.end())
	{
		return held->second;
	}
	// The identifier is recorded before the type is described, because that
	// type may be a specialization of its own whose arguments walk this map
	// again.
	const std::string id = "argument" + decimal(arguments_.size());
	const std::map<TypeId, std::string>::iterator placed =
		arguments_.insert(std::make_pair(type, id)).first;
	records_.push_back(abi_mangle::AbiDefinitionRecord());
	abi_mangle::AbiDefinitionRecord& record = records_.back();
	record.kind = abi_mangle::ABI_DEFINITION_TEMPLATE_ARGUMENT;
	record.id = id;
	map_[id] = &record;
	if (types_.is_value(type))
	{
		// 14.1p4: only a pointer or a reference place binds an *address*, and
		// the bits an argument at one holds are this unit's own entry number
		// for it - a small integer of no meaning at any other place.  So the
		// table is asked of those two alone: `sample<char, 2, true>` written as
		// the argument of a `void (*)()` place otherwise reads its own `N = 2`
		// as entry 2, which is the address of `sample<char, 2, true>`, and the
		// encoding walks into itself without bound.
		const TypeId place = types_.strip_cv(types_.target(type));
		const bool addressed = types_.kind(place) == TypeKind::Pointer ||
			types_.kind(place) == TypeKind::LValueReference ||
			types_.kind(place) == TypeKind::RValueReference;
		const SemaEntity* const designated =
			addressed ? types_.address_object(types_.value_bits(type)) : nullptr;
		if (designated != nullptr)
		{
			// 14.3.2p1 and the ABI's `<expr-primary>`: an argument at one of
			// 14.1p4's address places is *which object* it designates, so the
			// name writes that declaration's own encoding - `&left` at a
			// pointer place and the bare name at a reference place, which is
			// the difference between `X ad L_Z4left E E` and `L_Z4left E`.
			//
			// The value beside it is this unit's entry number for the address
			// and says nothing a second unit would agree with: two units that
			// each name `at<&left>` write one weak definition under two names
			// unless the declaration is what is encoded.
			record.template_argument.kind =
				abi_mangle::ABI_TEMPLATE_ARGUMENT_ENTITY;
			record.template_argument.address_of =
				!types_.is_reference(types_.target(type));
			record.template_argument.entity_ref = entity_of(*designated);
			return placed->second;
		}
		// 14.3.2p1 and the ABI's `<expr-primary>`: an argument at a non-type
		// place is written as the value and the type it was converted to, which
		// is what makes `f<3>` and `f<'\3'>` one name in the object file.
		record.template_argument.kind = abi_mangle::ABI_TEMPLATE_ARGUMENT_VALUE;
		record.template_argument.value_type =
			abi_type(types_, types_.target(type), *this);
		record.template_argument.value =
			static_cast<long long>(types_.value_bits(type));
		return placed->second;
	}
	if (types_.is_template_name(type))
	{
		// 14.3.3p1 and `<template-arg>`'s `<template-name>`: an argument at a
		// template place is written as the template it named, which is the
		// declaration's own name and no type at all - so the entry carries the
		// name rather than anything a type could be encoded from.
		record.template_argument.kind =
			abi_mangle::ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY;
		record.template_argument.name = types_.user_qualified_name(type);
		return placed->second;
	}
	if (types_.is_settled_run(type))
	{
		// 14.5.3p1 and `<template-arg>`'s `J...E`: the run a pack place took is
		// one argument holding however many the list gave it, which is what a
		// second unit reading the name splits back into the same run.
		record.template_argument.kind = abi_mangle::ABI_TEMPLATE_ARGUMENT_PACK;
		const std::vector<TypeId>& run = types_.pack_elements(type);
		for (std::size_t index = 0; index < run.size(); ++index)
		{
			record.template_argument.argument_refs.push_back(
				argument_of(run[index]));
		}
		return placed->second;
	}
	if (written_as_expression(types_, type))
	{
		// 14.1p4 and `<template-arg>`'s `X <expression> E`: a place at a value
		// position no argument list settled is written as the expression that
		// names it, so `S<N>` is `1SIXT_EE` rather than the `1SIT_E` a type
		// place makes.
		record.template_argument.kind =
			abi_mangle::ABI_TEMPLATE_ARGUMENT_EXPRESSION;
		record.template_argument.name = expression_of(type);
		return placed->second;
	}
	record.template_argument.kind = abi_mangle::ABI_TEMPLATE_ARGUMENT_TYPE;
	record.template_argument.type = abi_type(types_, type, *this);
	return placed->second;
}

const std::string& LocalContexts::entity_of(const SemaEntity& object)
{
	const std::map<const SemaEntity*, std::string>::iterator held =
		entities_.find(&object);
	if (held != entities_.end())
	{
		return held->second;
	}
	const std::string id = "entity" + decimal(entities_.size());
	const std::map<const SemaEntity*, std::string>::iterator placed =
		entities_.insert(std::make_pair(&object, id)).first;
	records_.push_back(abi_mangle::AbiDefinitionRecord());
	abi_mangle::AbiDefinitionRecord& record = records_.back();
	record.kind = abi_mangle::ABI_DEFINITION_ENTITY;
	record.id = id;
	map_[id] = &record;
	// `<expr-primary> ::= L <mangled-name> E`, and the mangled name of the
	// declaration is the one this unit already names its definition by - which
	// is the whole signature for a function 13.4p1 chose, and the template and
	// its arguments for a static data member of a specialization, neither of
	// which any spelling of the name can be split back into.
	const std::string symbol = abi_symbol_of(object, types_);
	if (symbol.compare(0, 2, "_Z") == 0)
	{
		record.entity.kind = abi_mangle::ABI_ENTITY_FACT_SYMBOL;
		record.entity.qualified_name = symbol;
		return placed->second;
	}
	// 3.5p9 leaves a variable at namespace scope named by its own identifier in
	// the object file, and `<mangled-name>` writes the encoding of that name
	// rather than the symbol - so `left` stands here as `_Z4left`, with 3.5p3's
	// internal linkage written on the last component as `_ZL6hidden`.
	record.entity.kind = abi_mangle::ABI_ENTITY_FACT_VARIABLE;
	record.entity.qualified_name = abi_qualified_name(object);
	record.entity.internal_linkage = object.internal_linkage;
	return placed->second;
}

const std::string& LocalContexts::expression_of(TypeId type)
{
	const std::map<TypeId, std::string>::iterator held =
		expressions_.find(type);
	if (held != expressions_.end())
	{
		return held->second;
	}
	const std::string id = "expression" + decimal(expressions_.size());
	const std::map<TypeId, std::string>::iterator placed =
		expressions_.insert(std::make_pair(type, id)).first;
	records_.push_back(abi_mangle::AbiDefinitionRecord());
	abi_mangle::AbiDefinitionRecord& record = records_.back();
	record.kind = abi_mangle::ABI_DEFINITION_EXPRESSION;
	record.id = id;
	map_[id] = &record;
	if (types_.is_pack_expansion(type))
	{
		// 14.5.3p4 and `sp`: the pack-expansion operator over an expression,
		// which is what `Dp` is over a type - so `S<Ns...>` is `1SIJXspT_EEE`.
		record.expression.kind = abi_mangle::ABI_EXPRESSION_PACK_EXPANSION;
		record.expression.expression_refs.push_back(
			expression_of(types_.target(type)));
		return placed->second;
	}
	record.expression.kind = abi_mangle::ABI_EXPRESSION_TEMPLATE_PARAMETER;
	record.expression.index = types_.template_index(type);
	// The ABI's compression registers a `<template-param>` written as a *type*,
	// and neither the expression nor the `X ... E` around it is a candidate of
	// its own - so `S<N>` written twice numbers its substitutions one lower than
	// `S<T>` written twice does.
	record.expression.substitutable = false;
	return placed->second;
}

const std::string& LocalContexts::context_of(const SemaEntity& function)
{
	const std::map<const SemaEntity*, std::string>::iterator held =
		ids_.find(&function);
	if (held != ids_.end())
	{
		return held->second;
	}
	// The identifier is recorded before the function's own name is built,
	// because that function may itself be a member of a class some other
	// function's body declared - and describing *it* walks this map again.
	const std::string id = "context" + decimal(ids_.size());
	const std::map<const SemaEntity*, std::string>::iterator placed =
		ids_.insert(std::make_pair(&function, id)).first;
	records_.push_back(abi_mangle::AbiDefinitionRecord());
	abi_mangle::AbiDefinitionRecord& record = records_.back();
	record.kind = abi_mangle::ABI_DEFINITION_CONTEXT;
	record.id = id;
	if (named_by_its_spelling(function))
	{
		// 3.5p9: the object file names the function by the spelling the program
		// wrote, so there is no encoding of a declaration for the context to
		// hold - what stands inside it is that spelling, with no parameter list
		// after it.
		record.context.kind = abi_mangle::ABI_CONTEXT_RAW;
		record.context.fragment = "Z" + source_name(function.name) + "E";
	}
	else
	{
		// 12.1: a constructor whose body declares a class has two entry points
		// and one declaration, and it is the declaration the class is named
		// after - so the complete-object name is the one that stands here.
		record.context.kind = abi_mangle::ABI_CONTEXT_FUNCTION;
		abi_mangle::AbiTargetRecord target;
		build_function_name(function, types_, kCompleteObjectAbi, *this, target,
		                    record.context.records);
		record.context.function = target.function;
	}
	map_[id] = &record;
	return placed->second;
}

}  // namespace

namespace
{

// 14.7.3p5: a class the program wrote out with `template<>` is no instantiation
// of anything - its body is unrelated to the pattern's and its members are
// declared and defined the way a normal class's are - so the classes a
// declaration is named through say "an instantiation made this" only where the
// specialization is one a *reading of the pattern* made.
bool made_by_an_instantiation(const SemaEntity& owner, TypeTable& types)
{
	return types.is_specialization(owner.type) && !owner.explicit_specialization;
}

}  // namespace

bool abi_instantiated_class(const SemaEntity& entity, TypeTable& types)
{
	if (made_by_an_instantiation(entity, types))
	{
		return true;
	}
	const std::vector<const SemaEntity*> owners = owning_classes(entity);
	for (std::size_t index = 0; index < owners.size(); ++index)
	{
		if (made_by_an_instantiation(*owners[index], types))
		{
			return true;
		}
	}
	return false;
}

bool abi_instantiated(const SemaEntity& entity, TypeTable& types)
{
	// 14.7.1p1: a function template's specialization is the body read again
	// for the arguments that named it, which is a definition of the *template*
	// and not of this unit.  14.7.3p1's is the other way round: `template<>`
	// wrote it out here, so it is this unit's own definition and the program's
	// one, and 14.7.3p6 leaves it `inline` only where its own declaration says
	// so.
	if (entity.kind == SemaKind::Function && entity.primary != nullptr &&
	    entity.primary->template_parameters != nullptr &&
	    !entity.explicit_specialization)
	{
		return true;
	}
	// 14.2: a member of a class a template-id named is made where a use asks
	// for it, so the classes the declaration is named through are what say it -
	// and 14.7.3p5's class is what none of them is, because the program wrote
	// its body out and this unit owes the member exactly what an ordinary
	// class's member owes.
	const std::vector<const SemaEntity*> owners = owning_classes(entity);
	for (std::size_t index = 0; index < owners.size(); ++index)
	{
		if (made_by_an_instantiation(*owners[index], types))
		{
			return true;
		}
	}
	return false;
}

std::string abi_symbol_of(const SemaEntity& entity, TypeTable& types,
                          unsigned variant)
{
	// 1.4p8: a reserved function is the implementation's own, and the name the
	// object file gives it is the one the implementation's runtime defines -
	// not an encoding of the declaration the analysis made to describe it.
	if (entity.builtin != kNotBuiltin)
	{
		// 3.7.4.1p2: the four allocation and deallocation functions are named
		// by what the implementation calls them, which is not a name any
		// spelling of the declaration derives - a program that replaces one
		// writes `operator delete` and defines this symbol.
		switch (entity.builtin)
		{
		case kBuiltinOperatorNew:
			return "cppgm_builtin_operator_new";

		case kBuiltinOperatorNewArray:
			return "cppgm_builtin_operator_new_array";

		case kBuiltinOperatorDelete:
			return "cppgm_builtin_operator_delete";

		case kBuiltinOperatorDeleteArray:
			return "cppgm_builtin_operator_delete_array";

		default:
			break;
		}
		return "cppgm_builtin_" + entity.name.substr(sizeof("__builtin_") - 1);
	}
	if (named_by_its_spelling(entity))
	{
		return entity.name;
	}
	LocalContexts contexts(types);
	abi_mangle::AbiTargetRecord target;
	std::vector<abi_mangle::AbiFunctionRecord> records;
	if (entity.kind != SemaKind::Function)
	{
		target.kind = abi_mangle::ABI_TARGET_FACT_VARIABLE;
		target.qualified_name = abi_qualified_name(entity);
		target.internal_linkage = entity.internal_linkage;
		// 9.4.2p1 and 14.2: a static data member of a specialization is named
		// through the template and the arguments that made it, which no
		// spelling of the name can be split back into.
		build_data_name(entity, types, contexts, target, records);
		return abi_mangle::mangle_target(target, records,
		                                 contexts.definitions());
	}
	build_function_name(entity, types, variant, contexts, target, records);
	return abi_mangle::mangle_target(target, records, contexts.definitions());
}
