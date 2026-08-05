#include "ctrl_expr.h"

#include <climits>

#include "literal_scan.h"

namespace
{

// The binary operators of the assignment's grammar, with the precedence its
// twelve left-recursive productions give them.  A higher number binds tighter;
// zero is not a binary operator at all.  The productions differ only in the
// operators they name and the level they sit at, so the whole cascade is this
// one list and the loop in parse_binary.
#define CPPGM_CTRL_EXPR_BINARY_OPERATORS(X) \
	X(OP_LOR, 1) \
	X(OP_LAND, 2) \
	X(OP_BOR, 3) \
	X(OP_XOR, 4) \
	X(OP_AMP, 5) \
	X(OP_EQ, 6) X(OP_NE, 6) \
	X(OP_LT, 7) X(OP_GT, 7) X(OP_LE, 7) X(OP_GE, 7) \
	X(OP_LSHIFT, 8) X(OP_RSHIFT, 8) \
	X(OP_PLUS, 9) X(OP_MINUS, 9) \
	X(OP_STAR, 10) X(OP_DIV, 10) X(OP_MOD, 10)

const int kLowestPrecedence = 1;

// A token holds its operator in one byte, which the vocabulary has to fit in.
static_assert(kTokenTypeCount <= 255, "a token type has to fit in one byte");

// The most negative value of the signed 64-bit type 16.2.4 promotes to.
const unsigned long long kMostNegativeSigned = 1ULL << 63;

int binary_precedence(ETokenType op)
{
	switch (op)
	{
#define CPPGM_CTRL_EXPR_BINARY_PRECEDENCE(name, precedence) case name: return precedence;
	CPPGM_CTRL_EXPR_BINARY_OPERATORS(CPPGM_CTRL_EXPR_BINARY_PRECEDENCE)
#undef CPPGM_CTRL_EXPR_BINARY_PRECEDENCE
	default:
		return 0;
	}
}

bool is_unary_operator(ETokenType op)
{
	return op == OP_PLUS || op == OP_MINUS || op == OP_LNOT || op == OP_COMPL;
}

CtrlExprValue make_signed(unsigned long long bits)
{
	CtrlExprValue value;
	value.bits = bits;
	value.is_signed = true;
	return value;
}

CtrlExprValue apply_unary(ETokenType op, CtrlExprValue operand)
{
	CtrlExprValue result = operand;
	switch (op)
	{
	case OP_PLUS:
		break;
	case OP_MINUS:
		// Signed arithmetic is done on the two's complement pattern, which is
		// the wrap the course defines and which no operand can trap on.
		result.bits = 0ULL - operand.bits;
		break;
	case OP_COMPL:
		result.bits = ~operand.bits;
		break;
	default:
		// 5.3.1: the operand of `!` is contextually converted to bool and the
		// result is bool, which the course ABI makes signed, so the operand's
		// own signedness does not survive.
		result = make_signed(operand.bits == 0 ? 1 : 0);
		break;
	}
	return result;
}

// 5.9 and 5.10 apply the usual arithmetic conversions before comparing, so a
// signed operand beside an unsigned one is compared as unsigned.
bool compare(ETokenType op, CtrlExprValue left, CtrlExprValue right)
{
	int order;
	if (left.is_signed && right.is_signed)
	{
		const long long a = static_cast<long long>(left.bits);
		const long long b = static_cast<long long>(right.bits);
		order = a < b ? -1 : (a > b ? 1 : 0);
	}
	else
	{
		order = left.bits < right.bits ? -1 : (left.bits > right.bits ? 1 : 0);
	}

	switch (op)
	{
	case OP_EQ:
		return order == 0;
	case OP_NE:
		return order != 0;
	case OP_LT:
		return order < 0;
	case OP_GT:
		return order > 0;
	case OP_LE:
		return order <= 0;
	default:
		return order >= 0;
	}
}

// `/` and `%`.  The assignment course-defines a zero divisor as an error, and
// the only other pair without a representable quotient is the most negative
// signed value over -1 (5.6/4), which the reference reports the same way.
// `live` is false in an operand the expression does not evaluate, where an
// error is not reported at all.
bool apply_division(ETokenType op, bool is_signed, unsigned long long left,
                    unsigned long long right, bool live, unsigned long long& out)
{
	const bool unrepresentable =
		is_signed && left == kMostNegativeSigned && right == ~0ULL;
	if (right == 0 || unrepresentable)
	{
		out = 0;
		return !live;
	}
	if (is_signed)
	{
		const long long a = static_cast<long long>(left);
		const long long b = static_cast<long long>(right);
		out = static_cast<unsigned long long>(op == OP_DIV ? a / b : a % b);
	}
	else
	{
		out = op == OP_DIV ? left / right : left % right;
	}
	return true;
}

bool apply_arithmetic(ETokenType op, CtrlExprValue left, CtrlExprValue right,
                      bool live, CtrlExprValue& out)
{
	// The usual arithmetic conversions (5.0.10) on two operands 16.2.4 has
	// already widened to 64 bits: the result is signed only if both are.
	out.is_signed = left.is_signed && right.is_signed;
	switch (op)
	{
	case OP_STAR:
		out.bits = left.bits * right.bits;
		return true;
	case OP_PLUS:
		out.bits = left.bits + right.bits;
		return true;
	case OP_MINUS:
		out.bits = left.bits - right.bits;
		return true;
	case OP_AMP:
		out.bits = left.bits & right.bits;
		return true;
	case OP_XOR:
		out.bits = left.bits ^ right.bits;
		return true;
	case OP_BOR:
		out.bits = left.bits | right.bits;
		return true;
	default:
		return apply_division(op, out.is_signed, left.bits, right.bits, live, out.bits);
	}
}

bool apply_shift(ETokenType op, CtrlExprValue left, CtrlExprValue right,
                 bool live, CtrlExprValue& out)
{
	// 5.8: the result has the type of the promoted left operand, so the right
	// operand takes no part in the usual arithmetic conversions.  The
	// assignment course-defines a negative count, and a count of 64 or more,
	// as an error; a negative signed count is already 2^63 or more as a
	// pattern, so one comparison covers both.
	out.is_signed = left.is_signed;
	if (right.bits >= 64)
	{
		out.bits = 0;
		return !live;
	}

	const unsigned count = static_cast<unsigned>(right.bits);
	if (op == OP_LSHIFT)
	{
		out.bits = left.bits << count;
	}
	else if (left.is_signed)
	{
		// Course defined: a right shift of a signed left operand shifts in
		// copies of the sign bit.
		out.bits = static_cast<unsigned long long>(
			static_cast<long long>(left.bits) >> count);
	}
	else
	{
		out.bits = left.bits >> count;
	}
	return true;
}

bool apply_binary(ETokenType op, CtrlExprValue left, CtrlExprValue right,
                  bool live, CtrlExprValue& out)
{
	switch (op)
	{
	case OP_LAND:
		out = make_signed(left.bits != 0 && right.bits != 0 ? 1 : 0);
		return true;
	case OP_LOR:
		out = make_signed(left.bits != 0 || right.bits != 0 ? 1 : 0);
		return true;
	case OP_EQ:
	case OP_NE:
	case OP_LT:
	case OP_GT:
	case OP_LE:
	case OP_GE:
		// 5.9 and 5.10 yield bool, which is signed.
		out = make_signed(compare(op, left, right) ? 1 : 0);
		return true;
	case OP_LSHIFT:
	case OP_RSHIFT:
		return apply_shift(op, left, right, live, out);
	default:
		return apply_arithmetic(op, left, right, live, out);
	}
}

// 5.14 and 5.15: `&&` does not evaluate its right operand when the left one is
// false, and `||` does not when the left one is true.  An operand that is not
// evaluated is still parsed and still typed; only its errors go unreported.
bool right_operand_is_live(ETokenType op, bool live, CtrlExprValue left)
{
	if (op == OP_LAND)
	{
		return live && left.bits != 0;
	}
	if (op == OP_LOR)
	{
		return live && left.bits == 0;
	}
	return live;
}

} // namespace

