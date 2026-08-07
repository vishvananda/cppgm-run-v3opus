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
//
// The sequence-level facts - its encoding-prefix, its ud-suffix and whether the
// parts disagree about either - are folded in as each part arrives, so they are
// held once for the whole sequence rather than once per part, and building the
// token needs only the pass that encodes the bodies.
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
	// All that is left to do with a part once the sequence has been resolved:
	// where its body is in `source_`, and whether that body is spelled raw.
	struct Part
	{
		std::size_t body_begin;
		std::size_t body_end;
		bool raw;
	};

	void note_encoding(LiteralEncoding encoding);
	void note_suffix(std::size_t begin, std::size_t end);
	bool encode_part(const Part& part, std::size_t unit_size, std::string& data) const;
	bool encode_narrow_part(const Part& part, std::string& data) const;
	bool encode_wide_part(const Part& part, std::size_t unit_size, std::string& data) const;
	void take_source(PostToken& token);
	void clear();

	std::string source_;
	std::vector<Part> parts_;
	// See 2.14.5.13 and 2.14.8.8, as the course defines them: the parts may
	// name at most one encoding-prefix and at most one ud-suffix between them,
	// and a ud-suffix that does not start with `_` is reserved.
	LiteralEncoding encoding_;
	bool prefixed_;
	bool conflict_;
	std::size_t suffix_begin_;
	std::size_t suffix_end_;
};

// Which of the literals 2.14p1 lists a terminal was written as.
enum class LiteralForm
{
	None,
	Number,
	Character,
	String
};

// Analyses one literal terminal of a parse, whatever form it was written in.
//
// A layer that reads a parsed terminal has only its spelling, and the token
// stream spells every literal the one way, so which of 2.14p1's literals it is
// has to be asked again - of phase 3, which answered it once already.  So the
// spelling is lexed back into the pp-tokens it was read from and the first of
// them says the form.  No character of the spelling answers it on its own: a
// character-literal may hold a `"`, a floating-literal may begin with its `.`,
// and either may be written after an encoding-prefix.
//
// 2.14.5p12's sequence is rebuilt from its parts rather than from the one
// string they were joined into, because what separates two of them is a token
// boundary the joined text no longer has.  `None`, with `token` left as it was,
// for a spelling that is no literal.
LiteralForm scan_literal(const std::string& spelling, PostToken& token);
