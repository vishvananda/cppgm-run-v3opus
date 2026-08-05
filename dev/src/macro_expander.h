#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "macro_model.h"
#include "pptoken_lexer.h"

// Translation phase 4: preprocessing directives and macro replacement.
//
// The expander pulls one preprocessing-token at a time.  A `#` that starts a
// line begins a directive, whose line is read as a whole and updates the macro
// table; every other token belongs to the current text-sequence and is macro
// replaced, so the macros in force are exactly the ones defined above that
// point in the file.
//
// Replacement is the rescan of 16.3.4, run on one stack whose top is the next
// preprocessing-token.  An invocation pops its own tokens off that stack and
// pushes its replacement back, so a replacement can be re-examined and can
// form a new invocation with tokens that follow it in the file.  The rescan
// is also where a function-like macro finds its `(`, and a directive ends the
// text-sequence before it, so a name a directive separates from a `(` is not
// an invocation.  A directive *inside* an argument list is a different case
// that 16.3/11 leaves undefined; see `collect_arguments`.
//
// Nested work is on the same stack rather than on the C++ stack.  Macro
// replacing an argument pushes the argument between markers that redirect the
// rescan's output into a buffer, so the depth an input can reach costs heap
// and nothing else.
//
// PA4 is the whole of phase 4 for one file with two directives.  The rest of
// phase 4 is above a file rather than inside one - `#include` changes what the
// source is, `#if` changes whether there is text at all, and `__FILE__` is a
// fact about a file - so it is a derived class, and what it overrides is what
// this one leaves open: a directive it does not implement
// (`run_directive_line`), a source that runs out (`pop_source`), a predefined
// macro whose value is where it stands (`expand_builtin`), and an identifier
// the text-sequence answers rather than emits (`run_text_operator`).
class MacroExpander : public PPTokenSource
{
public:
	// `source` may be null, for a derived class that opens its first source
	// itself; `pop_source` is then asked for one.
	explicit MacroExpander(PPTokenSource* source);

	bool next(PPToken& token) override;

protected:
	// Acts on one directive, given the tokens after its `#`, which is an empty
	// range for the null directive.  PA4 implements `#define` and `#undef` and
	// rejects the rest.
	virtual void run_directive_line(const MacroToken* begin, const MacroToken* end);

	// Called when the current source runs out.  True when another one has been
	// installed with `set_source` and reading should continue.
	virtual bool pop_source() { return false; }

	// Produces the replacement of a predefined macro whose value depends on
	// where it was invoked.  Only a derived class defines such a macro.
	virtual void expand_builtin(const MacroToken& head, BuiltinMacro builtin);

	// Macro replaces `[begin, end)` on its own, leaving the result in `out`.
	// This is how a directive line that takes preprocessing-tokens - `#if`,
	// `#include`, `#line` - gets them replaced without the rescan running off
	// the end of the line into the file.
	void expand_range(const MacroToken* begin, const MacroToken* end,
	                  std::vector<MacroToken>& out);

	// Acts on `token`, an identifier equal to `text_operator_` reached in a
	// text-sequence.  Its operand follows it in the rescan.
	virtual void run_text_operator(const MacroToken& token);

	// Installs the source the next preprocessing-token is read from.
	void set_source(PPTokenSource* source);
	void push_token(const MacroToken& token) { stack_.push_back(token); }
	SourceError error(const std::string& message) const;

	// The next preprocessing-token of the rescan, and the one after it, which
	// is what a text operator's operand parse reads.
	MacroToken take();
	const MacroToken* peek();

	SpellingPool spellings_;
	MacroSpellings spelled_;
	PaintSets paints_;
	MacroTable macros_;

	// A spelling id no spelling has, for a hook that is not in use.
	static const SpellingId kNoSpelling = static_cast<SpellingId>(-1);

