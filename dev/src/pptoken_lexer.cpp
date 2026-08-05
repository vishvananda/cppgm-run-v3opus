#include "pptoken_lexer.h"

#include <unordered_set>
#include <utility>

#include "source_charset.h"
#include "token_model.h"

namespace
{

const std::size_t kMaxOperatorLength = 4;
const std::size_t kMaxRawStringDelimiterLength = 16;

// Every spelling that has to be matched by look-ahead rather than by the
// identifier production is at most four ASCII characters, so they are packed
// into an integer and compared by value.
typedef unsigned int SpellingId;

constexpr SpellingId spelled(char first)
{
	return static_cast<unsigned char>(first);
}

constexpr SpellingId spelled(char first, char second)
{
	return (spelled(first) << 8) | static_cast<unsigned char>(second);
}

constexpr SpellingId spelled(char first, char second, char third)
{
	return (spelled(first, second) << 8) | static_cast<unsigned char>(third);
}

constexpr SpellingId spelled(char first, char second, char third, char fourth)
{
	return (spelled(first, second, third) << 8) | static_cast<unsigned char>(fourth);
}

// See C++ standard 2.13 Operators and punctuators.  The identifier-like
// spellings are matched after the identifier production instead, so that
// maximal munch over an identifier decides between `or` and `orange`.
bool is_operator_or_punctuator(SpellingId id)
{
	switch (id)
	{
	case spelled('{'): case spelled('}'): case spelled('['): case spelled(']'):
	case spelled('#'): case spelled('('): case spelled(')'): case spelled(';'):
	case spelled(':'): case spelled('?'): case spelled('.'): case spelled('+'):
	case spelled('-'): case spelled('*'): case spelled('/'): case spelled('%'):
	case spelled('^'): case spelled('&'): case spelled('|'): case spelled('~'):
	case spelled('!'): case spelled('='): case spelled('<'): case spelled('>'):
	case spelled(','):
	case spelled('#', '#'): case spelled('<', ':'): case spelled(':', '>'):
	case spelled('<', '%'): case spelled('%', '>'): case spelled('%', ':'):
	case spelled(':', ':'): case spelled('.', '*'): case spelled('+', '='):
	case spelled('-', '='): case spelled('*', '='): case spelled('/', '='):
	case spelled('%', '='): case spelled('^', '='): case spelled('&', '='):
	case spelled('|', '='): case spelled('<', '<'): case spelled('>', '>'):
	case spelled('=', '='): case spelled('!', '='): case spelled('<', '='):
	case spelled('>', '='): case spelled('&', '&'): case spelled('|', '|'):
	case spelled('+', '+'): case spelled('-', '-'): case spelled('-', '>'):
	case spelled('.', '.', '.'): case spelled('>', '>', '='):
	case spelled('<', '<', '='): case spelled('-', '>', '*'):
	case spelled('%', ':', '%', ':'):
		return true;
	default:
		return false;
	}
}

bool is_identifier_like_operator(const std::string& text)
{
	// Reject on length and leading character before hashing, which is what
	// nearly every identifier in a real source file needs.
	if (text.size() < 2 || text.size() > 6)
	{
		return false;
	}
	switch (text[0])
	{
	case 'a': case 'b': case 'c': case 'd': case 'n': case 'o': case 'x':
		break;
	default:
		return false;
	}
	static const std::unordered_set<std::string> kSpellings =
	{
#define CPPGM_IDENTIFIER_LIKE_OPERATOR_SPELLING(spelling, token_type) spelling,
		CPPGM_IDENTIFIER_LIKE_OPERATORS(CPPGM_IDENTIFIER_LIKE_OPERATOR_SPELLING)
#undef CPPGM_IDENTIFIER_LIKE_OPERATOR_SPELLING
	};
	return kSpellings.count(text) != 0;
}

bool is_string_literal_prefix(SpellingId id)
{
	switch (id)
	{
	case spelled('u', '8'): case spelled('u'): case spelled('U'):
	case spelled('L'): case spelled('R'): case spelled('u', '8', 'R'):
	case spelled('u', 'R'): case spelled('U', 'R'): case spelled('L', 'R'):
		return true;
	default:
		return false;
	}
}

bool is_character_literal_prefix(SpellingId id)
{
	return id == spelled('u') || id == spelled('U') || id == spelled('L');
}

// The packed form of a spelling, or zero when the text is too long or is not
// plain ASCII and so cannot be one.  Zero is unambiguous: no spelling that is
// matched this way is empty or contains a null character.
SpellingId spelling_id(const std::string& text)
{
	if (text.empty() || text.size() > kMaxOperatorLength)
	{
		return 0;
	}
	SpellingId id = 0;
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		const unsigned char unit = static_cast<unsigned char>(text[index]);
		if (unit >= 0x80)
		{
			return 0;
		}
		id = (id << 8) | unit;
	}
	return id;
}

