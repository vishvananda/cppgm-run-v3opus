#include "token_model.h"

#include <cstdio>
#include <cstring>

namespace
{

// Every spelling that is a simple token, paired with the token type it takes:
// the 2.12 keywords stated here, and the 2.13 operators and punctuators from
// the two lists phase 3 shares, minus the four it reserves for preprocessing.
#define CPPGM_SIMPLE_TOKEN_SPELLINGS(X) \
	CPPGM_IDENTIFIER_LIKE_OPERATORS(X) \
	CPPGM_PUNCTUATION_OPERATORS(X) \
	X("alignas", KW_ALIGNAS) X("alignof", KW_ALIGNOF) X("asm", KW_ASM) \
	X("auto", KW_AUTO) X("bool", KW_BOOL) X("break", KW_BREAK) \
	X("case", KW_CASE) X("catch", KW_CATCH) X("char", KW_CHAR) \
	X("char16_t", KW_CHAR16_T) X("char32_t", KW_CHAR32_T) X("class", KW_CLASS) \
	X("const", KW_CONST) X("constexpr", KW_CONSTEXPR) X("const_cast", KW_CONST_CAST) \
	X("continue", KW_CONTINUE) X("decltype", KW_DECLTYPE) X("default", KW_DEFAULT) \
	X("do", KW_DO) X("double", KW_DOUBLE) \
	X("dynamic_cast", KW_DYNAMIC_CAST) X("else", KW_ELSE) X("enum", KW_ENUM) \
	X("explicit", KW_EXPLICIT) X("export", KW_EXPORT) X("extern", KW_EXTERN) \
	X("false", KW_FALSE) X("float", KW_FLOAT) X("for", KW_FOR) \
	X("friend", KW_FRIEND) X("goto", KW_GOTO) X("if", KW_IF) \
	X("inline", KW_INLINE) X("int", KW_INT) X("long", KW_LONG) \
	X("mutable", KW_MUTABLE) X("namespace", KW_NAMESPACE) \
	X("noexcept", KW_NOEXCEPT) X("nullptr", KW_NULLPTR) X("operator", KW_OPERATOR) \
	X("private", KW_PRIVATE) X("protected", KW_PROTECTED) X("public", KW_PUBLIC) \
	X("register", KW_REGISTER) X("reinterpret_cast", KW_REINTERPET_CAST) \
	X("return", KW_RETURN) X("short", KW_SHORT) X("signed", KW_SIGNED) \
	X("sizeof", KW_SIZEOF) X("static", KW_STATIC) X("static_assert", KW_STATIC_ASSERT) \
	X("static_cast", KW_STATIC_CAST) X("struct", KW_STRUCT) X("switch", KW_SWITCH) \
	X("template", KW_TEMPLATE) X("this", KW_THIS) X("thread_local", KW_THREAD_LOCAL) \
	X("throw", KW_THROW) X("true", KW_TRUE) X("try", KW_TRY) \
	X("typedef", KW_TYPEDEF) X("typeid", KW_TYPEID) X("typename", KW_TYPENAME) \
	X("union", KW_UNION) X("unsigned", KW_UNSIGNED) X("using", KW_USING) \
	X("virtual", KW_VIRTUAL) X("void", KW_VOID) X("volatile", KW_VOLATILE) \
	X("wchar_t", KW_WCHAR_T) X("while", KW_WHILE)

struct SimpleTokenEntry
{
	const char* text;
	std::size_t size;
	ETokenType type;
};

// Open addressing in a power-of-two table: an identifier that is not a keyword
// is rejected on its first probe, which is the case that runs per token of a
// real source file.
const std::size_t kSlotCount = 512;

unsigned int spelling_hash(const char* text, std::size_t size)
{
	unsigned int hash = 2166136261u;
	for (std::size_t index = 0; index < size; ++index)
	{
		hash ^= static_cast<unsigned char>(text[index]);
		hash *= 16777619u;
	}
	return hash;
}

struct SimpleTokenTable
{
	SimpleTokenTable()
	{
		for (std::size_t slot = 0; slot < kSlotCount; ++slot)
		{
			slots[slot].text = 0;
			slots[slot].size = 0;
			slots[slot].type = kTokenTypeCount;
		}
#define CPPGM_SIMPLE_TOKEN_INSERT(spelling, token_type) \
		insert(spelling, sizeof(spelling) - 1, token_type);
		CPPGM_SIMPLE_TOKEN_SPELLINGS(CPPGM_SIMPLE_TOKEN_INSERT)
#undef CPPGM_SIMPLE_TOKEN_INSERT
	}

