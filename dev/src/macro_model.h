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

// An interned spelling: the bytes the pool holds, which stay put for as long
// as the pool does.
struct Spelling
{
	const char* data;
	std::uint32_t size;
};

// Interns preprocessing-token spellings.  The pool owns the text; ids are
// dense, which is what lets the macro table be a vector indexed by name.
//
// Interning is on the path of every source token, so the text is packed into
// blocks rather than allocated a string at a time, and the index is an open
// addressed table of ids: one lookup touches the table and the bytes, and a
// new spelling costs an append rather than a node.
class SpellingPool
{
public:
	SpellingPool();

	SpellingId intern(const char* text, std::size_t size);
	SpellingId intern(const std::string& text)
	{
		return intern(text.data(), text.size());
	}

	Spelling text(SpellingId id) const { return spellings_[id]; }
	std::size_t size() const { return spellings_.size(); }

private:
	static std::size_t hash(const char* text, std::size_t size);
	bool matches(SpellingId id, const char* text, std::size_t size) const;
	std::size_t slot_of(const char* text, std::size_t size) const;
	void grow();
	const char* store(const char* text, std::size_t size);

	// The block a spelling is copied into is never resized, so a spelling's
	// bytes have one address for the lifetime of the pool.
	static const std::size_t kBlockSize = 64 * 1024;

	std::vector<Spelling> spellings_;
	// Slot contents are `id + 1`, so that zero is the empty slot.
	std::vector<SpellingId> slots_;
	std::size_t mask_;
	std::vector<std::vector<char> > blocks_;
	std::size_t used_;
};

// Appends the spelling to `text`.
inline void append_spelling(std::string& text, Spelling spelling)
{
	text.append(spelling.data, spelling.size);
}

// The sets of macro names a token is hidden from, so that a set is one integer
// on a token.
//
// 16.3.4 grows these sets one name at a time down a chain of nested
// invocations, so a set is here the set it extends plus one name: however deep
// the nesting is, a set costs one node and never a copy of the names below it.
// The same nesting recurs throughout a file, so extension is memoised on its
// operand ids and a set that has been built before is the same integer again.
//
// A function-like invocation intersects two sets, which is the one operation
// the chain does not answer by itself; it is memoised too, and its result is
// built back up in a canonical order so that the same set is one id.
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
		PaintId parent;
		SpellingId name;
		std::uint32_t size;
	};

	void collect(PaintId set, std::vector<SpellingId>& names) const;
	PaintId build(const std::vector<SpellingId>& names);

	std::vector<Entry> entries_;
	std::unordered_map<std::uint64_t, PaintId> added_;
	std::unordered_map<std::uint64_t, PaintId> intersected_;
	std::vector<SpellingId> left_;
	std::vector<SpellingId> right_;
	std::vector<SpellingId> common_;
};

// The phase 3 token types, plus the ones phase 4 introduces: the placemarker
// of 16.3.3, and the control markers the rescan stack carries.
enum class MacroTokenType : std::uint8_t
{
#define CPPGM_MACRO_TOKEN_TYPE_ENUMERATOR(name) name,
	CPPGM_PP_TOKEN_TYPES(CPPGM_MACRO_TOKEN_TYPE_ENUMERATOR)
#undef CPPGM_MACRO_TOKEN_TYPE_ENUMERATOR
	Placemarker,
	BeginSink,
	EndArgument,
	Substitute,
	EndLine
};

// The two share their phase 3 prefix by construction, so converting between
// them is a cast rather than a table.
#define CPPGM_MACRO_TOKEN_TYPE_CHECK(name) \
	static_assert(static_cast<std::uint8_t>(MacroTokenType::name) == \
		static_cast<std::uint8_t>(PPTokenType::name), \
		"the phase 3 and phase 4 token types have to agree");
CPPGM_PP_TOKEN_TYPES(CPPGM_MACRO_TOKEN_TYPE_CHECK)
#undef CPPGM_MACRO_TOKEN_TYPE_CHECK

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

