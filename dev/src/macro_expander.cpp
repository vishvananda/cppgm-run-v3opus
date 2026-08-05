#include "macro_expander.h"

#include <utility>

namespace
{

bool is_literal_spelling(MacroTokenType type)
{
	return type == MacroTokenType::CharacterLiteral ||
		type == MacroTokenType::UserDefinedCharacterLiteral ||
		type == MacroTokenType::StringLiteral ||
		type == MacroTokenType::UserDefinedStringLiteral;
}

}

const SpellingId MacroExpander::kNoSpelling;

MacroExpander::MacroExpander(PPTokenSource* source)
	: spelled_(spellings_)
	, macros_(spellings_, spelled_)
	, skipping_(false)
	, text_operator_(kNoSpelling)
	, directive_end_offset_(0)
	, source_(source)
	, source_done_(source == nullptr)
	, line_start_(true)
	, whitespace_(false)
	, directive_pending_(false)
	, finished_(false)
	, offset_(0)
	, has_lookahead_(false)
	, has_pending_(false)
	, seen_token_(false)
	, line_target_(nullptr)
	, line_done_(false)
{}

void MacroExpander::set_source(PPTokenSource* source)
{
	source_ = source;
	source_done_ = source == nullptr;
	// A source begins at the start of a line, and what came before it is
	// separated from it by at least the new-line that ended the directive.
	line_start_ = true;
	whitespace_ = true;
}

SourceError MacroExpander::error(const std::string& message) const
{
	return SourceError(" " + message);
}

bool MacroExpander::next(PPToken& token)
{
	while (true)
	{
		if (has_pending_)
		{
			const Spelling spelling = spellings_.text(pending_.spelling);
			token.type = static_cast<PPTokenType>(pending_.type);
			token.spelling.assign(spelling.data, spelling.size);
			token.offset = pending_.offset;
			has_pending_ = false;
			return true;
		}
		if (finished_)
		{
			return false;
		}
		if (!advance())
		{
			finished_ = true;
			token.type = PPTokenType::EndOfFile;
			token.spelling.clear();
			token.offset = offset_;
			return true;
		}
	}
}

// The phase 3 token in `raw_` as phase 4 sees it, with white space reduced to
// the flag the token carries.
void MacroExpander::convert(MacroToken& token)
{
	token.spelling = spellings_.intern(raw_.spelling);
	token.type = macro_token_type(raw_.type);
	token.offset = static_cast<std::uint32_t>(raw_.offset);
	token.whitespace_before = whitespace_;
	whitespace_ = false;
	offset_ = token.offset;
	seen_token_ = true;
}

// The next preprocessing-token of the file, skipping the white space and the
// new-lines between tokens.  A new-line is white space once the line it ends
// is known not to be a directive, which is what 16.3 needs for stringizing.
//
// A source that runs out is not the end of the input: `pop_source` decides,
// which is what makes an `#include` a source of its own rather than a splice.
bool MacroExpander::read_source(MacroToken& token)
{
	while (true)
	{
		if (source_done_)
		{
			if (!pop_source())
			{
				return false;
			}
			continue;
		}
		if (!source_->next(raw_) || raw_.type == PPTokenType::EndOfFile)
		{
			source_done_ = true;
			continue;
		}
		if (raw_.type == PPTokenType::WhitespaceSequence)
		{
			whitespace_ = true;
			continue;
		}
		if (raw_.type == PPTokenType::NewLine)
		{
			line_start_ = true;
			whitespace_ = whitespace_ || seen_token_;
			continue;
		}
		convert(token);
		return true;
	}
}

