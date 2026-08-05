#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include "source_charset.h"

// Ill-formed source detected by a translation phase.  The message carries the
// source position; only the exit status is contractual, the text is for humans.
class SourceError : public std::runtime_error
{
public:
	explicit SourceError(const std::string& message)
		: std::runtime_error(message)
	{}
};

// Whether the bytes are a source file or text a later phase has produced.
//
// The spelling `##` joins in phase 4 is written in the characters phases 1 and
// 2 have already produced, so re-lexing it must not splice a trailing `\` or
// read a trigraph a second time.  A universal-character-name is still decoded:
// after phase 1 a character outside the basic source character set is spelled
// by one, so phase 3 is the phase that reads it.
enum class SourceForm
{
	Physical,
	Translated
};

// Translation phases 1 and 2 as a random access character source.
//
// The reader hands out one translated code point at a time, having applied
// UTF-8 decoding, trigraph replacement, line splicing and universal-character-
// name decoding in that order, plus the terminating new-line phase 2 adds to a
// file that does not end in one.
//
// A `\` is examined for a line splice first, then for a universal-character-
// name.  When neither matches, the `\` and the characters the failed name was
// spelled with are final: they take no further part in splicing or in a name of
// their own, which is what keeps `"\\u0041"` an escaped backslash.
//
// A raw-string-literal body is spelled in untranslated characters, so the
// reader can be switched into raw mode.  Because the consumption point is a
// byte offset into the original file, switching modes and rewinding are both
// exact: lookahead already computed in the other mode is simply recomputed.
class SourceReader
{
private:
	// One translated code point together with the span of the source file it
	// was spelled with, which is what makes the consumption point exact.
	struct Fetched
	{
		int code_point;
		bool invalid_escape_value;
		std::size_t begin;
		std::size_t end;
	};

public:
	explicit SourceReader(std::string bytes,
	                      SourceForm form = SourceForm::Physical);

	// Translated code point `ahead` positions past the consumption point, or
	// kEndOfFile.  `ahead` must be less than kLookaheadCapacity.
	//
	// Every phase asks for and consumes one code point at a time, so the buffer
	// hit is inline here and only refilling it is a call.
	int peek(std::size_t ahead = 0)
	{
		if (ahead >= count_)
		{
			fill(ahead + 1);
			if (ahead >= count_)
			{
				return kEndOfFile;
			}
		}
		return lookahead_[(head_ + ahead) & (kBufferCapacity - 1)].code_point;
	}

	// Consumes the code point at the consumption point.
	void advance()
	{
		if (count_ == 0)
		{
			fill(1);
			if (count_ == 0)
			{
				return;
			}
		}
		const Fetched& front = lookahead_[head_];
		if (front.code_point == kEndOfFile)
		{
			return;
		}
		if (front.invalid_escape_value)
		{
			throw_invalid_escape_value();
		}
		head_ = (head_ + 1) & (kBufferCapacity - 1);
		--count_;
	}

	// The consumption point, as a byte offset into the source file.
	std::size_t mark() const
	{
		return count_ == 0 ? cursor_ : lookahead_[head_].begin;
	}

	void rewind(std::size_t offset);

	bool raw_mode() const
	{
		return raw_mode_;
	}

	void set_raw_mode(bool raw);

	// "line:column" of the consumption point, counted in code points.
	std::string position_text() const;

	static const std::size_t kLookaheadCapacity = 8;

private:
	// A universal-character-name that turns out to be malformed contributes the
	// characters it was spelled with, so one step of the translation can yield
	// a short run rather than a single code point.
	static const std::size_t kMaxRunLength = 10;
	// Rounded up to a power of two: the ring buffer index is on the path of
	// every character, so it has to be a mask rather than a division.
	static const std::size_t kBufferCapacity = 32;
	static_assert(kBufferCapacity >= kLookaheadCapacity + kMaxRunLength,
		"lookahead plus one translation step must fit in the ring buffer");

	void fill(std::size_t wanted);
	void throw_invalid_escape_value() const;
	void push(const Fetched& fetched);
	void push_backslash_run();
	void push_universal_character_name(const Fetched& backslash, const Fetched& marker);
	std::string position_text_at(std::size_t offset) const;

	Fetched decoded_at(std::size_t offset) const;
	Fetched trigraph_at(std::size_t offset) const;

	std::string bytes_;
	SourceForm form_;
	std::size_t origin_;
	std::size_t cursor_;
	Fetched lookahead_[kBufferCapacity];
	std::size_t head_;
	std::size_t count_;
	bool raw_mode_;
};
