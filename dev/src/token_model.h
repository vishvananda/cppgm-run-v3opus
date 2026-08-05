#pragma once

#include <cstddef>
#include <string>

// The token vocabulary shared by the assignments: the fundamental types of
// 3.9.1 with the sizes the course ABI gives them, and the keyword, operator and
// punctuator token types the assignments name.  Phase 7 uses all of it; phase 3
// takes the identifier-like operators of 2.13 from it, because that is the one
// fact the two phases share.
//
// Each enumeration and its name table are generated from one list, so a type,
// its spelling and its size cannot drift apart.

#define CPPGM_FUNDAMENTAL_TYPES(X) \
	X(FT_SIGNED_CHAR, "signed char", 1) \
	X(FT_SHORT_INT, "short int", 2) \
	X(FT_INT, "int", 4) \
	X(FT_LONG_INT, "long int", 8) \
	X(FT_LONG_LONG_INT, "long long int", 8) \
	X(FT_UNSIGNED_CHAR, "unsigned char", 1) \
	X(FT_UNSIGNED_SHORT_INT, "unsigned short int", 2) \
	X(FT_UNSIGNED_INT, "unsigned int", 4) \
	X(FT_UNSIGNED_LONG_INT, "unsigned long int", 8) \
	X(FT_UNSIGNED_LONG_LONG_INT, "unsigned long long int", 8) \
	X(FT_WCHAR_T, "wchar_t", 4) \
	X(FT_CHAR, "char", 1) \
	X(FT_CHAR16_T, "char16_t", 2) \
	X(FT_CHAR32_T, "char32_t", 4) \
	X(FT_BOOL, "bool", 1) \
	X(FT_FLOAT, "float", 4) \
	X(FT_DOUBLE, "double", 8) \
	X(FT_LONG_DOUBLE, "long double", 16) \
	X(FT_VOID, "void", 0) \
	X(FT_NULLPTR_T, "nullptr_t", 8)

enum EFundamentalType
{
#define CPPGM_FUNDAMENTAL_TYPE_ENUMERATOR(name, text, size) name,
	CPPGM_FUNDAMENTAL_TYPES(CPPGM_FUNDAMENTAL_TYPE_ENUMERATOR)
#undef CPPGM_FUNDAMENTAL_TYPE_ENUMERATOR
	kFundamentalTypeCount
};