// Fills the look-ahead with the next token of the text-sequence.  Returns
// false where the sequence ends: at the end of the file, or at the `#` of a
// directive, whose line is read instead.
bool MacroExpander::fetch()
{
	while (true)
	{
		MacroToken token;
		if (!read_source(token))
		{
			return false;
		}
		const bool starts_line = line_start_;
		line_start_ = false;
		if (starts_line &&
		    (is_punctuation(token, spelled_.hash) ||
		     is_punctuation(token, spelled_.alt_hash)))
		{
			// The text-sequence ends here: the directive is processed once the
			// tokens before it have been replaced.
			read_directive(token);
			return false;
		}
		if (skipping_)
		{
			// An excluded section is not text.  Its tokens are read only so
			// that the directives between them can be found, so nothing here
			// is interned, replaced or diagnosed.
			continue;
		}
		if (token.type == MacroTokenType::Identifier &&
		    token.spelling == spelled_.va_args)
		{
			// 16.3/5: outside the replacement list of a variadic macro the
			// identifier is not allowed to appear at all.
			throw error("__VA_ARGS__ outside a variadic macro");
		}
		lookahead_ = token;
		has_lookahead_ = true;
		return true;
	}
}

bool MacroExpander::ensure_source()
{
	if (has_lookahead_)
	{
		return true;
	}
	// The directive is read and is not text, so the text-sequence has ended
	// whether or not the line the directive started was reached from one.
	if (directive_pending_)
	{
		return false;
	}
	return fetch();
}

MacroToken MacroExpander::take()
{
	if (!stack_.empty())
	{
		MacroToken token = stack_.back();
		stack_.pop_back();
		return token;
	}
	has_lookahead_ = false;
	return lookahead_;
}

const MacroToken* MacroExpander::peek()
{
	if (!stack_.empty())
	{
		return &stack_.back();
	}
	if (!ensure_source())
	{
		return nullptr;
	}
	return &lookahead_;
}

// The rest of the directive line, which the new-line that ends it terminates.
void MacroExpander::read_directive(const MacroToken& hash)
{
	directive_.clear();
	directive_.push_back(hash);
	while (!source_done_)
	{
		if (!source_->next(raw_) || raw_.type == PPTokenType::EndOfFile)
		{
			source_done_ = true;
			directive_end_offset_ = static_cast<std::uint32_t>(raw_.offset);
			break;
		}
		if (raw_.type == PPTokenType::WhitespaceSequence)
		{
			whitespace_ = true;
			continue;
		}
		if (raw_.type == PPTokenType::NewLine)
		{
			// Where the directive ends physically, which is one line before
			// the line `#line` renumbers.
			directive_end_offset_ = static_cast<std::uint32_t>(raw_.offset);
			break;
		}
		MacroToken token;
		convert(token);
		directive_.push_back(token);
	}
	// What follows the directive starts a line, and the new-line that ended
	// the directive separates it from the text before.
	line_start_ = true;
	whitespace_ = true;
	directive_pending_ = true;
}

void MacroExpander::run_directive()
{
	directive_pending_ = false;
	run_directive_line(directive_.data() + 1,
		directive_.data() + directive_.size());
}

// PA4's directive set.  The null directive is the empty range.
void MacroExpander::run_directive_line(const MacroToken* begin,
                                       const MacroToken* end)
{
	if (begin == end)
	{
		return;
	}
	if (begin->type != MacroTokenType::Identifier)
	{
		throw error("unknown preprocessing directive");
	}
	if (begin->spelling == spelled_.define)
	{
		macros_.define(begin, end);
	}
	else if (begin->spelling == spelled_.undef)
	{
		macros_.undefine(begin, end);
	}
	else
	{
		throw error("PA4 allows only #define and #undef");
	}
}

void MacroExpander::expand_builtin(const MacroToken& head, BuiltinMacro builtin)
{
	(void)head;
	(void)builtin;
	throw error("a predefined macro was defined by no one");
}

// The rescan of a token sequence that is not the file: one directive line.
//
// The end marker keeps the rescan inside the line.  A function-like macro name
// at its end finds the marker rather than the file's next `(`, so an
// invocation cannot start on a directive line and finish after it.
void MacroExpander::expand_range(const MacroToken* begin, const MacroToken* end,
                                 std::vector<MacroToken>& out)
{
	out.clear();
	line_target_ = &out;
	line_done_ = false;
	stack_.push_back(make_marker(MacroTokenType::EndLine, 0));
	for (const MacroToken* at = end; at-- != begin; )
	{
		stack_.push_back(*at);
	}
	stack_.push_back(make_marker(MacroTokenType::BeginSink, 0));
	while (!line_done_)
	{
		advance();
	}
	line_target_ = nullptr;
}