CtrlExprEvaluator::CtrlExprEvaluator(CtrlExprIsDefined is_defined)
	: is_defined_(is_defined)
	, has_invalid_(false)
	, position_(0)
{}

void CtrlExprEvaluator::begin_line()
{
	tokens_.clear();
	names_.clear();
	has_invalid_ = false;
}

void CtrlExprEvaluator::add(const PPToken& token)
{
	if (has_invalid_)
	{
		// The line already fails, and `line_empty` already knows it, so
		// nothing after the first bad token is worth analysing.
		return;
	}

	switch (token.type)
	{
	case PPTokenType::Identifier:
	{
		// A controlling expression is evaluated in preprocessing-token
		// context, so a keyword is an identifier here.  The 2.13 operators
		// that are spelled as identifiers are not: phase 3 has already
		// classified those as preprocessing-op-or-punc.
		if (names_.size() + token.spelling.size() > UINT_MAX)
		{
			has_invalid_ = true;
			return;
		}
		Token& entry = push(TokenKind::Identifier);
		entry.name.begin = static_cast<unsigned>(names_.size());
		entry.name.size = static_cast<unsigned>(token.spelling.size());
		names_ += token.spelling;
		break;
	}
	case PPTokenType::PreprocessingOpOrPunc:
	{
		ETokenType op = kTokenTypeCount;
		if (!lookup_simple_token(token.spelling, op))
		{
			// `#`, `##`, `%:` and `%:%:` have no token form at all.
			has_invalid_ = true;
			return;
		}
		push(TokenKind::Operator).op = static_cast<unsigned char>(op);
		break;
	}
	case PPTokenType::PPNumber:
		add_literal(token.spelling, false);
		break;
	case PPTokenType::CharacterLiteral:
	case PPTokenType::UserDefinedCharacterLiteral:
		add_literal(token.spelling, true);
		break;
	default:
		// A string-literal is an array, a header-name and a
		// non-whitespace-character have no token form: none is an
		// integral-literal, so the line is an error.
		has_invalid_ = true;
		break;
	}
}

