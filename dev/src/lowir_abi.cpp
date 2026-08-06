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
                       std::string& terminal)
{
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

std::string abi_symbol_of(const SemaEntity& entity, TypeTable& types)
{
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
		target.qualified_name = entity.dump_name;
		target.internal_linkage = entity.internal_linkage;
		return abi_mangle::mangle_target(target, records,
		                                 abi_mangle::AbiDefinitionMap());
	}
	target.kind = abi_mangle::ABI_TARGET_FACT_FUNCTION;
	const std::vector<TypeId>& parameters = types.parameters(entity.type);
	std::string terminal;
	if (entity.name.compare(0, 8, "operator") == 0 &&
	    operator_terminal(entity.name.substr(8), parameters.size(), terminal))
	{
		// 13.5: an operator function is named by the operator it overloads,
		// which the encoding spells as its own terminal rather than as a source
		// name.  The regions around the declaration still precede it, with one
		// component standing where that terminal goes.
		target.function.kind = abi_mangle::ABI_FUNCTION_TARGET_ENCODING;
		const std::string& spelled = entity.dump_name;
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
		written.kind = abi_mangle::ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
		written.terminal = terminal;
		records.push_back(written);
	}
	else
	{
		target.function.kind = abi_mangle::ABI_FUNCTION_TARGET_PATH;
		target.function.qualified_name = entity.dump_name;
	}
	for (std::size_t index = 0; index < parameters.size(); ++index)
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
