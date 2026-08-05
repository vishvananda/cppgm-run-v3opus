#pragma once

#include <cstddef>
#include <vector>

#include "parse_token.h"

// A position in a token sequence, with the one piece of bracket state the
// grammar reads back.
//
// C++ tokens nest correctly in `()`, `[]` and `{}`, but a close-angle-bracket
// is spelled like an operator, so 14.2.3 decides between the two by which
// bracket pair is innermost: while that pair is `<>`, the next `>` closes it
// and is not an operator.  Every bracket pair is opened and closed inside one
// rule, so `angle` is saved and restored by the same `Mark` that undoes a
// failed alternative, and a rule always leaves it as it found it.
class ParseCursor
{
public:
	explicit ParseCursor(const std::vector<ParseToken>& tokens)
		: tokens_(tokens)
		, pos_(0)
		, angle_(false)
	{}

protected:
	// The parser state a failed alternative has to undo.
	struct Mark
	{
		std::size_t pos;
		bool angle;
	};

	unsigned peek(std::size_t offset = 0) const
	{
		const std::size_t index = pos_ + offset;
		return index < tokens_.size() ? tokens_[index].type : unsigned(ST_EOF);
	}

	unsigned flags_at(std::size_t offset = 0) const
	{
		const std::size_t index = pos_ + offset;
		return index < tokens_.size() ? tokens_[index].flags : 0u;
	}

	bool at(unsigned type) const { return peek() == type; }

	// True when the current token is an identifier in the given mock name
	// category, or a literal with the given spelling fact.
	bool at_identifier(unsigned flag) const
	{
		return at(TT_IDENTIFIER) && (flags_at() & flag) != 0;
	}

	bool at_literal(unsigned flag) const
	{
		return at(TT_LITERAL) && (flags_at() & flag) != 0;
	}

	bool accept(unsigned type)
	{
		if (!at(type))
		{
			return false;
		}
		++pos_;
		return true;
	}

	// Consumes an opening `(`, `[` or `{`, which makes the innermost pair a
	// non-angle one for as long as it is open.
	bool open_bracket(unsigned type)
	{
		if (!accept(type))
		{
			return false;
		}
		angle_ = false;
		return true;
	}

	Mark mark() const
	{
		Mark saved;
		saved.pos = pos_;
		saved.angle = angle_;
		return saved;
	}

	void reset(const Mark& saved)
	{
		pos_ = saved.pos;
		angle_ = saved.angle;
	}

	bool fail(const Mark& saved)
	{
		reset(saved);
		return false;
	}

	const std::vector<ParseToken>& tokens_;
	std::size_t pos_;
	bool angle_;
};
