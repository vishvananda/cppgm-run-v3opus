#include "preprocessor.h"

#include <utility>

#include "literal_scan.h"
#include "string_literal.h"

namespace
{

// A translation unit that includes itself has no end, so the nesting an
// inclusion may reach is bounded.  The limit is far above any real header
// hierarchy and well below what would exhaust memory.
const std::size_t kMaxIncludeDepth = 256;

// `#line` sets a line number, which the assignment says is a positive integer
// and does not otherwise bound.  The presumed line of a later line is that
// number plus a physical distance, so it is kept inside a signed count.
const unsigned long long kMaxPresumedLine = 0x7FFFFFFFFFFFFFFFULL;

bool is_name_start(char character)
{
	return (character >= 'a' && character <= 'z') ||
		(character >= 'A' && character <= 'Z') ||
		character == '_';
}

// The body of a header-name, which is what it is spelled with minus the `<>`
// or `""` around it.
std::string header_name_body(const Spelling& spelling)
{
	return spelling.size < 2
		? std::string()
		: std::string(spelling.data + 1, spelling.size - 2);
}

// `text` as an ordinary string-literal denoting it.
std::string spelling_quoted(const std::string& text)
{
	std::string spelling = "\"";
	for (std::size_t at = 0; at < text.size(); ++at)
	{
		if (text[at] == '"' || text[at] == '\\')
		{
			spelling += '\\';
		}
		spelling += text[at];
	}
	spelling += '"';
	return spelling;
}

}

Preprocessor::OpenFile::OpenFile(const SourceFile& source, std::string name,
                                 std::size_t depth)
	: lexer(source.text())
	, source(&source)
	, presumed_name(std::move(name))
	, presumed_literal(0)
	, line_delta(0)
	, if_depth(depth)
{}

bool Preprocessor::MacroOracle::is_defined(const std::string& identifier) const
{
	Preprocessor& preprocessor = *owner;
	return preprocessor.macros_.lookup(
		preprocessor.spellings_.intern(identifier)) != nullptr;
}

Preprocessor::Preprocessor(SourceFileTable& files,
                           const PreprocessorOptions& options,
                           const std::string& path)
	: MacroExpander(nullptr)
	, files_(files)
	, pack_(0)
	, pack_epoch_(0)
	, counter_(0)
	, oracle_(*this)
	, conditions_(oracle_)
	, zero_(spellings_.intern("0"))
	, one_(spellings_.intern("1"))
{
	text_operator_ = spelled_.pragma_operator;
	define_predefined(options);

	const SourceFile* source = files_.open(path);
	if (source == nullptr)
	{
		throw error("cannot read source file " + path);
	}
	push_file(*source, path);
}

void Preprocessor::push_file(const SourceFile& source, std::string name)
{
	if (open_.size() >= kMaxIncludeDepth)
	{
		throw error("source file inclusion is nested too deeply");
	}
	open_.push_back(std::unique_ptr<OpenFile>(
		new OpenFile(source, std::move(name), ifs_.size())));
	set_source(&open_.back()->lexer);
}

// The file being read runs out.  Its `#if` groups have to have been closed in
// it, and what was around it resumes where the `#include` left it.
bool Preprocessor::pop_source()
{
	if (open_.empty())
	{
		return false;
	}
	if (ifs_.size() != open_.back()->if_depth)
	{
		throw error("an #if group is not terminated in the file that opened it");
	}
	open_.pop_back();
	if (open_.empty())
	{
		return false;
	}
	set_source(&open_.back()->lexer);
	return true;
}

unsigned long long Preprocessor::presumed_line(std::uint32_t offset) const
{
	const OpenFile& file = *open_.back();
	// Unsigned so that the sum is defined for any `#line` the assignment
	// accepts; a delta only ever brings a physical line back to a positive
	// presumed one.
	return static_cast<unsigned long long>(file.source->line_of(offset)) +
		static_cast<unsigned long long>(file.line_delta);
}