bool is_raw_string_prefix(const std::string& text)
{
	return !text.empty() && text[text.size() - 1] == 'R';
}

// See `simple-escape-sequence` grammar.
bool is_simple_escape(int code_point)
{
	switch (code_point)
	{
	case '\'':
	case '"':
	case '?':
	case '\\':
	case 'a':
	case 'b':
	case 'f':
	case 'n':
	case 'r':
	case 't':
	case 'v':
		return true;
	default:
		return false;
	}
}

// A d-char-sequence is spelled without space, `\` or the vertical whitespace,
// but 2.5.2 lets a malformed token be tokenized anyway rather than rejected, so
// only the characters that would make the delimiter unrecognizable are refused.
// What ends the sequence is the `(`; what ends the literal is the delimiter.
bool is_raw_string_delimiter_char(int code_point)
{
	return code_point != kEndOfFile && code_point != '(' && code_point != ')';
}

// A hexadecimal pp-number takes its exponent sign after `p`, not after `e`,
// because `e` is one of its digits.  That makes `0x1e+2` the three tokens a
// real hexadecimal literal needs rather than one pp-number.
bool is_exponent_letter(int code_point, bool hexadecimal)
{
	if (hexadecimal)
	{
		return code_point == 'p' || code_point == 'P';
	}
	return code_point == 'e' || code_point == 'E';
}

// `x` only introduces hexadecimal digits while the pp-number is still its
// leading digit-sequence.  A `.` ends that sequence, and so does an `e` that is
// really an exponent, that is one followed by a sign or by a digit.  So the `x`
// of `1e2x3e+4` is an ordinary body character and the whole thing is one
// pp-number, while `0Exe+2` is hexadecimal and stops before the `+`.
bool ends_leading_digits(int code_point, int next)
{
	if (code_point == '.')
	{
		return true;
	}
	if (code_point != 'e' && code_point != 'E')
	{
		return false;
	}
	return is_digit(next) || next == '+' || next == '-';
}

} // namespace

PPTokenLexer::PPTokenLexer(std::string source)
	: reader_(std::move(source))
	, directive_(DirectiveState::LineStart)
	, finished_(false)
{}

bool PPTokenLexer::next(PPToken& token)
{
	if (finished_)
	{
		return false;
	}

	token.spelling.clear();
	token.offset = reader_.mark();

	const int code_point = reader_.peek();
	if (code_point == kEndOfFile)
	{
		finished_ = true;
		token.type = PPTokenType::EndOfFile;
		return true;
	}
	if (code_point == '\n')
	{
		reader_.advance();
		token.type = PPTokenType::NewLine;
		directive_ = DirectiveState::LineStart;
		return true;
	}
	if (starts_whitespace_sequence())
	{
		scan_whitespace_sequence();
		token.type = PPTokenType::WhitespaceSequence;
		return true;
	}

	scan_token(token);
	update_directive_state(token);
	return true;
}

bool PPTokenLexer::starts_whitespace_sequence()
{
	const int code_point = reader_.peek();
	if (is_basic_whitespace(code_point))
	{
		return true;
	}
	if (code_point != '/')
	{
		return false;
	}
	const int next = reader_.peek(1);
	return next == '/' || next == '*';
}

void PPTokenLexer::scan_whitespace_sequence()
{
	// Comments are subsumed into the whitespace-sequence production rather
	// than replaced by a space, which keeps `a/*x*/b` two tokens.
	while (true)
	{
		if (is_basic_whitespace(reader_.peek()))
		{
			reader_.advance();
			continue;
		}
		if (reader_.peek() != '/')
		{
			return;
		}
		const int next = reader_.peek(1);
		if (next == '/')
		{
			skip_line_comment();
			continue;
		}
		if (next == '*')
		{
			skip_block_comment();
			continue;
		}
		return;
	}
}

