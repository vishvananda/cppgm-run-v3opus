#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "macro_model.h"
#include "pptoken_lexer.h"

// Translation phase 4: preprocessing directives and macro replacement.
//
// The expander reads one logical line at a time.  A line whose first token is
// `#` is a directive and updates the macro table; every other line belongs to
// the current text-sequence and is macro replaced, so the macros in force are
// exactly the ones defined above that point in the file.
//
// Replacement is the rescan of 16.3.4, run on one stack whose top is the next
// preprocessing-token.  An invocation pops its own tokens off that stack and
// pushes its replacement back, so a replacement can be re-examined and can
// form a new invocation with tokens that follow it in the file.  The rescan
// is also where a function-like macro finds its `(`, which is why an
// invocation never crosses a directive: the stack and the line both end there.
//
// Nested work is on the same stack rather than on the C++ stack.  Macro
// replacing an argument pushes the argument between markers that redirect the
// rescan's output into a buffer, so the depth an input can reach costs heap
// and nothing else.
class MacroExpander : public PPTokenSource
{
public:
	explicit MacroExpander(PPTokenSource& source);

	bool next(PPToken& token) override;

private:
	// One argument of an invocation being built: the tokens as written, and
	// the macro replaced ones once 16.3.1 has asked for them.
	struct Argument
	{
		std::uint32_t raw_begin;
		std::uint32_t raw_end;
		std::uint32_t expanded_begin;
		std::uint32_t expanded_end;
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
	bool read_line();
	bool ensure_line();
	MacroToken take();
	const MacroToken* peek();

	// Directives.
	void run_directive();

	// Rescan.
	bool advance();
	bool try_expand(MacroToken& token);
	void expand_object_like(const MacroToken& head, const MacroDefinition& macro);
	void expand_function_like(const MacroToken& head, const MacroDefinition& macro);
	void collect_arguments(Invocation& invocation, MacroToken& closing);
	void request_argument_expansions(const Invocation& invocation);
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
	SourceError error(const std::string& message) const;

	SpellingPool spellings_;
	PaintSets paints_;
	MacroTable macros_;

	SpellingId va_args_;
	SpellingId hash_;
	SpellingId alt_hash_;
	SpellingId lparen_;
	SpellingId rparen_;
	SpellingId comma_;
	SpellingId define_;
	SpellingId undef_;

	PPTokenSource& source_;
	PPToken raw_;
	bool source_done_;
	bool line_is_directive_;
	bool directive_pending_;
	bool finished_;
	std::uint32_t offset_;

	std::vector<MacroToken> line_;
	std::size_t line_position_;

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

	std::string text_;
};