void CtrlExprEvaluator::add_literal(const std::string& spelling, bool character_literal)
{
	if (character_literal)
	{
		scan_character_literal(spelling, literal_);
	}
	else
	{
		scan_pp_number(spelling, literal_);
	}

	// The assignment accepts an integral-literal only: a literal that is not
	// user defined, not an array, and of one of the integral types.
	if (literal_.kind != PostTokenKind::Literal ||
	    !fundamental_type_is_integral(literal_.type))
	{
		has_invalid_ = true;
		return;
	}

	// 16.2.4, as the assignment course-defines it: the literal takes its phase
	// 7 type first, then widens to intmax_t or uintmax_t.
	Token& entry = push(TokenKind::Value);
	entry.bits = literal_.integer_value();
	entry.is_signed = fundamental_type_is_signed(literal_.type);
}

CtrlExprEvaluator::Token& CtrlExprEvaluator::push(TokenKind kind)
{
	Token entry;
	entry.kind = kind;
	entry.op = static_cast<unsigned char>(kTokenTypeCount);
	entry.is_signed = true;
	entry.bits = 0;
	tokens_.push_back(entry);
	return tokens_.back();
}

const CtrlExprEvaluator::Token* CtrlExprEvaluator::peek() const
{
	return position_ < tokens_.size() ? &tokens_[position_] : 0;
}

