#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "token_model.h"

struct PreprocessorOptions;
class SourceFileTable;

// The terminal vocabulary of the shared source grammar: the phase 7 token
// types plus the terminals the grammar names that a token type alone cannot
// express.  `OP_RSHIFT` never reaches the parser, because 14.2.3 needs
// `T<U<3>>` to close two angle bracket pairs, so it arrives as the two
// consecutive terminals `ST_RSHIFT_1` and `ST_RSHIFT_2`.
enum AstTokenType
{
	TT_IDENTIFIER = kTokenTypeCount,
	TT_LITERAL,
	ST_EOF,
	ST_RSHIFT_1,
	ST_RSHIFT_2,
	kAstTokenTypeCount
};

// The name the dump gives a terminal, such as `KW_INT` or `TT_IDENTIFIER`.
const char* ast_token_type_name(unsigned type);

// One terminal: its type and the source spelling it was written with.  The
// spelling is an index into the stream's pool, so a token is eight bytes and
// a repeated identifier is stored once however often it occurs.
struct AstToken
{
	std::uint16_t type;
	std::uint32_t spelling;
};

// 16.6: the alignment `#pragma pack` leaves a class subobject, at each position
// of a token stream.
//
// The directive is a phase 4 fact and 9.2p13's layout is a phase 7 question, so
// what carries it between them is the one thing both name: a position in the
// stream.  Only the positions the value changes at are stored, so a unit that
// writes no such pragma stores nothing and one that writes n stores n, and the
// value in force anywhere is one binary search.
class PackTable
{
public:
	bool empty() const { return regions_.empty(); }

	// Records that the alignment becomes `alignment` at `begin`.  Positions
	// arrive in stream order.
	void add(std::size_t begin, unsigned long long alignment);

	// The alignment in force at `index`, or zero where no directive asked for
	// one.
	unsigned long long at(std::size_t index) const;

private:
	struct Region
	{
		std::size_t begin;
		unsigned long long alignment;
	};

	std::vector<Region> regions_;
};

// 2.2p1 and 16.2p4: whether the reading was in this unit's own source file, at
// each position of a token stream.
//
// `#include` is a phase 4 fact and which unit owes a definition is a phase 7
// question, so what carries it between them is the one thing both name: a
// position in the stream.  It is the same shape as `PackTable` and for the same
// reason - only the positions the answer changes at are stored, so a unit that
// includes nothing stores nothing and a unit with n inclusions stores at most
// 2n, and the answer anywhere is one binary search.
class IncludeTable
{
public:
	bool empty() const { return regions_.empty(); }

	// Records that the reading becomes `own` at `begin`.  Positions arrive in
	// stream order.
	void add(std::size_t begin, bool own);

	// Whether the token at `index` was read from this unit's own source file.
	// True where no inclusion was ever recorded, because then every token was.
	bool own_at(std::size_t index) const;

private:
	struct Region
	{
		std::size_t begin;
		bool own;
	};

	std::vector<Region> regions_;
};

// 2.2p1 and 16.3: the place in the source each terminal was written at.
//
// A place is a file and a byte offset into it, and the two are recorded the
// two ways they change: the file changes once per `#include`, which is the run
// encoding `IncludeTable` and `PackTable` already use, and the offset changes
// at every terminal, which is one `std::uint32_t` beside each.  The line and
// column a reader wants are counted off the file where they are asked for,
// because only a handful of terminals are ever asked about and every one of
// them would otherwise be counted for.
class SourceFile;

class SourcePositionTable
{
public:
	bool empty() const { return offsets_.empty(); }

	// Records that the reading is in `file` from `begin` on.  Positions arrive
	// in stream order.
	void open(std::size_t begin, const SourceFile* file);
	// Records the place of the next terminal.
	void append(std::size_t offset);

	// "file:line:column" of the terminal at `index`, or an empty string where
	// the stream recorded no place for it.
	std::string text_at(std::size_t index) const;

private:
	struct Region
	{
		std::size_t begin;
		const SourceFile* file;
	};

	std::vector<Region> files_;
	std::vector<std::uint32_t> offsets_;
};

// The terminals of one translation unit, with the spellings the dump needs.
//
// PA10 spells a name back out in several places - a template-id, a qualified
// name, a decltype-specifier - and in each the answer is the tokens it was
// written with rather than a normalised form.  Keeping the spellings next to
// the terminals lets a rule record the span it matched and spell it later,
// instead of building a name node per token.
class AstTokenStream
{
public:
	AstTokenStream();

	// Runs translation phases 1 to 7 over `path`.  Throws SourceError when the
	// file cannot be read or is not a valid sequence of C++ tokens.
	void build(SourceFileTable& files, const PreprocessorOptions& options,
	           const std::string& path);

	std::size_t size() const { return tokens_.size(); }

	unsigned type(std::size_t index) const
	{
		return index < tokens_.size() ? tokens_[index].type : unsigned(ST_EOF);
	}

	const std::string& spelling(std::size_t index) const
	{
		return pool_[index < tokens_.size() ? tokens_[index].spelling : 0];
	}

	// The tokens of `[begin, end)` written back out, separated only where two
	// of them would otherwise read back as one longer token.  This is how the
	// dump spells every name the grammar leaves for a later assignment to
	// resolve, so `TC1 < TC2 < 1 >>` comes back as `TC1<TC2<1>>`.
	//
	// Whether a separator stands between two terminals is a fact of those two
	// and the one after them, and of nothing else - the range being spelled
	// does not enter into it.  So it is settled once per terminal when the
	// stream is built, and spelling a range is the substring its own terminals
	// occupy of the whole stream written out.  A rule that backtracks spells
	// the same terminals many times, and this is what keeps that a copy rather
	// than a re-reading.
	std::string flatten(std::size_t begin, std::size_t end) const;

	// 16.6: what `#pragma pack` asked for, by position in this stream.
	const PackTable& packs() const { return packs_; }

	// 2.2p1: which tokens this unit's own source wrote, by position.
	const IncludeTable& sources() const { return sources_; }

	// Where in the source each terminal was written, by position.
	const SourcePositionTable& positions() const { return positions_; }

	// The characters of a narrow string literal, as phase 7 decoded them.
	// False for every other token.  One rule - the language a
	// linkage-specification names - wants a literal's value rather than its
	// spelling, and the value is a fact the tokenizer has already established.
	bool string_value(std::size_t index, std::string& out) const;

private:
	std::uint32_t intern(const std::string& text);
	void append(unsigned type, const std::string& text, std::size_t offset);
	// The whole stream written out, which every spelling of a range is a
	// substring of.  Made once, where the last terminal has arrived.
	void spell_stream();

	std::vector<AstToken> tokens_;
	std::string spelled_;
	// Where each terminal's spelling begins in `spelled_`, so that the
	// separator before it belongs to the terminal pair rather than to a range.
	std::vector<std::size_t> spelled_at_;
	PackTable packs_;
	IncludeTable sources_;
	SourcePositionTable positions_;
	std::vector<std::string> pool_;
	std::unordered_map<std::string, std::uint32_t> interned_;
	// By token index, for the string literals only: most tokens have no value
	// and every token would otherwise pay for the few that do.
	std::unordered_map<std::size_t, std::string> string_values_;
};
