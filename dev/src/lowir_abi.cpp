#include "lowir_abi.h"

#include <deque>
#include <map>
#include <sstream>

#include "abi_mangle.h"
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

	const abi_mangle::AbiDefinitionMap& definitions() const { return map_; }

private:
	TypeTable& types_;
	// The records the map points at.  A deque is what holds them: describing
	// one function may describe the function whose body declared *its* class,
	// and a reference already handed out has to survive that.
	std::deque<abi_mangle::AbiDefinitionRecord> records_;
	std::map<const SemaEntity*, std::string> ids_;
	std::map<TypeId, std::string> arguments_;
	abi_mangle::AbiDefinitionMap map_;
};

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
		// 14.1p2 and `<template-param>`: a specialization's own name is
		// encoded from the template's signature, where the parameter stands
		// for itself and is written by its place among the ones its head
		// declared.
		AbiType out;
		out.kind = abi_mangle::ABI_TYPE_TEMPLATE_PARAMETER;
		out.index = types.template_index(type);
		return out;
	}

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
			const std::vector<TypeId>& arguments = types.template_arguments(type);
			for (std::size_t index = 0; index < arguments.size(); ++index)
			{
				out.argument_refs.push_back(contexts.argument_of(arguments[index]));
			}
			return out;
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
std::vector<std::string> name_components(const std::string& spelled)
{
	std::vector<std::string> parts;
	std::size_t at = 0;
	while (true)
	{
		const std::size_t next = spelled.find("::", at);
		if (next == std::string::npos)
		{
			parts.push_back(spelled.substr(at));
			return parts;
		}
		parts.push_back(spelled.substr(at, next - at));
		at = next + 2;
	}
}

// 14.2: the classes a declaration is named through, innermost first.
//
// A component of an encoded name is a source name unless the class it names is
// a specialization, which the ABI writes as the template's name and then the
// arguments - so the walk that builds the name has to ask the class rather
// than the spelling, which cannot be split back into the two.  The regions
// above the outermost class are namespaces and say nothing about templates.
std::vector<const SemaEntity*> owning_classes(const SemaEntity& entity)
{
	std::vector<const SemaEntity*> owners;
	for (const Scope* at = entity.region; at != nullptr; at = at->parent)
	{
		if (at->kind == ScopeKind::TemplateParameters)
		{
			// 14.1p1: the region a specialization's bindings stand in encloses
			// the class alone and names nothing.
			continue;
		}
		if (at->kind != ScopeKind::Class || at->owner == nullptr)
		{
			break;
		}
		owners.push_back(at->owner);
	}
	return owners;
}