void Preprocessor::set_presumed_name(std::string name)
{
	OpenFile& file = current();
	file.presumed_name = std::move(name);
	file.presumed_literal = 0;
}

SpellingId Preprocessor::presumed_name_literal()
{
	OpenFile& file = current();
	if (file.presumed_literal == 0)
	{
		file.presumed_literal = intern_quoted(file.presumed_name);
	}
	return file.presumed_literal;
}

void Preprocessor::run_directive_line(const MacroToken* begin,
                                      const MacroToken* end)
{
	if (begin == end)
	{
		// The null directive.
		return;
	}
	if (begin->type != MacroTokenType::Identifier)
	{
		// A non-directive: an error where it is reached, and nothing at all
		// where the section holding it is excluded.
		if (skipping_)
		{
			return;
		}
		throw error("a preprocessing directive has to name a directive");
	}

	const SpellingId name = begin->spelling;
	const MacroToken* rest = begin + 1;
	if (name == spelled_.if_ || name == spelled_.ifdef || name == spelled_.ifndef)
	{
		open_if(name, rest, end);
	}
	else if (name == spelled_.elif)
	{
		run_elif(rest, end);
	}
	else if (name == spelled_.else_)
	{
		run_else();
	}
	else if (name == spelled_.endif)
	{
		run_endif();
	}
	else if (skipping_)
	{
		// Inside an excluded section only the shape of the `#if` group is
		// read, so every other directive is ignored, name and all.
	}
	else if (name == spelled_.define)
	{
		macros_.define(begin, end);
	}
	else if (name == spelled_.undef)
	{
		macros_.undefine(begin, end);
	}
	else if (name == spelled_.include)
	{
		run_include(rest, end);
	}
	else if (name == spelled_.line)
	{
		run_line(rest, end);
	}
	else if (name == spelled_.error)
	{
		throw error("#error in a section that is not excluded");
	}
	else if (name == spelled_.pragma)
	{
		apply_pragma(rest, end);
	}
	else
	{
		throw error("a preprocessing directive has to name a directive");
	}
}

void Preprocessor::update_skipping()
{
	skipping_ = !ifs_.empty() && !ifs_.back().active;
}

void Preprocessor::open_if(SpellingId name, const MacroToken* begin,
                           const MacroToken* end)
{
	IfGroup group;
	group.outer_active = !skipping_;
	group.seen_else = false;
	group.active = false;
	if (group.outer_active)
	{
		// A controlling expression in an excluded section is not evaluated at
		// all, which is what makes any sequence of tokens legal there.
		group.active = name == spelled_.if_
			? evaluate_condition(begin, end)
			: operand_is_defined(begin, end) == (name == spelled_.ifdef);
	}
	group.taken = group.active;
	ifs_.push_back(group);
	update_skipping();
}

void Preprocessor::run_elif(const MacroToken* begin, const MacroToken* end)
{
	if (ifs_.empty())
	{
		throw error("#elif without #if");
	}
	IfGroup& group = ifs_.back();
	if (group.seen_else)
	{
		throw error("#elif after #else");
	}
	group.active = group.outer_active && !group.taken &&
		evaluate_condition(begin, end);
	group.taken = group.taken || group.active;
	update_skipping();
}

void Preprocessor::run_else()
{
	if (ifs_.empty())
	{
		throw error("#else without #if");
	}
	IfGroup& group = ifs_.back();
	if (group.seen_else)
	{
		throw error("#else after #else");
	}
	group.seen_else = true;
	group.active = group.outer_active && !group.taken;
	group.taken = true;
	update_skipping();
}

void Preprocessor::run_endif()
{
	if (ifs_.empty())
	{
		throw error("#endif without #if");
	}
	ifs_.pop_back();
	update_skipping();
}