bool MacroExpander::advance()
{
	if (stack_.empty() && !ensure_source())
	{
		if (directive_pending_)
		{
			run_directive();
			return true;
		}
		return false;
	}
	MacroToken token = take();
	if (is_marker(token.type))
	{
		run_marker(token);
		return true;
	}
	if (try_expand(token))
	{
		return true;
	}
	if (token.spelling == text_operator_ && sinks_.empty() &&
	    token.type == MacroTokenType::Identifier)
	{
		// 16.6 as the assignment course-defines it: recognised in a
		// text-sequence, and only after all macro replacement, which is here.
		run_text_operator(token);
		return true;
	}
	emit(token);
	return true;
}

void MacroExpander::run_text_operator(const MacroToken& token)
{
	(void)token;
	throw error("a text operator was declared by no one");
}

// A placemarker never reaches here: `push_replacement` is the one place that
// decides a placemarker's fate, and it drops the ones the pastes left over
// before the replacement is put back on the stack.
void MacroExpander::emit(const MacroToken& token)
{
	if (sinks_.empty())
	{
		pending_ = token;
		has_pending_ = true;
	}
	else
	{
		output_.push_back(token);
	}
}

bool MacroExpander::try_expand(MacroToken& token)
{
	if (token.type != MacroTokenType::Identifier || token.unavailable)
	{
		return false;
	}
	const MacroDefinition* macro = macros_.lookup(token.spelling);
	if (macro == nullptr)
	{
		return false;
	}
	if (macro->function_like)
	{
		// 16.3.4: without a `(` next there is no invocation, and the token is
		// not made unavailable either, which is what lets a macro name that
		// leaves an argument be invoked by a later `(`.
		const MacroToken* ahead = peek();
		if (ahead == nullptr || !is_punctuation(*ahead, spelled_.lparen))
		{
			return false;
		}
	}
	if (paints_.contains(token.paint, token.spelling))
	{
		token.unavailable = true;
		return false;
	}
	if (macro->builtin != BuiltinMacro::None)
	{
		expand_builtin(token, macro->builtin);
	}
	else if (macro->function_like)
	{
		expand_function_like(token, *macro);
	}
	else
	{
		expand_object_like(token, *macro);
	}
	return true;
}

void MacroExpander::expand_object_like(const MacroToken& head,
                                       const MacroDefinition& macro)
{
	Invocation invocation;
	invocation.macro = &macro;
	invocation.head = head;
	invocation.accumulated_paint = paints_.add(head.paint, head.spelling);
	invocation.replacement_paint = invocation.accumulated_paint;
	invocation.arguments_begin = arguments_.size();
	invocation.tokens_begin = argument_tokens_.size();
	substitute(invocation);
}

void MacroExpander::expand_function_like(const MacroToken& head,
                                         const MacroDefinition& macro)
{
	invocations_.push_back(Invocation());
	Invocation& invocation = invocations_.back();
	invocation.macro = &macro;
	invocation.head = head;
	invocation.arguments_begin = arguments_.size();
	invocation.tokens_begin = argument_tokens_.size();

	MacroToken closing;
	collect_arguments(invocation, closing);

	// 16.3.4: what the replacement is hidden from is what the head and the
	// closing paren agree on, while the invocation nests in the accumulating
	// set of the head.
	invocation.accumulated_paint = paints_.add(head.paint, head.spelling);
	invocation.replacement_paint =
		paints_.add(paints_.intersect(head.paint, closing.paint), head.spelling);

	stack_.push_back(make_marker(MacroTokenType::Substitute, 0));
	request_argument_expansions(invocation);
	drop_raw_arguments(invocation);
}

