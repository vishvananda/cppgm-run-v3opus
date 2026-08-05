#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

// Ill-formed source detected by a translation phase.  The message carries the
// source position; only the exit status is contractual, the text is for humans.
class SourceError : public std::runtime_error
{
public:
	explicit SourceError(const std::string& message)
		: std::runtime_error(message)
	{}
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
public:
	explicit SourceReader(std::string bytes);

	// Translated code point `ahead` positions past the consumption point, or
	// kEndOfFile.  `ahead` must be less than kLookaheadCapacity.
	int peek(std::size_t ahead = 0);

	// Consumes the code point at the consumption point.
	void advance();

	// The consumption point, as a byte offset into the source file.
	std::size_t mark() const;
	void rewind(std::size_t offset);

	bool raw_mode() const;
	void set_raw_mode(bool raw);

	// "line:column" of the consumption point, counted in code points.
	std::string position_text() const;

	static const std::size_t kLookaheadCapacity = 8;

private:
	struct Fetched
	{
		int code_point;
		std::size_t begin;
		std::size_t end;
		bool invalid_escape_value;
	};

	// A universal-character-name that turns out to be malformed contributes the
	// characters it was spelled with, so one step of the translation can yield
	// a short run rather than a single code point.
	static const std::size_t kMaxRunLength = 10;
	static const std::size_t kBufferCapacity = kLookaheadCapacity + kMaxRunLength;

	void fill(std::size_t wanted);
	void push(const Fetched& fetched);
	void push_backslash_run();
	void push_universal_character_name(const Fetched& backslash, const Fetched& marker);
	std::string position_text_at(std::size_t offset) const;

	Fetched decoded_at(std::size_t offset) const;
	Fetched trigraph_at(std::size_t offset) const;

	std::string bytes_;
	std::size_t cursor_;
	Fetched lookahead_[kBufferCapacity];
	std::size_t head_;
	std::size_t count_;
	bool raw_mode_;
};