// 16.1: the `defined` operators are answered, the rest is macro replaced, and
// what is left is the controlling expression PA3 evaluates.
bool Preprocessor::evaluate_condition(const MacroToken* begin,
                                      const MacroToken* end)
{
	if (!protect_defined(begin, end))
	{
		throw error("the defined operator needs one macro name");
	}
	expand_range(guarded_.data(), guarded_.data() + guarded_.size(), expanded_);

	conditions_.begin_line();
	for (std::size_t index = 0; index < expanded_.size(); ++index)
	{
		to_pp_token(expanded_[index], converted_);
		conditions_.add(converted_);
	}
	if (conditions_.line_empty())
	{
		throw error("#if has no controlling expression");
	}
	CtrlExprValue value;
	if (!conditions_.evaluate(value))
	{
		throw error("#if controlling expression is an error");
	}
	return value.bits != 0;
}

// 16.1/1: the operand of `defined` is not macro replaced, so the operator is
// answered here, before the line is.  What is left has no operand to protect.
bool Preprocessor::protect_defined(const MacroToken* begin, const MacroToken* end)
{
	guarded_.clear();
	for (const MacroToken* at = begin; at != end; ++at)
	{
		if (at->type != MacroTokenType::Identifier ||
		    at->spelling != spelled_.defined)
		{
			guarded_.push_back(*at);
			continue;
		}

		const MacroToken* operand = at + 1;
		const bool parenthesized = operand != end &&
			is_punctuation(*operand, spelled_.lparen);
		if (parenthesized)
		{
			++operand;
		}
		SpellingId name = 0;
		if (operand == end || !macro_name(*operand, name))
		{
			return false;
		}
		const MacroToken* next = operand + 1;
		if (parenthesized)
		{
			if (next == end || !is_punctuation(*next, spelled_.rparen))
			{
				return false;
			}
			++next;
		}

		MacroToken result = *at;
		result.type = MacroTokenType::PPNumber;
		result.spelling = macros_.lookup(name) != nullptr ? one_ : zero_;
		guarded_.push_back(result);
		at = next - 1;
	}
	return true;
}

// The operand of `#ifdef` and `#ifndef`, which is a macro name and not an
// expression.
bool Preprocessor::operand_is_defined(const MacroToken* begin,
                                      const MacroToken* end)
{
	SpellingId name = 0;
	if (begin == end || !macro_name(*begin, name))
	{
		throw error("#ifdef needs one macro name");
	}
	return macros_.lookup(name) != nullptr;
}

// 16.2: the operand names a header.  A header-name phase 3 already made one
// contains no identifier, so macro replacing the operand leaves it alone and
// the one path answers all three forms it can arrive in.
void Preprocessor::run_include(const MacroToken* begin, const MacroToken* end)
{
	expand_range(begin, end, expanded_);
	std::string nextf;
	if (!header_name_of(nextf))
	{
		throw error("#include needs a header-name or a string-literal");
	}
	include_file(nextf);
}

// The name the replaced operand denotes: a header-name, an ordinary
// string-literal, or - 16.2/4 - what is between a `<` and a `>`, which macro
// replacement can leave spelled as preprocessing-tokens of their own and which
// an implementation combines into a header-name its own way.  The way here is
// the one the white space between them does not reach: their spellings, joined.
bool Preprocessor::header_name_of(std::string& out)
{
	const std::size_t count = expanded_.size();
	if (count == 1 && expanded_[0].type == MacroTokenType::HeaderName)
	{
		out = header_name_body(spellings_.text(expanded_[0].spelling));
		return true;
	}
	if (count == 1)
	{
		return string_literal_text(expanded_[0], out);
	}
	if (count < 2 || !is_punctuation(expanded_[0], spelled_.less) ||
	    !is_punctuation(expanded_[count - 1], spelled_.greater))
	{
		return false;
	}
	out.clear();
	for (std::size_t index = 1; index + 1 < count; ++index)
	{
		append_spelling(out, spellings_.text(expanded_[index].spelling));
	}
	return true;
}