// The tokens an argument was written with are needed after the prescan only by
// `#` and `##`.  Where the macro has neither, the prescan the rescan is about
// to run is their last reader, so they are released as soon as it is queued.
//
// This is what keeps nested arguments linear: `f(f(f(...)))` would otherwise
// hold every enclosing level's argument for as long as the innermost one is
// being replaced, which is quadratic in the nesting depth.
void MacroExpander::drop_raw_arguments(const Invocation& invocation)
{
	if (invocation.macro->keeps_raw_arguments)
	{
		return;
	}
	const std::size_t count = invocation.macro->parameter_count();
	for (std::size_t index = 0; index < count; ++index)
	{
		Argument& argument = arguments_[invocation.arguments_begin + index];
		argument.raw_begin = static_cast<std::uint32_t>(invocation.tokens_begin);
		argument.raw_end = argument.raw_begin;
	}
	argument_tokens_.resize(invocation.tokens_begin);
}

void MacroExpander::collect_arguments(Invocation& invocation, MacroToken& closing)
{
	take();  // the `(` the caller has already seen
	const MacroDefinition& macro = *invocation.macro;
	const std::size_t count = macro.parameter_count();
	// The variadic parameter takes the commas that follow it.  So does the
	// last parameter of a plain macro: the reference accepts extra arguments
	// there rather than rejecting the invocation.
	const std::size_t last = count == 0 ? 0 : count - 1;

	for (std::size_t index = 0; index < count; ++index)
	{
		Argument argument;
		argument.raw_begin = static_cast<std::uint32_t>(argument_tokens_.size());
		argument.raw_end = argument.raw_begin;
		argument.expanded_begin = 0;
		argument.expanded_end = 0;
		argument.raw_empty = true;
		arguments_.push_back(argument);
	}

	std::size_t current = 0;
	std::size_t depth = 0;
	bool any = false;
	while (true)
	{
		const MacroToken* ahead = peek();
		if (ahead == nullptr && directive_pending_)
		{
			// 16.3/11 leaves a directive inside an argument list undefined.
			// The invocation has already started, so the directive is acted on
			// where it stands and the argument list goes on after it.  This is
			// what makes the common `#if` inside a call work.
			run_directive();
			continue;
		}
		if (ahead == nullptr || is_marker(ahead->type))
		{
			throw error("macro invocation is not terminated");
		}
		MacroToken token = take();
		if (is_punctuation(token, spelled_.rparen))
		{
			if (depth == 0)
			{
				closing = token;
				break;
			}
			--depth;
		}
		else if (is_punctuation(token, spelled_.lparen))
		{
			++depth;
		}
		else if (is_punctuation(token, spelled_.comma) && depth == 0 &&
		         current < last)
		{
			any = true;
			++current;
			arguments_[invocation.arguments_begin + current].raw_begin =
				static_cast<std::uint32_t>(argument_tokens_.size());
			arguments_[invocation.arguments_begin + current].raw_end =
				static_cast<std::uint32_t>(argument_tokens_.size());
			continue;
		}
		if (count == 0)
		{
			throw error("macro takes no arguments");
		}
		any = true;
		argument_tokens_.push_back(token);
		Argument& argument = arguments_[invocation.arguments_begin + current];
		argument.raw_end = static_cast<std::uint32_t>(argument_tokens_.size());
		argument.raw_empty = false;
	}

	const std::size_t supplied = current + 1;
	const std::size_t required = macro.variadic ? macro.parameters.size() : count;
	if (count == 0)
	{
		if (any)
		{
			throw error("macro takes no arguments");
		}
	}
	else if (supplied < required)
	{
		throw error("macro invocation has too few arguments");
	}
}

void MacroExpander::request_argument_expansions(const Invocation& invocation)
{
	const MacroDefinition& macro = *invocation.macro;
	// Pushed back to front, so that the rescan reaches the first argument
	// first and the substitution marker last.
	for (std::size_t index = macro.parameter_count(); index-- > 0; )
	{
		if (!macro.expands_argument[index])
		{
			continue;
		}
		const Argument argument = arguments_[invocation.arguments_begin + index];
		if (argument.raw_begin == argument.raw_end)
		{
			continue;
		}
		stack_.push_back(make_marker(MacroTokenType::EndArgument,
			static_cast<std::uint32_t>(index)));
		for (std::uint32_t at = argument.raw_end; at-- > argument.raw_begin; )
		{
			stack_.push_back(argument_tokens_[at]);
		}
		stack_.push_back(make_marker(MacroTokenType::BeginSink, 0));
	}
}

