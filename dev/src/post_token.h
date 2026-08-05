#pragma once

#include <cstddef>
#include <string>

#include "token_model.h"

// One analysed token: the result of the tokenization part of translation
// phase 7, as a typed fact rather than as output text.
//
// `source` is the PA1 token data of the preprocessing-token the token came
// from, and for a concatenated string-literal sequence it is the space
// separated list of the sources of its parts.  Its buffers are reused from
// token to token, so a driver should keep one PostToken and refill it.

enum class PostTokenKind
{
	Invalid,
	Simple,
	Identifier,
	Literal,
	LiteralArray,
	UserDefinedLiteral,
	EndOfFile
};

enum class UserDefinedLiteralKind
{
	Integer,
	Floating,
	Character,
	String
};

struct PostToken
{
	PostTokenKind kind;
	std::string source;

	// Simple.
	ETokenType simple_type;

	// Literal, LiteralArray, and the character and string user-defined
	// literals: the type of the value, and its object representation in the
	// course ABI.  `element_count` counts array elements, including the
	// terminating null character of a string.
	EFundamentalType type;
	std::size_t element_count;
	std::string data;

	// UserDefinedLiteral.  For the integer and floating kinds the prefix is
	// the first `ud_prefix_size` bytes of `source`.
	UserDefinedLiteralKind ud_kind;
	std::string ud_suffix;
	std::size_t ud_prefix_size;

	PostToken()
		: kind(PostTokenKind::EndOfFile)
		, simple_type(kTokenTypeCount)
		, type(FT_INT)
		, element_count(0)
		, ud_kind(UserDefinedLiteralKind::Integer)
		, ud_prefix_size(0)
	{}

	// Clears everything but `source`, which every kind carries and which the
	// caller has usually just filled in.
	void reset(PostTokenKind new_kind)
	{
		kind = new_kind;
		simple_type = kTokenTypeCount;
		type = FT_INT;
		element_count = 0;
		data.clear();
		ud_kind = UserDefinedLiteralKind::Integer;
		ud_suffix.clear();
		ud_prefix_size = 0;
	}

	// Stores `value` as the object representation of `value_type`, truncated
	// to its size and byte-wise little-endian.
	void set_integer_value(EFundamentalType value_type, unsigned long long value)
	{
		type = value_type;
		data.clear();
		const std::size_t size = fundamental_type_size(value_type);
		for (std::size_t index = 0; index < size; ++index)
		{
			data.push_back(static_cast<char>(value & 0xFF));
			value >>= 8;
		}
	}
};