// The course-defined search: the directory of the presumed `__FILE__` first,
// then the working directory.  The file that is found becomes `__FILE__`.
void Preprocessor::include_file(const std::string& nextf)
{
	const std::string& from = current().presumed_name;
	const std::size_t slash = from.rfind('/');
	std::string chosen;
	SourceFileId id;
	bool found = false;
	if (slash != std::string::npos)
	{
		chosen = from.substr(0, slash + 1) + nextf;
		found = files_.identify(chosen, id);
	}
	if (!found)
	{
		chosen = nextf;
		found = files_.identify(chosen, id);
	}
	if (!found)
	{
		throw error("cannot find included file " + nextf);
	}
	if (pragma_once_.find(id) != pragma_once_.end())
	{
		// 16.6: the file has asked to be included at most once, and it has.
		return;
	}
	const SourceFile* source = files_.open(chosen);
	if (source == nullptr)
	{
		throw error("cannot read included file " + chosen);
	}
	push_file(*source, chosen);
}

// `#line ppnumber [string-literal]` renumbers the line that follows the
// directive, which is the physical line after the one the directive ended on.
void Preprocessor::run_line(const MacroToken* begin, const MacroToken* end)
{
	if (collecting_arguments())
	{
		// The presumed location of a token is the one in force where it was
		// written, and the tokens of the argument list were written above this
		// directive; renumbering them from here would answer them wrongly.
		throw error("#line cannot renumber a macro invocation it is inside of");
	}
	expand_range(begin, end, expanded_);
	if (expanded_.empty() || expanded_[0].type != MacroTokenType::PPNumber)
	{
		throw error("#line needs a line number");
	}
	scan_pp_number(spelling_text(expanded_[0].spelling), literal_);
	if (literal_.kind != PostTokenKind::Literal ||
	    !fundamental_type_is_integral(literal_.type))
	{
		throw error("#line needs an integer line number");
	}
	const unsigned long long number = literal_.integer_value();
	if (number == 0 || number > kMaxPresumedLine)
	{
		throw error("#line needs a positive line number");
	}

	if (expanded_.size() > 1)
	{
		std::string name;
		if (expanded_.size() != 2 || !string_literal_text(expanded_[1], name))
		{
			throw error("#line takes a line number and a file name");
		}
		set_presumed_name(std::move(name));
	}

	OpenFile& file = current();
	const long long physical =
		static_cast<long long>(file.source->line_of(directive_end_offset_));
	file.line_delta = static_cast<long long>(number) - (physical + 1);
}

void Preprocessor::apply_pragma(const MacroToken* begin, const MacroToken* end)
{
	if (end - begin == 1 && begin->type == MacroTokenType::Identifier &&
	    begin->spelling == spelled_.once)
	{
		// The course defines `once` in terms of the identity of `__FILE__`,
		// which `#line` can have set to a name no file has.  There is then no
		// identity to remember and the directive cannot be carried out.
		SourceFileId id;
		if (!files_.identify(current().presumed_name, id))
		{
			throw error("#pragma once in a file that cannot be identified: " +
				current().presumed_name);
		}
		pragma_once_.insert(id);
		return;
	}
	if (apply_pack_pragma(begin, end))
	{
		return;
	}
	// 16.6: a pragma this implementation does not support has no effect.
}

// The decimal a `pack` argument is written as.  Only a plain integer means
// anything here, so a pp-number that is not one leaves the directive alone
// rather than being answered with a number it does not spell.
bool Preprocessor::pragma_number(const MacroToken& token, unsigned long long& out)
{
	if (token.type != MacroTokenType::PPNumber)
	{
		return false;
	}
	const std::string text = spelling_text(token.spelling);
	out = 0;
	for (std::size_t at = 0; at < text.size(); ++at)
	{
		if (text[at] < '0' || text[at] > '9' || out > (1ULL << 40))
		{
			return false;
		}
		out = out * 10 + static_cast<unsigned long long>(text[at] - '0');
	}
	return !text.empty();
}