void MacroExpander::run_marker(const MacroToken& marker)
{
	if (marker.type == MacroTokenType::BeginSink)
	{
		sinks_.push_back(output_.size());
		return;
	}
	if (marker.type == MacroTokenType::EndLine)
	{
		const std::size_t begin = sinks_.back();
		sinks_.pop_back();
		line_target_->assign(output_.begin() + begin, output_.end());
		output_.resize(begin);
		line_done_ = true;
		return;
	}
	if (marker.type == MacroTokenType::EndArgument)
	{
		const std::size_t begin = sinks_.back();
		sinks_.pop_back();
		Invocation& invocation = invocations_.back();
		Argument& argument = arguments_[invocation.arguments_begin + marker.offset];
		argument.expanded_begin = static_cast<std::uint32_t>(argument_tokens_.size());
		argument_tokens_.insert(argument_tokens_.end(),
			output_.begin() + begin, output_.end());
		argument.expanded_end = static_cast<std::uint32_t>(argument_tokens_.size());
		output_.resize(begin);
		return;
	}

	const Invocation invocation = invocations_.back();
	invocations_.pop_back();
	substitute(invocation);
	arguments_.resize(invocation.arguments_begin);
	argument_tokens_.resize(invocation.tokens_begin);
}

void MacroExpander::substitute(const Invocation& invocation)
{
	build_.clear();
	const MacroDefinition& macro = *invocation.macro;
	for (std::size_t index = 0; index < macro.body.size(); ++index)
	{
		append_item(macro.body[index], invocation);
	}
	push_replacement(invocation.head);
}

void MacroExpander::append_item(const MacroBodyItem& item,
                                const Invocation& invocation)
{
	const MacroDefinition& macro = *invocation.macro;
	MacroToken token;
	token.offset = invocation.head.offset;
	token.whitespace_before = item.whitespace_before;

	if (item.kind == MacroBodyItem::Token)
	{
		if (item.drop_for_empty_va &&
		    arguments_[invocation.arguments_begin +
		               macro.variadic_index()].raw_empty)
		{
			return;
		}
		token.spelling = item.spelling;
		token.type = item.type;
		append_token(token, invocation, item.paste_before, true);
		return;
	}
	if (item.kind == MacroBodyItem::Stringize)
	{
		stringize(arguments_[invocation.arguments_begin + item.parameter], token);
		append_token(token, invocation, item.paste_before, true);
		return;
	}

	const Argument argument = arguments_[invocation.arguments_begin + item.parameter];
	const std::uint32_t begin = item.raw_argument
		? argument.raw_begin : argument.expanded_begin;
	const std::uint32_t end = item.raw_argument
		? argument.raw_end : argument.expanded_end;
	if (begin == end)
	{
		if (item.raw_argument)
		{
			// 16.3.3: an empty `##` operand is a placemarker until the pastes
			// around it have been done.
			token.type = MacroTokenType::Placemarker;
			token.spelling = 0;
			append_token(token, invocation, item.paste_before, true);
		}
		return;
	}
	for (std::uint32_t at = begin; at != end; ++at)
	{
		MacroToken substituted = argument_tokens_[at];
		if (at == begin)
		{
			substituted.whitespace_before = item.whitespace_before;
		}
		append_token(substituted, invocation, item.paste_before && at == begin, false);
	}
}

void MacroExpander::append_token(MacroToken token, const Invocation& invocation,
                                 bool paste, bool from_replacement)
{
	finish_token(token, invocation, !from_replacement);
	if (paste)
	{
		paste_onto_back(token, invocation);
	}
	else
	{
		build_.push_back(token);
	}
}