// See 2.12 Keywords and 2.13 Operators and punctuators.  Alternative tokens
// share the token type of the primary spelling they stand for, so `and` and
// `&&` are both OP_LAND.
#define CPPGM_TOKEN_TYPES(X) \
	X(KW_ALIGNAS) X(KW_ALIGNOF) X(KW_ASM) X(KW_AUTO) X(KW_BOOL) \
	X(KW_BREAK) X(KW_CASE) X(KW_CATCH) X(KW_CHAR) X(KW_CHAR16_T) \
	X(KW_CHAR32_T) X(KW_CLASS) X(KW_CONST) X(KW_CONSTEXPR) X(KW_CONST_CAST) \
	X(KW_CONTINUE) X(KW_DECLTYPE) X(KW_DEFAULT) X(KW_DELETE) X(KW_DO) \
	X(KW_DOUBLE) X(KW_DYNAMIC_CAST) X(KW_ELSE) X(KW_ENUM) X(KW_EXPLICIT) \
	X(KW_EXPORT) X(KW_EXTERN) X(KW_FALSE) X(KW_FLOAT) X(KW_FOR) \
	X(KW_FRIEND) X(KW_GOTO) X(KW_IF) X(KW_INLINE) X(KW_INT) \
	X(KW_LONG) X(KW_MUTABLE) X(KW_NAMESPACE) X(KW_NEW) X(KW_NOEXCEPT) \
	X(KW_NULLPTR) X(KW_OPERATOR) X(KW_PRIVATE) X(KW_PROTECTED) X(KW_PUBLIC) \
	X(KW_REGISTER) X(KW_REINTERPET_CAST) X(KW_RETURN) X(KW_SHORT) X(KW_SIGNED) \
	X(KW_SIZEOF) X(KW_STATIC) X(KW_STATIC_ASSERT) X(KW_STATIC_CAST) X(KW_STRUCT) \
	X(KW_SWITCH) X(KW_TEMPLATE) X(KW_THIS) X(KW_THREAD_LOCAL) X(KW_THROW) \
	X(KW_TRUE) X(KW_TRY) X(KW_TYPEDEF) X(KW_TYPEID) X(KW_TYPENAME) \
	X(KW_UNION) X(KW_UNSIGNED) X(KW_USING) X(KW_VIRTUAL) X(KW_VOID) \
	X(KW_VOLATILE) X(KW_WCHAR_T) X(KW_WHILE) \
	X(OP_LBRACE) X(OP_RBRACE) X(OP_LSQUARE) X(OP_RSQUARE) X(OP_LPAREN) \
	X(OP_RPAREN) X(OP_BOR) X(OP_XOR) X(OP_COMPL) X(OP_AMP) \
	X(OP_LNOT) X(OP_SEMICOLON) X(OP_COLON) X(OP_DOTS) X(OP_QMARK) \
	X(OP_COLON2) X(OP_DOT) X(OP_DOTSTAR) X(OP_PLUS) X(OP_MINUS) \
	X(OP_STAR) X(OP_DIV) X(OP_MOD) X(OP_ASS) X(OP_LT) \
	X(OP_GT) X(OP_PLUSASS) X(OP_MINUSASS) X(OP_STARASS) X(OP_DIVASS) \
	X(OP_MODASS) X(OP_XORASS) X(OP_BANDASS) X(OP_BORASS) X(OP_LSHIFT) \
	X(OP_RSHIFT) X(OP_RSHIFTASS) X(OP_LSHIFTASS) X(OP_EQ) X(OP_NE) \
	X(OP_LE) X(OP_GE) X(OP_LAND) X(OP_LOR) X(OP_INC) \
	X(OP_DEC) X(OP_COMMA) X(OP_ARROWSTAR) X(OP_ARROW)

enum ETokenType
{
#define CPPGM_TOKEN_TYPE_ENUMERATOR(name) name,
	CPPGM_TOKEN_TYPES(CPPGM_TOKEN_TYPE_ENUMERATOR)
#undef CPPGM_TOKEN_TYPE_ENUMERATOR
	kTokenTypeCount
};

// The operators of 2.13 that are spelled the way an identifier is, with the
// token type each takes.  Phase 3 needs the same list to classify such a
// spelling as a preprocessing-op-or-punc rather than an identifier, so the list
// is stated here once and the phase 7 spelling table is built on top of it.
#define CPPGM_IDENTIFIER_LIKE_OPERATORS(X) \
	X("and", OP_LAND) X("and_eq", OP_BANDASS) X("bitand", OP_AMP) \
	X("bitor", OP_BOR) X("compl", OP_COMPL) X("delete", KW_DELETE) \
	X("new", KW_NEW) X("not", OP_LNOT) X("not_eq", OP_NE) \
	X("or", OP_LOR) X("or_eq", OP_BORASS) X("xor", OP_XOR) \
	X("xor_eq", OP_XORASS)

const char* fundamental_type_name(EFundamentalType type);

// Size in bytes under the course ABI.  Zero for void, which has no object
// representation.
std::size_t fundamental_type_size(EFundamentalType type);

const char* token_type_name(ETokenType type);

// The token type of a keyword, operator or punctuator spelling.  False for a
// spelling that is not a simple token, which includes the preprocessing-only
// operators `#`, `##`, `%:` and `%:%:`.
bool lookup_simple_token(const char* text, std::size_t size, ETokenType& type);

inline bool lookup_simple_token(const std::string& text, ETokenType& type)
{
	return lookup_simple_token(text.data(), text.size(), type);
}