void Preprocessor::set_pack(unsigned long long alignment)
{
	if (alignment != pack_)
	{
		pack_ = alignment;
		++pack_epoch_;
	}
}

// 16.6 and the course ABI: `#pragma pack` caps the alignment of every subobject
// of a class defined while it is in force.  The forms are the ones the
// implementations this ABI follows agree on - a width, a reset, a push with an
// optional label and an optional width, and a pop with an optional label - and
// the state they change is one integer and one stack, both of them facts of the
// translation unit rather than of a file.
//
// 16.6 does not replace macros in the tokens of a pragma, so what is read here
// are the tokens as written.  The value has to be a power of two, because it is
// used as an alignment: a width that is not one asks for something no address
// can satisfy, so it is a pragma this implementation does not have rather than
// a layout it cannot make.  False for anything that is not `pack`, which leaves
// 16.6's silence in place.
bool Preprocessor::apply_pack_pragma(const MacroToken* begin, const MacroToken* end)
{
	if (end - begin < 3 || begin->type != MacroTokenType::Identifier ||
	    begin->spelling != spelled_.pack ||
	    !is_punctuation(begin[1], spelled_.lparen) ||
	    !is_punctuation(end[-1], spelled_.rparen))
	{
		return false;
	}
	// The arguments, as the `,` separated list they are written as.  There are
	// at most three of them, so the list is read into a fixed array and a form
	// that writes more is one this implementation does not have.
	const MacroToken* argument[3] = {nullptr, nullptr, nullptr};
	const std::size_t written = static_cast<std::size_t>((end - 1) - (begin + 2));
	if (written > 5 || (written != 0 && written % 2 == 0))
	{
		return false;
	}
	std::size_t count = 0;
	for (std::size_t at = 0; at < written; ++at)
	{
		const MacroToken& token = begin[2 + at];
		if (at % 2 == 1)
		{
			if (!is_punctuation(token, spelled_.comma))
			{
				return false;
			}
			continue;
		}
		if (is_punctuation(token, spelled_.comma))
		{
			return false;
		}
		argument[count++] = &token;
	}
	if (count == 0)
	{
		// `pack()` restores the alignment a class would have had.
		set_pack(0);
		return true;
	}
	const bool verb = argument[0]->type == MacroTokenType::Identifier;
	const bool labelled = count > 1 && argument[1]->type == MacroTokenType::Identifier;
	// The width, where the form names one: it is the last argument of `push`
	// and the only argument of the bare form.
	unsigned long long width = 0;
	const MacroToken* const spelled_width =
		count == 1 && !verb ? argument[0]
		                    : (count > 1 && !labelled ? argument[1]
		                                              : (count == 3 ? argument[2]
		                                                            : nullptr));
	if (spelled_width != nullptr &&
	    (!pragma_number(*spelled_width, width) || width == 0 ||
	     (width & (width - 1)) != 0))
	{
		return false;
	}
	if (verb && argument[0]->spelling == spelled_.pop)
	{
		// A labelled pop unwinds to the frame that label pushed; an unlabelled
		// one drops the innermost frame.  Neither may name a width, and a pop
		// with nothing left to unwind leaves the default in force.
		if (count > 2 || (count == 2 && !labelled))
		{
			return false;
		}
		std::size_t depth = pack_stack_.size();
		if (count == 2)
		{
			while (depth != 0 && !(pack_stack_[depth - 1].labelled &&
			                       pack_stack_[depth - 1].label ==
			                           argument[1]->spelling))
			{
				--depth;
			}
			if (depth == 0)
			{
				return true;
			}
			--depth;
		}
		else if (depth != 0)
		{
			--depth;
		}
		set_pack(depth < pack_stack_.size() ? pack_stack_[depth].alignment : 0);
		pack_stack_.resize(depth);
		return true;
	}
	if (verb && argument[0]->spelling == spelled_.push)
	{
		if (count == 3 && !labelled)
		{
			return false;
		}
		PackFrame frame;
		frame.labelled = labelled;
		frame.label = labelled ? argument[1]->spelling : SpellingId();
		frame.alignment = pack_;
		pack_stack_.push_back(frame);
		if (spelled_width != nullptr)
		{
			set_pack(width);
		}
		return true;
	}
	if (count != 1 || verb)
	{
		return false;
	}
	set_pack(width);
	return true;
}