// Which of the components of a qualified name that class stands at, or the
// count itself when the name has none: `components` ends in the declaration's
// own name, and the classes are the components before it, innermost last.
bool specialized_component(const std::vector<const SemaEntity*>& owners,
                           TypeTable& types, std::size_t index,
                           std::size_t components, const SemaEntity*& owner)
{
	if (index + 1 >= components)
	{
		return false;
	}
	const std::size_t depth = components - 2 - index;
	if (depth >= owners.size())
	{
		return false;
	}
	owner = owners[depth];
	return types.is_specialization(owner->type);
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
	const std::vector<const SemaEntity*> owners = owning_classes(entity);
	bool specialized = false;
	for (std::size_t index = 0; index < owners.size(); ++index)
	{
		specialized = specialized || types.is_specialization(owners[index]->type);
	}
	if (!specialized)
	{
		return;
	}
	const std::vector<std::string> components =
		name_components(abi_qualified_name(entity));
	target.function.kind = abi_mangle::ABI_FUNCTION_TARGET_ENCODING;
	for (std::size_t index = 0; index + 1 < components.size(); ++index)
	{
		abi_mangle::AbiFunctionRecord region;
		const SemaEntity* owner = nullptr;
		if (specialized_component(owners, types, index, components.size(), owner))
		{
			region.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_TEMPLATE;
			region.name =
				name_components(types.template_name(owner->type)).back();
			const std::vector<TypeId>& arguments =
				types.template_arguments(owner->type);
			for (std::size_t at = 0; at < arguments.size(); ++at)
			{
				region.argument_refs.push_back(contexts.argument_of(arguments[at]));
			}
		}
		else
		{
			region.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_SOURCE;
			region.name = components[index];
		}
		records.push_back(region);
	}
	// The encoder drops the last *name* component where a terminal names the
	// entity, so the components before it stay the regions they are.
	abi_mangle::AbiFunctionRecord placeholder;
	placeholder.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_SOURCE;
	placeholder.name = entity.name;
	records.push_back(placeholder);
	abi_mangle::AbiFunctionRecord written;
	written.kind = abi_mangle::ABI_FUNCTION_RECORD_TERMINAL_SOURCE;
	written.source_name = components.back();
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
	const std::vector<std::string> components =
		name_components(abi_qualified_name(entity));
	// 9.8p1: a member of a class a function's body declared is named through
	// that class and the class through the function, so the whole name stands
	// inside the context and the regions the definition was written in say
	// nothing at all.
	const bool local = entity.local_function != nullptr && components.size() > 1;
	// 14.2: a member of a specialization is named through the template and the
	// arguments that made it, which no path built from one spelling can write.
	const std::vector<const SemaEntity*> owners = owning_classes(entity);
	bool specialized = false;
	for (std::size_t index = 0; index + 1 < components.size(); ++index)
	{
		const SemaEntity* owner = nullptr;
		specialized = specialized ||
			specialized_component(owners, types, index, components.size(), owner);
	}
	if (named_by_terminal || local || specialized)
	{
		// 13.5: an operator function is named by the operator it overloads,
		// which the encoding spells as its own terminal rather than as a source
		// name.  The regions around the declaration still precede it, with one
		// component standing where that terminal goes.
		target.function.kind = abi_mangle::ABI_FUNCTION_TARGET_ENCODING;
		for (std::size_t index = 0; index + 1 < components.size(); ++index)
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
					region.source_name = components[index];
					region.discriminator = decimal(entity.local_occurrence);
				}
			}
			else
			{
				const SemaEntity* owner = nullptr;
				if (specialized_component(owners, types, index,
				                          components.size(), owner))
				{
					// The record stands for one component, and the regions
					// around the template were written by the ones before it.
					region.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_TEMPLATE;
					region.name =
						name_components(types.template_name(owner->type)).back();
					const std::vector<TypeId>& arguments =
						types.template_arguments(owner->type);
					for (std::size_t at = 0; at < arguments.size(); ++at)
					{
						region.argument_refs.push_back(
							contexts.argument_of(arguments[at]));
					}
				}
				else
				{
					region.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_SOURCE;
					region.name = components[index];
				}
			}
			records.push_back(region);
		}
		if (components.size() > 1)
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
			written.kind = abi_mangle::ABI_FUNCTION_RECORD_CONVERSION_TERMINAL;
			written.type = abi_type(types, types.target(entity.type), contexts);
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
			written.source_name = components.back();
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
		const std::vector<TypeId>& arguments =
			types.type_list_at(entity.template_arguments);
		for (std::size_t index = 0; index < arguments.size(); ++index)
		{
			written.argument_refs.push_back(contexts.argument_of(arguments[index]));
		}
		records.push_back(written);
		// The ABI writes a function template's result type, because two
		// specializations of one template can differ in it alone.
		abi_mangle::AbiFunctionRecord result;
		result.kind = abi_mangle::ABI_FUNCTION_RECORD_RESULT;
		result.type = abi_type(types, types.target(templated->type), contexts);
		records.push_back(result);
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
	record.template_argument.kind = abi_mangle::ABI_TEMPLATE_ARGUMENT_TYPE;
	map_[id] = &record;
	record.template_argument.type = abi_type(types_, type, *this);
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
