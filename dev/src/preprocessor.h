#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "ctrl_expr.h"
#include "macro_expander.h"
#include "macro_model.h"
#include "post_token.h"
#include "pptoken_lexer.h"
#include "source_files.h"

// The predefined macros whose values a run decides rather than the standard.
// They are fixed for the whole run, so they are computed once and shared by
// every translation unit.
struct PreprocessorOptions
{
	std::string author;  // `__CPPGM_AUTHOR__`, without its quotes
	std::string date;    // `__DATE__`, "Mmm dd yyyy"
	std::string time;    // `__TIME__`, "hh:mm:ss"
};

// Translation phase 4 for a translation unit: PA4's macro replacement, plus
// every directive that is about a file rather than about a macro.
//
// What this owns and PA4 does not is the stack of open source files.  Each one
// carries its own presumed name and line, because `__FILE__` and `__LINE__`
// are facts about a file rather than about a token: the byte offset PA1
// already stores answers both against the file that is open when the token is
// replaced, so a token costs no location of its own.
//
// That holds because an invocation is read from one file and is located
// against it.  A directive ends the text-sequence before it, so an `#include`
// or a `#line` between two text tokens is between two invocations as well; the
// two ways one could reach into an invocation - a directive in an argument
// list, which 16.3/11 leaves undefined, and a file that runs out inside one -
// are refused rather than answered wrongly.
class Preprocessor : public MacroExpander
{
public:
	// Opens `path` as the source file.  Throws SourceError when it cannot be
	// read.
	Preprocessor(SourceFileTable& files, const PreprocessorOptions& options,
	             const std::string& path);

	// 16.6 and the course ABI: the maximum alignment `#pragma pack` leaves a
	// class subobject, or zero where the directive has asked for none.  It is
	// a fact of a *position* in the token stream rather than of the unit, so
	// the stream builder reads it beside each token it appends; `pack_epoch`
	// counts the directives that have changed it, which is what lets that
	// reading be one integer comparison per token instead of a copy.
	unsigned long long pack_alignment() const { return pack_; }
	unsigned long long pack_epoch() const { return pack_epoch_; }

protected:
	void run_directive_line(const MacroToken* begin, const MacroToken* end) override;
	bool pop_source() override;
	void expand_builtin(const MacroToken& head, BuiltinMacro builtin) override;
	void run_text_operator(const MacroToken& token) override;

private:
	// One open source file: the phase 3 lexer reading it, the bytes and line
	// index it reads, and the presumed name and line offset `#line` gives it.
	// An inclusion saves and restores all of it by nesting.
	struct OpenFile
	{
		OpenFile(const SourceFile& source, std::string name, std::size_t depth);

		PPTokenLexer lexer;
		const SourceFile* source;
		std::string presumed_name;
		// The presumed name as a string-literal spelling, interned when
		// `__FILE__` first asks for it rather than once per use.
		SpellingId presumed_literal;
		// Presumed line minus physical line, which is what `#line` sets.
		long long line_delta;
		// How many `#if` groups were open where this file started: a group has
		// to be closed in the file that opened it.
		std::size_t if_depth;
	};

	// One `#if` group, from its `#if` to its `#endif`.  A group in an excluded
	// section is still tracked, because 16.1 orders its sections whether or
	// not any of them is reached.
	struct IfGroup
	{
		bool outer_active;  // whether the section holding this group is active
		bool taken;         // whether one of its sections has been active
		bool active;        // whether its current section is
		bool seen_else;
	};

	// The macro table, as 16.1's `defined` operator asks about it.  The
	// operator is answered before replacement, so this is reached only where
	// macro replacement produced a `defined` of its own.
	struct MacroOracle : CtrlExprMacros
	{
		explicit MacroOracle(Preprocessor& owner) : owner(&owner) {}

		bool is_defined(const std::string& identifier) const override;

		Preprocessor* owner;
	};

	// Files.
	void push_file(const SourceFile& source, std::string name);
	OpenFile& current() { return *open_.back(); }
	unsigned long long presumed_line(std::uint32_t offset) const;
	void set_presumed_name(std::string name);
	SpellingId presumed_name_literal();

	// Conditional inclusion.
	void open_if(SpellingId name, const MacroToken* begin, const MacroToken* end);
	void run_elif(const MacroToken* begin, const MacroToken* end);
	void run_else();
	void run_endif();
	void update_skipping();
	bool evaluate_condition(const MacroToken* begin, const MacroToken* end);
	bool protect_defined(const MacroToken* begin, const MacroToken* end);
	bool operand_is_defined(const MacroToken* begin, const MacroToken* end);

	// The other directives.
	void run_include(const MacroToken* begin, const MacroToken* end);
	bool header_name_of(std::string& out);
	void run_line(const MacroToken* begin, const MacroToken* end);
	void apply_pragma(const MacroToken* begin, const MacroToken* end);
	bool apply_pack_pragma(const MacroToken* begin, const MacroToken* end);
	bool pragma_number(const MacroToken& token, unsigned long long& out);
	void set_pack(unsigned long long alignment);
	void include_file(const std::string& nextf);

	// Predefined macros.
	void define_predefined(const PreprocessorOptions& options);
	void define_object_like(const char* name, MacroTokenType type,
	                        const std::string& spelling);

	// Spellings.
	bool macro_name(const MacroToken& token, SpellingId& out) const;
	std::string spelling_text(SpellingId spelling) const;
	SpellingId intern_decimal(unsigned long long value);
	SpellingId intern_quoted(const std::string& text);
	bool string_literal_text(const MacroToken& token, std::string& out);
	void to_pp_token(const MacroToken& token, PPToken& out) const;

	SourceFileTable& files_;
	std::vector<std::unique_ptr<OpenFile> > open_;
	std::vector<IfGroup> ifs_;
	// The files `#pragma once` has closed, by identity rather than by path.
	std::unordered_set<SourceFileId, SourceFileIdHash> pragma_once_;
	// 16.6: the alignment `#pragma pack` leaves a class subobject, and the
	// values `push` saved, each with the label the directive named it by or
	// none.  The stack is the directive's own, so it outlives the file that
	// pushed onto it.
	struct PackFrame
	{
		bool labelled;
		SpellingId label;
		unsigned long long alignment;
	};

	unsigned long long pack_;
	unsigned long long pack_epoch_;
	std::vector<PackFrame> pack_stack_;
	unsigned long long counter_;

	MacroOracle oracle_;
	CtrlExprEvaluator conditions_;

	SpellingId zero_;
	SpellingId one_;

	// Reused buffers.  One directive line is in flight at a time, so one of
	// each is enough and none of them grows with the file.
	std::vector<MacroToken> guarded_;
	std::vector<MacroToken> expanded_;
	std::vector<MacroToken> pragma_;
	std::string scratch_;
	PPToken converted_;
	PostToken literal_;
};