	// Whether the text-sequence is inside an excluded conditional section.  An
	// excluded section contributes no text at all, so its tokens are dropped
	// where they are read rather than replaced and thrown away.
	bool skipping_;
	// The spelling that is an operator rather than a token where a
	// text-sequence produces it, or `kNoSpelling` where there is none.  It is
	// an id comparison on the emit path rather than a virtual call.
	SpellingId text_operator_;
	// The byte offset of the new-line that ended the directive being run,
	// which is the physical line `#line` counts from.
	std::uint32_t directive_end_offset_;

private:
	// One argument of an invocation being built: the tokens as written, and
	// the macro replaced ones once 16.3.1 has asked for them.
	struct Argument
	{
		std::uint32_t raw_begin;
		std::uint32_t raw_end;
		std::uint32_t expanded_begin;
		std::uint32_t expanded_end;
		// Whether the argument was written empty.  This outlives the raw
		// tokens: the `, ## __VA_ARGS__` extension asks after they have been
		// released.
		bool raw_empty;
	};

	// One function-like invocation between the point its arguments are known
	// and the point its replacement is pushed.  Invocations nest strictly, so
	// they and their arguments live in stacks rather than in nodes of their
	// own.
	struct Invocation
	{
		const MacroDefinition* macro;
		MacroToken head;
		PaintId replacement_paint;  // 16.3.4 hide set every produced token gets
		PaintId accumulated_paint;  // the nesting set a produced name is judged by
		std::size_t arguments_begin;
		std::size_t tokens_begin;
	};

	// Input.
	void convert(MacroToken& token);
	bool read_source(MacroToken& token);
	bool fetch();
	bool ensure_source();

	// Directives.
	void read_directive(const MacroToken& hash);
	void run_directive();

	// Rescan.
	bool advance();
	bool try_expand(MacroToken& token);
	void expand_object_like(const MacroToken& head, const MacroDefinition& macro);
	void expand_function_like(const MacroToken& head, const MacroDefinition& macro);
	void collect_arguments(Invocation& invocation, MacroToken& closing);
	void request_argument_expansions(const Invocation& invocation);
	void drop_raw_arguments(const Invocation& invocation);
	void run_marker(const MacroToken& marker);
	void substitute(const Invocation& invocation);
	void append_item(const MacroBodyItem& item, const Invocation& invocation);
	void append_token(MacroToken token, const Invocation& invocation, bool paste,
	                  bool from_replacement);
	void paste_onto_back(const MacroToken& right, const Invocation& invocation);
	void stringize(const Argument& argument, MacroToken& result);
	void finish_token(MacroToken& token, const Invocation& invocation,
	                  bool from_argument);
	void push_replacement(const MacroToken& head);

	void emit(const MacroToken& token);
	MacroToken make_marker(MacroTokenType type, std::uint32_t index) const;

	PPTokenSource* source_;
	PPToken raw_;
	bool source_done_;
	bool line_start_;
	bool whitespace_;
	bool directive_pending_;
	bool finished_;
	std::uint32_t offset_;

	// One preprocessing-token of look-ahead into the text-sequence: the rescan
	// asks whether a `(` follows a function-like macro name without consuming
	// it.  The rest of a text-sequence is never held, so a file written as one
	// long line costs no more than a file written as many.
	MacroToken lookahead_;
	bool has_lookahead_;
	// The directive line, held from the `#` that starts it until the text
	// before it has been macro replaced.
	std::vector<MacroToken> directive_;

	std::vector<MacroToken> stack_;
	std::vector<MacroToken> output_;
	std::vector<std::size_t> sinks_;
	std::vector<Invocation> invocations_;
	std::vector<Argument> arguments_;
	std::vector<MacroToken> argument_tokens_;
	std::vector<MacroToken> build_;

	MacroToken pending_;
	bool has_pending_;
	bool seen_token_;

	// Where an `expand_range` in progress leaves its result, and whether its
	// end marker has been reached.  A directive line cannot occur inside a
	// directive line, so one of each is enough.
	std::vector<MacroToken>* line_target_;
	bool line_done_;

	std::string text_;
};