void PPTokenLexer::skip_line_comment()
{
	reader_.advance();
	reader_.advance();
	while (true)
	{
		const int code_point = reader_.peek();
		if (code_point == '\n')
		{
			return;
		}
		// Phase 2 gives every non-empty file a terminating new-line, so end of
		// file here means the last one was spliced away by a trailing `\`.
		if (code_point == kEndOfFile)
		{
			throw error("Unterminated comment");
		}
		reader_.advance();
	}
}

void PPTokenLexer::skip_block_comment()
{
	reader_.advance();
	reader_.advance();
	while (true)
	{
		const int code_point = reader_.peek();
		if (code_point == kEndOfFile)
		{
			throw error("Unterminated comment");
		}
		if (code_point == '*' && reader_.peek(1) == '/')
		{
			reader_.advance();
			reader_.advance();
			return;
		}
		reader_.advance();
	}
}

void PPTokenLexer::scan_token(PPToken& token)
{
	const int code_point = reader_.peek();
	if (directive_ == DirectiveState::AfterInclude &&
	    (code_point == '<' || code_point == '"'))
	{
		scan_header_name(token);
		return;
	}
	if (is_identifier_start(code_point))
	{
		scan_identifier_or_literal(token);
		return;
	}
	if (is_digit(code_point) || (code_point == '.' && is_digit(reader_.peek(1))))
	{
		scan_pp_number(token);
		return;
	}
	if (code_point == '\'')
	{
		scan_character_literal(token);
		return;
	}
	if (code_point == '"')
	{
		scan_string_literal(token);
		return;
	}
	if (scan_op_or_punc(token))
	{
		return;
	}

	// Course defined: `'` and `"` never reach this production, so every other
	// code point that fits no token stands alone.
	take(token.spelling);
	token.type = PPTokenType::NonWhitespaceChar;
}

void PPTokenLexer::scan_header_name(PPToken& token)
{
	const int close = reader_.peek() == '<' ? '>' : '"';

	std::string& text = token.spelling;
	take(text);
	while (true)
	{
		const int code_point = reader_.peek();
		if (code_point == kEndOfFile || code_point == '\n')
		{
			throw error("Unterminated header name");
		}
		take(text);
		if (code_point == close)
		{
			break;
		}
	}

	token.type = PPTokenType::HeaderName;
}

void PPTokenLexer::scan_identifier_or_literal(PPToken& token)
{
	// The identifier is scanned into the token, so an encoding-prefix is
	// already in place when it turns out to introduce a literal.
	std::string& text = token.spelling;
	scan_identifier_text(text);

	const int next = reader_.peek();
	if (next == '"' || next == '\'')
	{
		const SpellingId prefix = spelling_id(text);
		if (next == '"' && is_string_literal_prefix(prefix))
		{
			if (is_raw_string_prefix(text))
			{
				scan_raw_string_literal(token);
			}
			else
			{
				scan_string_literal(token);
			}
			return;
		}
		if (next == '\'' && is_character_literal_prefix(prefix))
		{
			scan_character_literal(token);
			return;
		}
	}

	token.type = is_identifier_like_operator(text)
		? PPTokenType::PreprocessingOpOrPunc
		: PPTokenType::Identifier;
}

void PPTokenLexer::scan_identifier_text(std::string& text)
{
	take(text);
	while (is_identifier_continue(reader_.peek()))
	{
		take(text);
	}
}

void PPTokenLexer::scan_pp_number(PPToken& token)
{
	std::string& text = token.spelling;
	bool leading_digits = take(text) != '.';
	bool hexadecimal = false;
	if (!leading_digits)
	{
		take(text);
	}
	while (true)
	{
		const int code_point = reader_.peek();
		if (is_exponent_letter(code_point, hexadecimal))
		{
			const int sign = reader_.peek(1);
			if (sign == '+' || sign == '-')
			{
				take(text);
				take(text);
				leading_digits = false;
				continue;
			}
		}
		if (code_point != '.' && !is_identifier_continue(code_point))
		{
			break;
		}
		if (leading_digits)
		{
			if (code_point == 'x' || code_point == 'X')
			{
				hexadecimal = true;
				leading_digits = false;
			}
			else if (ends_leading_digits(code_point, reader_.peek(1)))
			{
				leading_digits = false;
			}
		}
		take(text);
	}
	token.type = PPTokenType::PPNumber;
}