// The spellings phase 4 has to recognise, interned once so that recognising
// one is an integer comparison.  The directive parse and the rescan look for
// overlapping sets of them, so they are held in one place rather than by each.
struct MacroSpellings
{
	explicit MacroSpellings(SpellingPool& spellings);

	SpellingId va_args;
	SpellingId hash;
	SpellingId alt_hash;
	SpellingId hash_hash;
	SpellingId alt_hash_hash;
	SpellingId lparen;
	SpellingId rparen;
	SpellingId comma;
	SpellingId ellipsis;
	// The two 16.2 combines a header-name out of, where macro replacement left
	// one spelled as separate preprocessing-tokens.
	SpellingId less;
	SpellingId greater;

	// 16: the directive names.  The whole list is here rather than split
	// between the two phase 4 owners, because whether an identifier names a
	// directive is one fact and the non-directive rule needs all of it.
	SpellingId define;
	SpellingId undef;
	SpellingId if_;
	SpellingId ifdef;
	SpellingId ifndef;
	SpellingId elif;
	SpellingId else_;
	SpellingId endif;
	SpellingId include;
	SpellingId line;
	SpellingId error;
	SpellingId pragma;

	// The identifiers 16.1 and 16.6 give a meaning of their own.  `pack` and
	// its two verbs are here for the same reason the directive names are: what
	// a pragma asks for is decided by one integer comparison per token of it.
	SpellingId defined;
	SpellingId once;
	SpellingId pragma_operator;
	SpellingId pack;
	SpellingId push;
	SpellingId pop;
};

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

// A predefined macro whose replacement list is not a fixed sequence of tokens
// but a fact about where it is used.  The fixed predefined macros are ordinary
// `#define`s and are not here.
enum class BuiltinMacro : std::uint8_t
{
	None,
	File,     // `__FILE__`: the presumed name of the current source file
	Line,     // `__LINE__`: the presumed line of the token that invoked it
	Counter   // `__COUNTER__`: one more than the last time it was invoked
};

// One `#define`.  The replacement list is kept as written for the 16.3/2
// redefinition rule, and as an analysed program for substitution.
struct MacroDefinition
{
	BuiltinMacro builtin;
	bool function_like;
	bool variadic;
	std::vector<SpellingId> parameters;
	std::vector<MacroBodyItem> body;
	std::vector<MacroToken> replacement;
	// Whether a parameter appears where 16.3.1 asks for the macro replaced
	// argument.  An argument that is only stringized or pasted is never
	// expanded, which is what keeps `# x` of an ill-formed invocation legal.
	std::vector<bool> expands_argument;
	// Whether any parameter is used as `#` or `##` asks for it, so that the
	// tokens an argument was written with have a reader after the prescan.
	bool keeps_raw_arguments;

	MacroDefinition()
		: builtin(BuiltinMacro::None)
		, function_like(false)
		, variadic(false)
		, keeps_raw_arguments(false)
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
	MacroTable(SpellingPool& spellings, const MacroSpellings& spelled);

	// `tokens` is one directive line, starting at the `define` or `undef`
	// identifier.  Throws SourceError when the directive is ill-formed.
	void define(const MacroToken* begin, const MacroToken* end);
	void undefine(const MacroToken* begin, const MacroToken* end);

	// Defines `name` as a predefined macro whose replacement is computed where
	// it is invoked.  It is a macro like any other from here on: `#ifdef` sees
	// it, and `#undef` removes it.
	void define_builtin(SpellingId name, BuiltinMacro builtin);

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
	const MacroSpellings& spelled_;
	std::vector<std::int32_t> by_name_;
	// A definition outlives its table entry, because an `#undef` followed by a
	// `#define` keeps the old one addressable; a deque is what makes a
	// definition pointer stable for as long as the expander holds it.
	std::deque<MacroDefinition> definitions_;
};