bool CtrlExprEvaluator::take_operator(ETokenType op)
{
	const Token* token = peek();
	if (token == 0 || token->kind != TokenKind::Operator || token->op != op)
	{
		return false;
	}
	++position_;
	return true;
}

bool CtrlExprEvaluator::name_equals(const Token& token, const char* text,
                                    std::size_t size) const
{
	return token.name.size == size &&
		names_.compare(token.name.begin, size, text, size) == 0;
}

CtrlExprEvaluator::Pending& CtrlExprEvaluator::push_frame(Frame frame, ETokenType op,
                                                          bool live)
{
	Pending entry;
	entry.frame = frame;
	entry.op = static_cast<unsigned char>(op);
	entry.precedence = 0;
	entry.live = live;
	entry.condition = false;
	entry.value_is_signed = true;
	entry.value = 0;
	pending_.push_back(entry);
	return pending_.back();
}

bool CtrlExprEvaluator::evaluate(CtrlExprValue& result)
{
	if (has_invalid_)
	{
		return false;
	}

	position_ = 0;
	pending_.clear();
	bool live = true;
	CtrlExprValue value;
	for (;;)
	{
		if (!parse_operand(live, value))
		{
			return false;
		}
		const Step step = after_operand(live, value);
		if (step == Step::Error)
		{
			return false;
		}
		if (step == Step::Done)
		{
			break;
		}
	}
	result = value;
	return true;
}

// Operand position: `(` and a prefix operator only stack more of it, so both
// are pushed and the next token read, until one primary-expression completes.
bool CtrlExprEvaluator::parse_operand(bool live, CtrlExprValue& value)
{
	for (;;)
	{
		const Token* token = peek();
		if (token == 0)
		{
			return false;
		}
		if (token->kind != TokenKind::Operator)
		{
			return parse_primary(value);
		}

		const ETokenType op = static_cast<ETokenType>(token->op);
		if (op == OP_LPAREN)
		{
			++position_;
			push_frame(Frame::Paren, op, live);
			continue;
		}
		if (!is_unary_operator(op))
		{
			return false;
		}
		++position_;
		push_frame(Frame::Unary, op, live);
	}
}

// Operator position: what follows a complete operand decides which pending
// operators it belongs to.  Returns to operand position for every operator
// that takes one, and reports the end of the line only with nothing pending.
CtrlExprEvaluator::Step CtrlExprEvaluator::after_operand(bool& live, CtrlExprValue& value)
{
	for (;;)
	{
		// A prefix operator binds tighter than anything a binary operator can
		// take, so the ones this operand completes apply right away.
		while (!pending_.empty() && pending_.back().frame == Frame::Unary)
		{
			value = apply_unary(static_cast<ETokenType>(pending_.back().op), value);
			pending_.pop_back();
		}

		const Token* token = peek();
		if (token == 0)
		{
			// The whole logical line has to be one controlling expression, so
			// an unclosed `(` or `?` is as much an error as a leftover token.
			if (!complete_pending(kLowestPrecedence, true, live, value))
			{
				return Step::Error;
			}
			return pending_.empty() ? Step::Done : Step::Error;
		}
		if (token->kind != TokenKind::Operator)
		{
			return Step::Error;
		}

		const ETokenType op = static_cast<ETokenType>(token->op);
		const int precedence = binary_precedence(op);
		if (precedence != 0)
		{
			// The twelve binary productions are all left recursive, so this
			// operator takes everything at least as tight as it is.
			if (!complete_pending(precedence, false, live, value))
			{
				return Step::Error;
			}
			++position_;
			Pending& frame = push_frame(Frame::Binary, op, live);
			frame.precedence = static_cast<unsigned char>(precedence);
			frame.value = value.bits;
			frame.value_is_signed = value.is_signed;
			live = right_operand_is_live(op, live, value);
			return Step::Operand;
		}
		if (op == OP_QMARK)
		{
			// `?:` is right associative, so it takes every binary operator but
			// leaves an enclosing `?:` waiting for its own third operand.
			if (!complete_pending(kLowestPrecedence, false, live, value))
			{
				return Step::Error;
			}
			++position_;
			Pending& frame = push_frame(Frame::Then, op, live);
			frame.condition = value.bits != 0;
			live = live && frame.condition;
			return Step::Operand;
		}
		if (op == OP_COLON)
		{
			if (!complete_pending(kLowestPrecedence, true, live, value) ||
			    pending_.empty() || pending_.back().frame != Frame::Then)
			{
				return Step::Error;
			}
			++position_;
			Pending& frame = pending_.back();
			frame.frame = Frame::Else;
			frame.value = value.bits;
			frame.value_is_signed = value.is_signed;
			live = frame.live && !frame.condition;
			return Step::Operand;
		}
		if (op != OP_RPAREN)
		{
			return Step::Error;
		}
		if (!complete_pending(kLowestPrecedence, true, live, value) ||
		    pending_.empty() || pending_.back().frame != Frame::Paren)
		{
			return Step::Error;
		}
		++position_;
		live = pending_.back().live;
		pending_.pop_back();
	}
}

