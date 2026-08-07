#include "lowir_abi.h"

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

AbiType abi_type(TypeTable& types, TypeId type);

AbiType wrapped(abi_mangle::AbiTypeKind kind, TypeTable& types,
                TypeId inner)
{
	AbiType out;
	out.kind = kind;
	out.types.push_back(abi_type(types, inner));
	return out;
}

// One C++ type as the ABI facts that describe it.  3.9.3p2 makes a
// cv-qualifier on an array a qualifier of its elements, which is where the
// type model already holds it, so the walk needs no special case for it.
AbiType abi_type(TypeTable& types, TypeId type)
{
	const unsigned cv = types.cv(type);
	if (cv != kCvNone)
	{
		AbiType out;
		out.kind = abi_mangle::ABI_TYPE_CV;
		out.is_const = (cv & kCvConst) != 0;
		out.is_volatile = (cv & kCvVolatile) != 0;
		out.types.push_back(abi_type(types, types.strip_cv(type)));
		return out;
	}
	switch (types.kind(type))
	{
	case TypeKind::Pointer:
		return wrapped(abi_mangle::ABI_TYPE_POINTER, types, types.target(type));

	case TypeKind::LValueReference:
		return wrapped(abi_mangle::ABI_TYPE_LVALUE_REFERENCE, types,
		               types.target(type));

	case TypeKind::RValueReference:
		return wrapped(abi_mangle::ABI_TYPE_RVALUE_REFERENCE, types,
		               types.target(type));

	case TypeKind::Array:
	{
		AbiType out = wrapped(abi_mangle::ABI_TYPE_ARRAY, types,
		                      types.target(type));
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
		out.types.push_back(abi_type(types, types.target(type)));
		const std::vector<TypeId>& parameters = types.parameters(type);
		for (std::size_t index = 0; index < parameters.size(); ++index)
		{
			out.types.push_back(abi_type(types, parameters[index]));
		}
		return out;
	}

	case TypeKind::MemberPointer:
	{
		AbiType out;
		out.kind = abi_mangle::ABI_TYPE_MEMBER_POINTER;
		out.types.push_back(abi_type(types, types.member_class(type)));
		out.types.push_back(abi_type(types, types.target(type)));
		return out;
	}

	case TypeKind::Class:
	case TypeKind::Enum:
	{
		// 3.4.3: the type is named from outside every region that encloses its
		// declaration, which is the spelling an object-file name carries.
		AbiType out;
		out.kind = abi_mangle::ABI_TYPE_NAMED;
		out.name = types.user_qualified_name(type);
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

std::string abi_symbol_of(const SemaEntity& entity, TypeTable& types,
                          unsigned variant)
{
	// 1.4p8: a reserved function is the implementation's own, and the name the
	// object file gives it is the one the implementation's runtime defines -
	// not an encoding of the declaration the analysis made to describe it.
	if (entity.builtin != kNotBuiltin)
	{
		return "cppgm_builtin_" + entity.name.substr(sizeof("__builtin_") - 1);
	}
	// 7.5p6 and 3.6.1p1: a name another translation unit reaches by its C
	// spelling is that spelling, and `main` is the one C++ function every
	// implementation names the same way.  Neither is encoded.
	if ((entity.c_linkage && !entity.internal_linkage) ||
	    (entity.kind == SemaKind::Function && entity.dump_name == "main"))
	{
		return entity.name;
	}
	abi_mangle::AbiTargetRecord target;
	std::vector<abi_mangle::AbiFunctionRecord> records;
	if (entity.kind != SemaKind::Function)
	{
		target.kind = abi_mangle::ABI_TARGET_FACT_VARIABLE;
		target.qualified_name = abi_qualified_name(entity);
		target.internal_linkage = entity.internal_linkage;
		return abi_mangle::mangle_target(target, records,
		                                 abi_mangle::AbiDefinitionMap());
	}
	target.kind = abi_mangle::ABI_TARGET_FACT_FUNCTION;
	const std::vector<TypeId>& parameters = types.parameters(entity.type);
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
	if (special ||
	    (entity.name.compare(0, 8, "operator") == 0 &&
	     operator_terminal(entity.name.substr(8), parameters.size(), terminal,
	                       literal_suffix)))
	{
		// 13.5: an operator function is named by the operator it overloads,
		// which the encoding spells as its own terminal rather than as a source
		// name.  The regions around the declaration still precede it, with one
		// component standing where that terminal goes.
		target.function.kind = abi_mangle::ABI_FUNCTION_TARGET_ENCODING;
		const std::string& spelled = abi_qualified_name(entity);
		std::size_t at = 0;
		while (true)
		{
			const std::size_t next = spelled.find("::", at);
			if (next == std::string::npos)
			{
				break;
			}
			abi_mangle::AbiFunctionRecord region;
			region.kind = abi_mangle::ABI_FUNCTION_RECORD_NAME_SOURCE;
			region.name = spelled.substr(at, next - at);
			records.push_back(region);
			at = next + 2;
		}
		if (!records.empty())
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
				: (variant == kBaseObjectAbi ? "destructor-base"
				                             : "destructor-complete");
		}
		else
		{
			written.kind = abi_mangle::ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
			written.terminal = terminal;
			written.literal_suffix = literal_suffix;
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
	}
	for (std::size_t index = first; index < parameters.size(); ++index)
	{
		abi_mangle::AbiFunctionRecord parameter;
		parameter.kind = abi_mangle::ABI_FUNCTION_RECORD_PARAMETER;
		parameter.type = abi_type(types, parameters[index]);
		records.push_back(parameter);
	}
	if (types.variadic(entity.type))
	{
		abi_mangle::AbiFunctionRecord ellipsis;
		ellipsis.kind = abi_mangle::ABI_FUNCTION_RECORD_VARIADIC;
		records.push_back(ellipsis);
	}
	return abi_mangle::mangle_target(target, records,
	                                 abi_mangle::AbiDefinitionMap());
}