// The `_Pragma` operator: `_Pragma ( string-literal )` in a text-sequence,
// after all macro replacement.  Its tokens leave the sequence.
void Preprocessor::run_text_operator(const MacroToken& token)
{
	(void)token;
	const MacroToken* ahead = peek();
	if (ahead == nullptr || !is_punctuation(*ahead, spelled_.lparen))
	{
		throw error("_Pragma has to be followed by ( string-literal )");
	}
	take();

	ahead = peek();
	if (ahead == nullptr || ahead->type != MacroTokenType::StringLiteral)
	{
		throw error("_Pragma takes a string-literal");
	}
	const MacroToken literal = take();

	ahead = peek();
	if (ahead == nullptr || !is_punctuation(*ahead, spelled_.rparen))
	{
		throw error("_Pragma has to be followed by ( string-literal )");
	}
	take();

	// 16.9: the literal is destringized - an `L` prefix, the leading and the
	// trailing `"` deleted, and `\"` and `\\` unescaped - and the result is
	// read as the tokens of a `#pragma` directive.  No other prefix is named
	// there, so no other prefix is deleted and a literal spelled with one is a
	// pragma this implementation does not have.
	const Spelling spelling = spellings_.text(literal.spelling);
	const std::size_t begin = spelling.size != 0 && spelling.data[0] == 'L' ? 1 : 0;
	const std::size_t end = spelling.size - 1;  // the trailing `"`
	scratch_.clear();
	bool leading_quote = true;
	for (std::size_t at = begin; at < end; ++at)
	{
		if (leading_quote && spelling.data[at] == '"')
		{
			leading_quote = false;
			continue;
		}
		if (spelling.data[at] == '\\' && at + 1 < end &&
		    (spelling.data[at + 1] == '"' || spelling.data[at + 1] == '\\'))
		{
			++at;
		}
		scratch_ += spelling.data[at];
	}

	pragma_.clear();
	PPTokenLexer lexer(scratch_, SourceForm::Translated);
	PPToken raw;
	while (lexer.next(raw))
	{
		if (raw.type == PPTokenType::WhitespaceSequence ||
		    raw.type == PPTokenType::NewLine ||
		    raw.type == PPTokenType::EndOfFile)
		{
			continue;
		}
		MacroToken pragma_token;
		pragma_token.type = macro_token_type(raw.type);
		pragma_token.spelling = spellings_.intern(raw.spelling);
		pragma_token.offset = token.offset;
		pragma_.push_back(pragma_token);
	}
	apply_pragma(pragma_.data(), pragma_.data() + pragma_.size());
}

void Preprocessor::expand_builtin(const MacroToken& head, BuiltinMacro builtin)
{
	MacroToken token;
	token.offset = head.offset;
	token.whitespace_before = head.whitespace_before;
	token.paint = paints_.add(head.paint, head.spelling);
	switch (builtin)
	{
	case BuiltinMacro::File:
		token.type = MacroTokenType::StringLiteral;
		token.spelling = presumed_name_literal();
		break;
	case BuiltinMacro::Line:
		token.type = MacroTokenType::PPNumber;
		token.spelling = intern_decimal(presumed_line(head.offset));
		break;
	case BuiltinMacro::Counter:
		token.type = MacroTokenType::PPNumber;
		token.spelling = intern_decimal(counter_++);
		break;
	case BuiltinMacro::None:
		return;
	}
	push_token(token);
}