void PPTokenLexer::scan_character_literal(PPToken& token)
{
	// The spelling already holds the encoding-prefix, if the literal has one.
	std::string& text = token.spelling;
	take(text);

	// An empty c-char-sequence is left for phase 7 to reject, so that phase 3
	// still yields the token the diagnostic will be attached to.
	while (true)
	{
		const int code_point = reader_.peek();
		if (code_point == kEndOfFile || code_point == '\n')
		{
			throw error("Unterminated character literal");
		}
		if (code_point == '\'')
		{
			take(text);
			break;
		}
		if (code_point == '\\')
		{
			scan_escape_sequence(text);
		}
		else
		{
			take(text);
		}
	}

	token.type = scan_ud_suffix(text)
		? PPTokenType::UserDefinedCharacterLiteral
		: PPTokenType::CharacterLiteral;
}

void PPTokenLexer::scan_string_literal(PPToken& token)
{
	// The spelling already holds the encoding-prefix, if the literal has one.
	std::string& text = token.spelling;
	take(text);

	while (true)
	{
		const int code_point = reader_.peek();
		if (code_point == kEndOfFile || code_point == '\n')
		{
			throw error("Unterminated string literal");
		}
		if (code_point == '"')
		{
			take(text);
			break;
		}
		if (code_point == '\\')
		{
			scan_escape_sequence(text);
		}
		else
		{
			take(text);
		}
	}

	token.type = scan_ud_suffix(text)
		? PPTokenType::UserDefinedStringLiteral
		: PPTokenType::StringLiteral;
}

void PPTokenLexer::scan_raw_string_literal(PPToken& token)
{
	// The spelling already holds the encoding-prefix, if the literal has one.
	std::string& text = token.spelling;

	// The body of a raw-string-literal is spelled in untranslated characters:
	// no trigraph replacement, no line splicing, no universal-character-names.
	reader_.set_raw_mode(true);
	take(text);

	std::string delimiter;
	scan_raw_string_delimiter(delimiter);
	text += delimiter;
	take(text);

	const std::string terminator = ")" + delimiter + "\"";
	const std::size_t body_start = text.size();
	while (true)
	{
		const int code_point = reader_.peek();
		if (code_point == kEndOfFile)
		{
			throw error("Unterminated raw string literal");
		}
		take(text);
		if (code_point != '"' || text.size() - body_start < terminator.size())
		{
			continue;
		}
		const std::size_t tail = text.size() - terminator.size();
		if (text.compare(tail, terminator.size(), terminator) == 0)
		{
			break;
		}
	}
	reader_.set_raw_mode(false);

	token.type = scan_ud_suffix(text)
		? PPTokenType::UserDefinedStringLiteral
		: PPTokenType::StringLiteral;
}

void PPTokenLexer::scan_raw_string_delimiter(std::string& delimiter)
{
	std::size_t length = 0;
	while (reader_.peek() != '(')
	{
		if (!is_raw_string_delimiter_char(reader_.peek()))
		{
			throw error("Invalid raw string literal delimiter");
		}
		if (length == kMaxRawStringDelimiterLength)
		{
			throw error("Raw string literal delimiter too long");
		}
		take(delimiter);
		++length;
	}
}

void PPTokenLexer::scan_escape_sequence(std::string& text)
{
	take(text);
	const int code_point = reader_.peek();
	if (is_simple_escape(code_point))
	{
		take(text);
		return;
	}
	if (is_octal_digit(code_point))
	{
		for (int digit = 0; digit < 3 && is_octal_digit(reader_.peek()); ++digit)
		{
			take(text);
		}
		return;
	}
	if (code_point == 'x')
	{
		take(text);
		if (!is_hex_digit(reader_.peek()))
		{
			throw error("Invalid hexadecimal escape sequence");
		}
		while (is_hex_digit(reader_.peek()))
		{
			take(text);
		}
		return;
	}
	throw error("Invalid escape sequence");
}

