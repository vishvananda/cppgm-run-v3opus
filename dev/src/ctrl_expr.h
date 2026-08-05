#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "post_token.h"
#include "pptoken_lexer.h"
#include "token_model.h"

// 16.1 Conditional inclusion: the controlling expression of `#if` and `#elif`.
//
// A controlling expression is evaluated in preprocessing-token context, so a
// keyword is still an identifier and the only operators are the ones phase 3
// already recognizes.  16.2.4 makes every signed type act as `intmax_t` and
// every unsigned type as `uintmax_t`, so a value here is a 64-bit pattern plus
// a signedness and nothing else.
//
// PA3 supplies a mock `defined`; PA4 and PA5 will supply the macro table.

struct CtrlExprValue
{
	unsigned long long bits;
	bool is_signed;
};

// Whether a macro name is defined, which is what the `defined` operator asks.
typedef bool (*CtrlExprIsDefined)(const std::string& identifier);

// One logical line at a time: `add` accumulates the preprocessing-tokens of a
// line, `evaluate` parses and evaluates them.  The caller keeps one evaluator
// and calls `begin_line` between lines, so the token vector and the identifier
// arena are grown once and reused for the whole file.
class CtrlExprEvaluator
{
public:
	explicit CtrlExprEvaluator(CtrlExprIsDefined is_defined);

	// Discards the accumulated line and starts an empty one.
	void begin_line();

	// Adds one preprocessing-token of the current line.  Whitespace-sequence
	// and new-line tokens are the caller's business, because they are what
	// delimits a line rather than part of one.
	void add(const PPToken& token);

	// True when the line has no preprocessing-tokens at all, which is the one
	// case the assignment reports nothing for.  A line whose only token is
	// rejected is not empty: it reports `error`.
	bool line_empty() const { return tokens_.empty() && !has_invalid_; }

	// Parses and evaluates the accumulated line.  False when the line is not a
	// valid controlling expression, which includes a division, modulus or
	// shift the assignment course-defines as an error.
	bool evaluate(CtrlExprValue& result);

private:
	enum class TokenKind : unsigned char
	{
		Operator,
		Identifier,
		Value
	};

	// One analysed token, packed into 16 bytes.  A logical line can hold as
	// many tokens as the file has bytes, so this record is what the stage
	// scales in: an identifier keeps a span in `names_` rather than a string
	// of its own, and shares those bytes with a literal's value because no
	// token is both.
	struct Token
	{
		TokenKind kind;
		unsigned char op;  // ETokenType, for an Operator
		bool is_signed;    // for a Value
		union
		{
			unsigned long long bits;  // for a Value
			struct
			{
				unsigned begin;
				unsigned size;
			} name;
		};
	};

	void add_literal(const std::string& spelling, bool character_literal);
	Token& push(TokenKind kind);

	const Token* peek() const;
	bool take_operator(ETokenType op);
	bool name_equals(const Token& token, const char* text, std::size_t size) const;

	// Each returns false when the line does not match the grammar.  `live` is
	// false inside the operand of `?:`, `&&` or `||` that is not evaluated:
	// such an operand is still parsed and still typed, but the errors of its
	// operators are not reported.
	bool parse_conditional(bool live, CtrlExprValue& out);
	bool parse_binary(int min_precedence, bool live, CtrlExprValue& out);
	bool parse_unary(bool live, CtrlExprValue& out);
	bool parse_primary(bool live, CtrlExprValue& out);
	bool parse_defined(CtrlExprValue& out);

	CtrlExprIsDefined is_defined_;

	// The current line.
	std::vector<Token> tokens_;
	std::string names_;
	bool has_invalid_;

	// The parse of the current line.
	std::size_t position_;
	unsigned depth_;

	// Reused scratch: the analysed form of one literal, and the operand of one
	// `defined`, which is the one place a name has to become a `std::string`.
	PostToken literal_;
	std::string defined_operand_;
};