void Preprocessor::define_predefined(const PreprocessorOptions& options)
{
	define_object_like("__CPPGM__", MacroTokenType::PPNumber, "201303L");
	define_object_like("__cplusplus", MacroTokenType::PPNumber, "201103L");
	define_object_like("__STDC_HOSTED__", MacroTokenType::PPNumber, "1");
	define_object_like("__CPPGM_AUTHOR__", MacroTokenType::StringLiteral,
		spelling_quoted(options.author));
	define_object_like("__DATE__", MacroTokenType::StringLiteral,
		spelling_quoted(options.date));
	define_object_like("__TIME__", MacroTokenType::StringLiteral,
		spelling_quoted(options.time));

	macros_.define_builtin(spellings_.intern("__FILE__"), BuiltinMacro::File);
	macros_.define_builtin(spellings_.intern("__LINE__"), BuiltinMacro::Line);
	macros_.define_builtin(spellings_.intern("__COUNTER__"), BuiltinMacro::Counter);
}

// A predefined macro is a `#define` like any other, so it is installed through
// the one path that analyses one.
void Preprocessor::define_object_like(const char* name, MacroTokenType type,
                                      const std::string& spelling)
{
	MacroToken line[3];
	line[0].type = MacroTokenType::Identifier;
	line[0].spelling = spelled_.define;
	line[1].type = MacroTokenType::Identifier;
	line[1].spelling = spellings_.intern(name);
	line[2].type = type;
	line[2].spelling = spellings_.intern(spelling);
	line[2].whitespace_before = true;
	macros_.define(line, line + 3);
}

// 2.6: an operator spelled as a word is a name to `defined` and to `#ifdef`,
// which ask about a macro table rather than about the expression grammar.
bool Preprocessor::macro_name(const MacroToken& token, SpellingId& out) const
{
	if (token.type == MacroTokenType::Identifier)
	{
		out = token.spelling;
		return true;
	}
	const Spelling spelling = spellings_.text(token.spelling);
	if (token.type == MacroTokenType::PreprocessingOpOrPunc &&
	    spelling.size != 0 && is_name_start(spelling.data[0]))
	{
		out = token.spelling;
		return true;
	}
	return false;
}

std::string Preprocessor::spelling_text(SpellingId spelling) const
{
	const Spelling text = spellings_.text(spelling);
	return std::string(text.data, text.size);
}

SpellingId Preprocessor::intern_decimal(unsigned long long value)
{
	char digits[24];
	std::size_t length = 0;
	do
	{
		digits[length++] = static_cast<char>('0' + value % 10);
		value /= 10;
	}
	while (value != 0);

	scratch_.clear();
	while (length != 0)
	{
		scratch_ += digits[--length];
	}
	return spellings_.intern(scratch_);
}

SpellingId Preprocessor::intern_quoted(const std::string& text)
{
	return spellings_.intern(spelling_quoted(text));
}

// Phase 6 for one string-literal: what a `#include` or a `#line` file name
// means is the sequence of characters the literal denotes.
bool Preprocessor::string_literal_text(const MacroToken& token, std::string& out)
{
	if (token.type != MacroTokenType::StringLiteral)
	{
		return false;
	}
	StringLiteralSequence sequence;
	sequence.add(spelling_text(token.spelling));
	sequence.build(literal_);
	if (literal_.kind != PostTokenKind::LiteralArray || literal_.type != FT_CHAR ||
	    literal_.data.empty())
	{
		return false;
	}
	// The array holds the terminating null character, which the name does not.
	out.assign(literal_.data, 0, literal_.data.size() - 1);
	return true;
}

void Preprocessor::to_pp_token(const MacroToken& token, PPToken& out) const
{
	const Spelling spelling = spellings_.text(token.spelling);
	out.type = static_cast<PPTokenType>(token.type);
	out.spelling.assign(spelling.data, spelling.size);
	out.offset = token.offset;
}