bool PPTokenLexer::scan_ud_suffix(std::string& text)
{
	if (!is_identifier_start(reader_.peek()))
	{
		return false;
	}
	scan_identifier_text(text);
	return true;
}

bool PPTokenLexer::scan_op_or_punc(PPToken& token)
{
	// See 2.5.3: `<::` not followed by `:` or `>` is `<` then `::`, so that
	// `vector<::std::string>` does not open with the digraph `<:`.
	if (reader_.peek() == '<' && reader_.peek(1) == ':' && reader_.peek(2) == ':')
	{
		const int after = reader_.peek(3);
		if (after != ':' && after != '>')
		{
			reader_.advance();
			token.spelling = "<";
			token.type = PPTokenType::PreprocessingOpOrPunc;
			return true;
		}
	}

	char candidate[kMaxOperatorLength];
	std::size_t available = 0;
	SpellingId id = 0;
	while (available < kMaxOperatorLength)
	{
		const int code_point = reader_.peek(available);
		if (code_point <= 0 || code_point > 0x7F)
		{
			break;
		}
		candidate[available] = static_cast<char>(code_point);
		id = (id << 8) | static_cast<unsigned int>(code_point);
		++available;
	}

	// Maximal munch: drop the last character of the candidate until what is
	// left is a spelling, which is one shift of the packed form.
	for (std::size_t length = available; length > 0; --length, id >>= 8)
	{
		if (!is_operator_or_punctuator(id))
		{
			continue;
		}
		for (std::size_t index = 0; index < length; ++index)
		{
			reader_.advance();
		}
		token.spelling.assign(candidate, length);
		token.type = PPTokenType::PreprocessingOpOrPunc;
		return true;
	}
	return false;
}

int PPTokenLexer::take(std::string& text)
{
	const int code_point = reader_.peek();
	reader_.advance();
	append_utf8(text, code_point);
	return code_point;
}

void PPTokenLexer::update_directive_state(const PPToken& token)
{
	// See 16.2: the directive is `# include < h-char-sequence > new-line`, so
	// exactly the one token after a line-leading `#` `include` is a
	// header-name, and a `<` opened there has to be closed.
	switch (directive_)
	{
	case DirectiveState::LineStart:
		directive_ = token.type == PPTokenType::PreprocessingOpOrPunc &&
			(token.spelling == "#" || token.spelling == "%:")
			? DirectiveState::AfterHash
			: DirectiveState::Other;
		break;
	case DirectiveState::AfterHash:
		directive_ = token.type == PPTokenType::Identifier && token.spelling == "include"
			? DirectiveState::AfterInclude
			: DirectiveState::Other;
		break;
	case DirectiveState::AfterInclude:
		directive_ = DirectiveState::Other;
		break;
	default:
		break;
	}
}

SourceError PPTokenLexer::error(const std::string& message) const
{
	return SourceError(reader_.position_text() + ":" + message);
}

void emit_pp_token(IPPTokenStream& output, const PPToken& token)
{
	switch (token.type)
	{
	case PPTokenType::WhitespaceSequence: output.emit_whitespace_sequence(); return;
	case PPTokenType::NewLine: output.emit_new_line(); return;
	case PPTokenType::HeaderName: output.emit_header_name(token.spelling); return;
	case PPTokenType::Identifier: output.emit_identifier(token.spelling); return;
	case PPTokenType::PPNumber: output.emit_pp_number(token.spelling); return;
	case PPTokenType::CharacterLiteral: output.emit_character_literal(token.spelling); return;
	case PPTokenType::UserDefinedCharacterLiteral:
		output.emit_user_defined_character_literal(token.spelling);
		return;
	case PPTokenType::StringLiteral: output.emit_string_literal(token.spelling); return;
	case PPTokenType::UserDefinedStringLiteral:
		output.emit_user_defined_string_literal(token.spelling);
		return;
	case PPTokenType::PreprocessingOpOrPunc:
		output.emit_preprocessing_op_or_punc(token.spelling);
		return;
	case PPTokenType::NonWhitespaceChar: output.emit_non_whitespace_char(token.spelling); return;
	case PPTokenType::EndOfFile: output.emit_eof(); return;
	}
}