// 16.3.3: the two spellings are joined and the result has to be one
// preprocessing-token, which is then rescanned like any other.
void MacroExpander::paste_onto_back(const MacroToken& right,
                                    const Invocation& invocation)
{
	if (build_.empty())
	{
		// The left operand vanished with the comma of the `, ## __VA_ARGS__`
		// extension, so there is nothing to join to.
		build_.push_back(right);
		return;
	}
	MacroToken& left = build_.back();
	if (right.type == MacroTokenType::Placemarker)
	{
		return;
	}
	if (left.type == MacroTokenType::Placemarker)
	{
		const bool whitespace = left.whitespace_before;
		left = right;
		left.whitespace_before = whitespace;
		return;
	}

	text_.clear();
	append_spelling(text_, spellings_.text(left.spelling));
	append_spelling(text_, spellings_.text(right.spelling));
	// The joined spelling is what phases 1 and 2 produced, so it is lexed as
	// such: a trailing `\` is a token of its own rather than a line splice.
	PPTokenLexer lexer(text_, SourceForm::Translated);
	PPToken joined;
	bool found = false;
	while (lexer.next(joined))
	{
		if (joined.type == PPTokenType::WhitespaceSequence ||
		    joined.type == PPTokenType::NewLine)
		{
			continue;
		}
		if (joined.type == PPTokenType::EndOfFile)
		{
			break;
		}
		if (found)
		{
			throw error("## joined " + text_ + " into more than one token");
		}
		found = true;
		left.spelling = spellings_.intern(joined.spelling);
		left.type = macro_token_type(joined.type);
	}
	if (!found)
	{
		throw error("## joined two tokens into none");
	}
	left.unavailable = false;
	finish_token(left, invocation, false);
}

// 16.3.2: the spelling of the argument as written, with one space where white
// space separated its tokens, and a `\` before the `"` and `\` of a literal.
void MacroExpander::stringize(const Argument& argument, MacroToken& result)
{
	text_ = "\"";
	for (std::uint32_t at = argument.raw_begin; at != argument.raw_end; ++at)
	{
		const MacroToken& token = argument_tokens_[at];
		if (at != argument.raw_begin && token.whitespace_before)
		{
			text_ += ' ';
		}
		const Spelling spelling = spellings_.text(token.spelling);
		const bool literal = is_literal_spelling(token.type);
		for (std::size_t index = 0; index < spelling.size; ++index)
		{
			const char character = spelling.data[index];
			if (literal && (character == '"' || character == '\\'))
			{
				text_ += '\\';
			}
			text_ += character;
		}
	}
	text_ += '"';
	result.type = MacroTokenType::StringLiteral;
	result.spelling = spellings_.intern(text_);
}

// A produced token is hidden from the names the invocation propagates, and is
// decided here against the accumulating set the invocation nests in.
//
// An object-like name is decided where it is produced because it needs no `(`
// to follow, so there is no later point that could decide it.  A function-like
// name is decided here only when it comes from an argument: 16.3.4 keeps the
// nesting relationship of the invocation across parameter substitution, while
// a name the replacement list itself spells is free again once the invocation
// that produced its head has been left behind.
void MacroExpander::finish_token(MacroToken& token, const Invocation& invocation,
                                 bool from_argument)
{
	token.paint = invocation.replacement_paint;
	if (token.unavailable || token.type != MacroTokenType::Identifier)
	{
		return;
	}
	const MacroDefinition* macro = macros_.lookup(token.spelling);
	if (macro != nullptr && (from_argument || !macro->function_like) &&
	    paints_.contains(invocation.accumulated_paint, token.spelling))
	{
		token.unavailable = true;
	}
}

void MacroExpander::push_replacement(const MacroToken& head)
{
	std::size_t kept = 0;
	for (std::size_t index = 0; index < build_.size(); ++index)
	{
		if (build_[index].type != MacroTokenType::Placemarker)
		{
			build_[kept++] = build_[index];
		}
	}
	build_.resize(kept);
	if (!build_.empty())
	{
		build_[0].whitespace_before = head.whitespace_before;
	}
	for (std::size_t index = build_.size(); index-- > 0; )
	{
		stack_.push_back(build_[index]);
	}
}

MacroToken MacroExpander::make_marker(MacroTokenType type,
                                      std::uint32_t index) const
{
	MacroToken marker;
	marker.type = type;
	marker.offset = index;
	return marker;
}