	void insert(const char* text, std::size_t size, ETokenType type)
	{
		std::size_t slot = spelling_hash(text, size) & (kSlotCount - 1);
		while (slots[slot].text != 0)
		{
			slot = (slot + 1) & (kSlotCount - 1);
		}
		slots[slot].text = text;
		slots[slot].size = size;
		slots[slot].type = type;
	}

	SimpleTokenEntry slots[kSlotCount];
};

const SimpleTokenTable& simple_token_table()
{
	static const SimpleTokenTable table;
	return table;
}

const char* const kFundamentalTypeNames[] =
{
#define CPPGM_FUNDAMENTAL_TYPE_NAME(name, text, size, kind) text,
	CPPGM_FUNDAMENTAL_TYPES(CPPGM_FUNDAMENTAL_TYPE_NAME)
#undef CPPGM_FUNDAMENTAL_TYPE_NAME
};

const unsigned char kFundamentalTypeSizes[] =
{
#define CPPGM_FUNDAMENTAL_TYPE_SIZE(name, text, size, kind) size,
	CPPGM_FUNDAMENTAL_TYPES(CPPGM_FUNDAMENTAL_TYPE_SIZE)
#undef CPPGM_FUNDAMENTAL_TYPE_SIZE
};

const FundamentalTypeClass kFundamentalTypeClasses[] =
{
#define CPPGM_FUNDAMENTAL_TYPE_CLASS(name, text, size, kind) FundamentalTypeClass::kind,
	CPPGM_FUNDAMENTAL_TYPES(CPPGM_FUNDAMENTAL_TYPE_CLASS)
#undef CPPGM_FUNDAMENTAL_TYPE_CLASS
};

const char* const kTokenTypeNames[] =
{
#define CPPGM_TOKEN_TYPE_NAME(name) #name,
	CPPGM_TOKEN_TYPES(CPPGM_TOKEN_TYPE_NAME)
#undef CPPGM_TOKEN_TYPE_NAME
};

} // namespace

const char* fundamental_type_name(EFundamentalType type)
{
	return kFundamentalTypeNames[type];
}

std::size_t fundamental_type_size(EFundamentalType type)
{
	return kFundamentalTypeSizes[type];
}

FundamentalTypeClass fundamental_type_class(EFundamentalType type)
{
	return kFundamentalTypeClasses[type];
}

const char* token_type_name(ETokenType type)
{
	return kTokenTypeNames[type];
}

std::string spell_floating_value(EFundamentalType type, long double value)
{
	// 4.8p1: the value is what the object of `type` holds, so it is rounded to
	// that width before it is spelled - `16777217.0` written for a `float` is
	// the `float` beside it and not the `double` the digits name.
	char digits[64];
	if (type == FT_FLOAT)
	{
		std::snprintf(digits, sizeof digits, "%.*g", kFloatingSpelledDigits,
		              static_cast<double>(static_cast<float>(value)));
	}
	else if (type == FT_LONG_DOUBLE)
	{
		std::snprintf(digits, sizeof digits, "%.*Lg", kFloatingSpelledDigits,
		              value);
	}
	else
	{
		std::snprintf(digits, sizeof digits, "%.*g", kFloatingSpelledDigits,
		              static_cast<double>(value));
	}
	return digits;
}

bool lookup_simple_token(const char* text, std::size_t size, ETokenType& type)
{
	// No simple token is empty or longer than `reinterpret_cast`, so most
	// identifiers never reach the table at all.
	if (size == 0 || size > 16)
	{
		return false;
	}

	const SimpleTokenTable& table = simple_token_table();
	std::size_t slot = spelling_hash(text, size) & (kSlotCount - 1);
	while (table.slots[slot].text != 0)
	{
		const SimpleTokenEntry& entry = table.slots[slot];
		if (entry.size == size && std::memcmp(entry.text, text, size) == 0)
		{
			type = entry.type;
			return true;
		}
		slot = (slot + 1) & (kSlotCount - 1);
	}
	return false;
}