// Completes every pending operator whose right operand `value` finishes: the
// binary ones at least as tight as `min_precedence`, and, when what follows
// can close one, a `?:` whose third operand `value` is.  Each restores the
// liveness it was pushed in.  False when an operator the line does evaluate is
// one the assignment course-defines as an error.
bool CtrlExprEvaluator::complete_pending(int min_precedence, bool through_conditional,
                                         bool& live, CtrlExprValue& value)
{
	while (!pending_.empty())
	{
		const Pending frame = pending_.back();
		if (frame.frame == Frame::Binary && frame.precedence >= min_precedence)
		{
			CtrlExprValue left;
			left.bits = frame.value;
			left.is_signed = frame.value_is_signed;
			live = frame.live;
			pending_.pop_back();
			if (!apply_binary(static_cast<ETokenType>(frame.op), left, value, live, value))
			{
				return false;
			}
			continue;
		}
		if (through_conditional && frame.frame == Frame::Else)
		{
			// 5.16: both branches take part in the usual arithmetic
			// conversions that give the result its type, whichever one the
			// condition selects.
			value.is_signed = frame.value_is_signed && value.is_signed;
			if (frame.condition)
			{
				value.bits = frame.value;
			}
			live = frame.live;
			pending_.pop_back();
			continue;
		}
		break;
	}
	return true;
}

bool CtrlExprEvaluator::parse_primary(CtrlExprValue& value)
{
	// Operand position has already established that this is neither the end of
	// the line nor an operator.
	const Token* token = peek();
	++position_;
	if (token->kind == TokenKind::Value)
	{
		value.bits = token->bits;
		value.is_signed = token->is_signed;
		return true;
	}
	if (name_equals(*token, "defined", sizeof("defined") - 1))
	{
		return parse_defined(value);
	}
	// Every other identifier or keyword evaluates as 0, except `true`, which
	// the assignment course-defines as 1.  Both have type bool, so both are
	// signed.
	value = make_signed(name_equals(*token, "true", sizeof("true") - 1) ? 1 : 0);
	return true;
}

bool CtrlExprEvaluator::parse_defined(CtrlExprValue& out)
{
	// `defined identifier` and `defined ( identifier )`.  The operand is an
	// identifier in preprocessing-token context, so a keyword is one too.
	const bool parenthesized = take_operator(OP_LPAREN);
	const Token* token = peek();
	if (token == 0 || token->kind != TokenKind::Identifier)
	{
		return false;
	}
	++position_;
	if (parenthesized && !take_operator(OP_RPAREN))
	{
		return false;
	}

	defined_operand_.assign(names_, token->name.begin, token->name.size);
	out = make_signed(is_defined_(defined_operand_) ? 1 : 0);
	return true;
}
