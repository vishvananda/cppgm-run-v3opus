#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "literal_scan.h"
#include "post_token.h"

// Translation phase 6: a maximal sequence of string-literals and
// user-defined-string-literals is one token.
//
// The sequence is accumulated as it is lexed, because the encoding of the
// whole sequence decides how each part's body is encoded, and a numeric escape
// is only in range once that encoding is known.  Every part is appended to one
// buffer that is also the token's source, so no spelling is copied twice and a
// sequence of n parts costs the same per part as n sequences of one.
class StringLiteralSequence
{
public:
	StringLiteralSequence();

	bool empty() const { return parts_.empty(); }

	// Adds one string-literal or user-defined-string-literal spelling.
	void add(const std::string& spelling);

	// Builds the token for the whole sequence and empties the sequence.  The
	// token is Invalid when the parts disagree on their encoding-prefix or
	// ud-suffix, when a ud-suffix is reserved, or when a numeric escape does
	// not fit one code unit of the resolved encoding.
	void build(PostToken& token);

	// True for the one sequence that is two tokens rather than one: a lone
	// `""` with a reserved ud-suffix, which after `operator` is the
	// literal-operator-id `operator "" identifier`.
	bool is_reserved_empty_suffix() const;

	// Splits that form: `token` becomes the `""` literal and `identifier` its
	// suffix.  Empties the sequence.
	void build_literal_operator_id(PostToken& token, std::string& identifier);

private:
	struct Part
	{
		std::size_t body_begin;
		std::size_t body_end;
		std::size_t suffix_begin;
		std::size_t suffix_end;
		LiteralEncoding encoding;
		bool raw;
	};

	bool resolve(LiteralEncoding& encoding, std::size_t& suffix_part) const;
	bool same_suffix(const Part& left, const Part& right) const;
	bool encode_part(const Part& part, std::size_t unit_size, std::string& data) const;
	bool encode_narrow_part(const Part& part, std::string& data) const;
	bool encode_wide_part(const Part& part, std::size_t unit_size, std::string& data) const;
	void take_source(PostToken& token);

	std::string source_;
	std::vector<Part> parts_;
};
