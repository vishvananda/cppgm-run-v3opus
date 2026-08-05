#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "pptoken_lexer.h"

// The typed facts translation phase 4 carries: interned spellings, hide sets,
// the preprocessing-token phase 4 works on, and the analysed macro table.
//
// A macro replaced token sequence is copied far more often than it is read
// once: an argument is copied into every use of its parameter, a replacement
// list into every invocation, and everything into the rescan.  So a spelling
// is interned once and a token is a fixed 16 byte record that copies with a
// memcpy, and an identifier's macro is found by indexing rather than hashing.

typedef std::uint32_t SpellingId;
typedef std::uint32_t PaintId;

// Interns preprocessing-token spellings.  The pool owns the text; ids are
// dense, which is what lets the macro table be a vector indexed by name.
class SpellingPool
{
public:
	SpellingPool();

	SpellingId intern(const std::string& text);

	const std::string& text(SpellingId id) const { return *texts_[id]; }
	std::size_t size() const { return texts_.size(); }

private:
	std::unordered_map<std::string, SpellingId> ids_;
	std::vector<const std::string*> texts_;
};

// The sets of macro names a token is hidden from, hash-consed so that a set is
// one integer on a token and set equality is integer equality.
//
// 16.3.4 grows these sets one name at a time and intersects them at every
// function-like invocation, and the same few sets recur throughout a file, so
// both operations are memoised on their operand ids.
class PaintSets
{
public:
	PaintSets();

	static PaintId empty() { return 0; }

	bool contains(PaintId set, SpellingId name) const;
	PaintId add(PaintId set, SpellingId name);
	PaintId intersect(PaintId left, PaintId right);

private:
	struct Entry
	{
		std::uint32_t begin;
		std::uint32_t size;
	};

	PaintId intern(const std::vector<SpellingId>& names);

	std::vector<Entry> entries_;
	std::vector<SpellingId> names_;
	std::unordered_map<std::string, PaintId> interned_;
	std::unordered_map<std::uint64_t, PaintId> added_;
	std::unordered_map<std::uint64_t, PaintId> intersected_;
	std::vector<SpellingId> scratch_;
	std::string key_;
};

// The phase 3 token types, plus the ones phase 4 introduces: the placemarker
// of 16.3.3, and the control markers the rescan stack carries.
enum class MacroTokenType : std::uint8_t
{
	WhitespaceSequence,
	NewLine,
	HeaderName,
	Identifier,
	PPNumber,
	CharacterLiteral,
	UserDefinedCharacterLiteral,
	StringLiteral,
	UserDefinedStringLiteral,
	PreprocessingOpOrPunc,
	NonWhitespaceChar,
	EndOfFile,
	Placemarker,
	BeginSink,
	EndArgument,
	Substitute
};

inline MacroTokenType macro_token_type(PPTokenType type)
{
	return static_cast<MacroTokenType>(static_cast<std::uint8_t>(type));
}

inline bool is_marker(MacroTokenType type)
{
	return type >= MacroTokenType::BeginSink;
}

// One preprocessing-token as phase 4 sees it.  `paint` is the set of macro
// names this token is hidden from and `unavailable` is the sticky flag a token
// keeps once an invocation of its own name was refused for it: a later
// substitution reassigns the set but must not make the token invokable again.
struct MacroToken
{
	SpellingId spelling;
	PaintId paint;
	std::uint32_t offset;
	MacroTokenType type;
	bool whitespace_before;
	bool unavailable;

	MacroToken()
		: spelling(0)
		, paint(PaintSets::empty())
		, offset(0)
		, type(MacroTokenType::EndOfFile)
		, whitespace_before(false)
		, unavailable(false)
	{}
};

// Whether `token` is the given operator or punctuator.
bool is_punctuation(const MacroToken& token, SpellingId spelling);

// One step of the substitution 16.3.3 describes, analysed once at definition
// time so that an invocation is a walk rather than a re-parse.
struct MacroBodyItem
{
	enum Kind : std::uint8_t
	{
		Token,      // a replacement-list token, used as is
		Parameter,  // an argument, raw or macro replaced
		Stringize   // `# parameter`
	};

	Kind kind;
	MacroTokenType type;       // Token: the phase 3 type
	SpellingId spelling;       // Token: the spelling
	std::uint32_t parameter;   // Parameter, Stringize: which one
	bool whitespace_before;
	bool paste_before;         // a `##` joins this item to the previous one
	bool raw_argument;         // Parameter: `##` operand, so not macro replaced
	bool drop_for_empty_va;    // Token `,` of the `, ## __VA_ARGS__` extension
};

// One `#define`.  The replacement list is kept as written for the 16.3/2
// redefinition rule, and as an analysed program for substitution.
struct MacroDefinition
{
	bool function_like;
	bool variadic;
	std::vector<SpellingId> parameters;
	std::vector<MacroBodyItem> body;
	std::vector<MacroToken> replacement;
	// Whether a parameter appears where 16.3.1 asks for the macro replaced
	// argument.  An argument that is only stringized or pasted is never
	// expanded, which is what keeps `# x` of an ill-formed invocation legal.
	std::vector<bool> expands_argument;

	MacroDefinition()
		: function_like(false)
		, variadic(false)
	{}

	// Named parameters plus the variadic one.
	std::size_t parameter_count() const
	{
		return parameters.size() + (variadic ? 1 : 0);
	}

	std::size_t variadic_index() const { return parameters.size(); }
};

// The macros currently defined, by name.
class MacroTable
{
public:
	explicit MacroTable(SpellingPool& spellings);

	// `tokens` is one directive line, starting at the `define` or `undef`
	// identifier.  Throws SourceError when the directive is ill-formed.
	void define(const MacroToken* begin, const MacroToken* end);
	void undefine(const MacroToken* begin, const MacroToken* end);

	const MacroDefinition* lookup(SpellingId name) const
	{
		return name < by_name_.size() && by_name_[name] >= 0
			? &definitions_[static_cast<std::size_t>(by_name_[name])]
			: nullptr;
	}

	bool is_object_like_macro(SpellingId name) const
	{
		const MacroDefinition* macro = lookup(name);
		return macro != nullptr && !macro->function_like;
	}

private:
	const MacroToken* parse_parameters(const MacroToken* begin,
	                                   const MacroToken* end,
	                                   MacroDefinition& macro) const;
	void analyse_body(const MacroToken* begin, const MacroToken* end,
	                  MacroDefinition& macro) const;
	void apply_gnu_comma_extension(MacroDefinition& macro) const;
	std::uint32_t parameter_index(const MacroDefinition& macro,
	                              const MacroToken& token) const;
	void check_name(const MacroToken& token) const;
	bool same_definition(const MacroDefinition& left,
	                     const MacroDefinition& right) const;
	void install(SpellingId name, MacroDefinition& macro);

	SpellingPool& spellings_;
	std::vector<std::int32_t> by_name_;
	// A definition outlives its table entry, because an `#undef` followed by a
	// `#define` keeps the old one addressable; a deque is what makes a
	// definition pointer stable for as long as the expander holds it.
	std::deque<MacroDefinition> definitions_;

	SpellingId va_args_;
	SpellingId hash_;
	SpellingId alt_hash_;
	SpellingId hash_hash_;
	SpellingId alt_hash_hash_;
	SpellingId lparen_;
	SpellingId rparen_;
	SpellingId comma_;
	SpellingId ellipsis_;
};
