#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "ast_model.h"
#include "sema_template.h"
#include "token_model.h"

// 5.19's constant expression, read out of the spelling 14.2 left it as.
//
// A template-argument-list reaches the semantic layer as text, because 14.2
// writes it inside a name and PA10 dumps a name as it was written.  A type
// argument is turned back into what was written by `sema_type_id.cpp`; this
// file is the same reading for the other kind of argument, and for the same
// reason.  What it answers is a value, over the subset PA20 supports: the
// literals, the id-expressions naming constant bindings, the unary and binary
// operators, `?:`, and the `sizeof`/`alignof`/cast forms that fold to an
// integral value.
//
// The terminals are recovered rather than re-lexed.  Phase 7 wrote them with
// exactly the separators that keep two of them from munching together, so the
// spelling splits back the way `split_type_id` splits a type-id: a name is one
// word however many components and argument lists it carries, and everything
// else is the punctuation the grammar reads.  The split is a fact of the
// spelling alone, so it is kept - one template-id written n times is split
// once - and only the evaluation is redone against the region that reads it.

namespace
{

bool is_name_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '$';
}

// The end of a balanced run that `spelling[at]` opens, which is the one scan
// `sema_type_id.cpp` asks of a spelling too and is answered in one place.
std::string::size_type balanced_end(const std::string& spelling,
                                    std::string::size_type at)
{
	return spelling_balanced_end(spelling, at);
}

// 2.14.3 and 2.14.5: the end of a literal that opens at `at`, which is the one
// spelling that holds a quote and an escape rather than a run of name
// characters.
std::string::size_type quoted_end(const std::string& spelling,
                                  std::string::size_type at)
{
	const char quote = spelling[at];
	for (++at; at < spelling.size(); ++at)
	{
		if (spelling[at] == '\\')
		{
			++at;
			continue;
		}
		if (spelling[at] == quote)
		{
			// 2.14.8's ud-suffix closes up with the literal it belongs to.
			for (++at; at < spelling.size() && is_name_char(spelling[at]); ++at)
			{
			}
			return at;
		}
	}
	return std::string::npos;
}

// The operators 5.19 writes, longest first, so that `<<` is never read as two
// `<` and `>=` never as `>` and `=`.
const char* const kOperators[] = {
	"<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "::",
	"+", "-", "*", "/", "%", "<", ">", "&", "|", "^", "!", "~", "?", ":",
	"(", ")", "[", "]", ","
};

const std::size_t kOperatorCount = sizeof(kOperators) / sizeof(kOperators[0]);

// The terminals of one constant expression, as PA10's spelling holds them.
//
// A word that opens with a name character is a name, and takes with it every
// template-argument-list, `decltype` operand and `::` that closes up with it -
// which is what makes `A<3>::value` one word and one lookup.  A word that opens
// with a quote is a literal, and everything else is one of 5.19's operators.
bool split_value_expression(const std::string& spelling,
                            std::vector<std::string>& out)
{
	std::string::size_type at = 0;
	while (at < spelling.size())
	{
		const char c = spelling[at];
		if (c == ' ')
		{
			++at;
			continue;
		}
		if (c == '\'' || c == '"')
		{
			const std::string::size_type end = quoted_end(spelling, at);
			if (end == std::string::npos)
			{
				return false;
			}
			out.push_back(spelling.substr(at, end - at));
			at = end;
			continue;
		}
		// 3.4.3p1: a name written `::x` is one word too, and the empty first
		// component is what says the global namespace - which is as much a
		// constant binding's name as any other.
		const bool rooted = !is_name_char(c) &&
			spelling.compare(at, 2, "::") == 0;
		if (is_name_char(c) || rooted)
		{
			const std::string::size_type start = at;
			at += rooted ? 2 : 0;
			for (;;)
			{
				while (at < spelling.size() && is_name_char(spelling[at]))
				{
					++at;
				}
				// 14.2 writes a template-argument-list after a name and
				// 7.1.6.2p1 a decltype-specifier's expression, each of which
				// belongs to the name it closes up with.  A `<` that opens no
				// balanced run is 5.9's operator instead.
				if (at < spelling.size() &&
				    ((spelling[at] == '<' &&
				      spelling.compare(start, at - start, "operator") != 0) ||
				     (spelling[at] == '(' &&
				      spelling.compare(start, at - start, "decltype") == 0)))
				{
					const std::string::size_type closed =
						balanced_end(spelling, at);
					if (closed != std::string::npos)
					{
						at = closed;
					}
				}
				if (at + 1 < spelling.size() && spelling[at] == ':' &&
				    spelling[at + 1] == ':')
				{
					at += 2;
					continue;
				}
				break;
			}
			out.push_back(spelling.substr(start, at - start));
			continue;
		}
		std::size_t chosen = kOperatorCount;
		for (std::size_t index = 0; index < kOperatorCount; ++index)
		{
			const std::string op(kOperators[index]);
			if (spelling.compare(at, op.size(), op) == 0)
			{
				chosen = index;
				break;
			}
		}
		if (chosen == kOperatorCount)
		{
			return false;
		}
		out.push_back(kOperators[chosen]);
		at += out.back().size();
	}
	return !out.empty();
}

// 5.19 over the operators PA20 folds, as the precedence each binds at.  Zero is
// no binary operator at all.
unsigned precedence_of(const std::string& word)
{
	if (word == "*" || word == "/" || word == "%") return 10;
	if (word == "+" || word == "-") return 9;
	if (word == "<<" || word == ">>") return 8;
	if (word == "<" || word == ">" || word == "<=" || word == ">=") return 7;
	if (word == "==" || word == "!=") return 6;
	if (word == "&") return 5;
	if (word == "^") return 4;
	if (word == "|") return 3;
	if (word == "&&") return 2;
	if (word == "||") return 1;
	return 0;
}

unsigned token_of(const std::string& word)
{
	if (word == "*") return OP_STAR;
	if (word == "/") return OP_DIV;
	if (word == "%") return OP_MOD;
	if (word == "+") return OP_PLUS;
	if (word == "-") return OP_MINUS;
	if (word == "<<") return OP_LSHIFT;
	if (word == ">>") return OP_RSHIFT;
	if (word == "<") return OP_LT;
	if (word == ">") return OP_GT;
	if (word == "<=") return OP_LE;
	if (word == ">=") return OP_GE;
	if (word == "==") return OP_EQ;
	if (word == "!=") return OP_NE;
	if (word == "&") return OP_AMP;
	if (word == "^") return OP_XOR;
	if (word == "|") return OP_BOR;
	if (word == "&&") return OP_LAND;
	if (word == "||") return OP_LOR;
	return 0;
}

bool is_cast_word(const std::string& word)
{
	return word.compare(0, 12, "static_cast<") == 0 ||
		word.compare(0, 17, "reinterpret_cast<") == 0 ||
		word.compare(0, 11, "const_cast<") == 0;
}

// The type-id one of 5.2.9's casts wrote inside its own angle brackets.
std::string cast_target(const std::string& word)
{
	const std::string::size_type open = word.find('<');
	return word.substr(open + 1, word.size() - open - 2);
}



}

// 5.19's constant expression read out of one template-argument spelling.
//
// It is a reading of its own rather than a walk of the analyzer's, because what
// it reads is text: where in the words it stands, and whether anything it
// named is one an argument list has yet to settle.  The arithmetic, the
// lookups and 8.1p1's type-id are the analyzer's, so the reader borrows it.
class TemplateArgumentReader
{
public:
	TemplateArgumentReader(SemaAnalyzer& analyzer, const SemaContext& ctx)
		: analyzer_(analyzer)
		, ctx_(ctx)
		, at_(0)
		, dependent_(false)
	{}

	SemaConstant read(const std::vector<std::string>& words)
	{
		return expression(words, 0, true);
	}

	// True where the words the spelling split into are all read, which is what
	// says the spelling was one constant expression and not a prefix of one.
	bool finished(const std::vector<std::string>& words) const
	{
		return at_ == words.size();
	}

	// 14.6.2p2: whether the reading stood a value in the place of one an
	// argument list has yet to settle.
	bool dependent() const { return dependent_; }

private:
	SemaConstant expression(const std::vector<std::string>& words,
	                        unsigned bind, bool live);
	SemaConstant unary(const std::vector<std::string>& words, bool live);
	SemaConstant type_trait(const std::string& word,
	                        const std::vector<std::string>& words, bool live);
	SemaConstant name(const std::string& spelling, bool live);
	SemaConstant cast(TypeId target, const SemaConstant& operand);
	SemaConstant binary(unsigned op, const SemaConstant& left,
	                    const SemaConstant& right);
	TypeId probe_type_id(const std::string& spelling);

	SemaAnalyzer& analyzer_;
	const SemaContext& ctx_;
	std::size_t at_;
	bool dependent_;
};

// 5.19 read from the words of one spelling.
//
// `live` is what 5.14p1, 5.15p1 and 5.16p1 leave off: an operand the answer
// does not depend on is read for its shape and not for its value, so a division
// by zero written in the arm a condition did not take is no error.
SemaConstant TemplateArgumentReader::expression(
	const std::vector<std::string>& words, unsigned bind, bool live)
{
	SemaConstant left = unary(words, live);
	for (;;)
	{
		if (at_ >= words.size())
		{
			return left;
		}
		if (words[at_] == "?")
		{
			if (bind > 0)
			{
				return left;
			}
			// 5.16p1: the second and third operands, of which only the one the
			// condition chose is evaluated.
			++at_;
			const bool taken = live && left.bits != 0;
			const SemaConstant chosen =
				expression(words, 0, live && taken);
			if (at_ >= words.size() || words[at_] != ":")
			{
				throw NotConstant("a conditional expression written as a "
				                  "template argument has no `:`");
			}
			++at_;
			const SemaConstant other =
				expression(words, 0, live && !taken);
			if (!live)
			{
				return chosen;
			}
			// 5.16p6: the result is one type however the condition came out,
			// which for two integral operands is the usual conversions.
			const SemaConstant one = analyzer_.promote(chosen);
			const SemaConstant two = analyzer_.promote(other);
			const TypeId type = analyzer_.common_type(one.type, two.type);
			return analyzer_.convert(taken ? one : two, type);
		}
		const unsigned precedence = precedence_of(words[at_]);
		if (precedence == 0 || precedence < bind)
		{
			return left;
		}
		const std::string op = words[at_];
		++at_;
		if (op == "&&" || op == "||")
		{
			// 5.14p1 and 5.15p1: the right operand is evaluated only where the
			// left one does not decide the answer.
			const bool decided = live && (left.bits != 0) == (op == "||");
			const SemaConstant right = expression(words, precedence + 1, live && !decided);
			SemaConstant out;
			out.type = analyzer_.types_.fundamental(FT_BOOL);
			out.bits = decided ? (op == "||" ? 1 : 0)
			                   : (right.bits != 0 ? 1 : 0);
			left = out;
			continue;
		}
		const SemaConstant right =
			expression(words, precedence + 1, live);
		left = live ? binary(token_of(op), left, right) : left;
	}
}

// 5.19 over one written operand: a literal, a name, a parenthesized expression,
// a unary operator, or one of the folds `sizeof`, `alignof` and a cast write.
SemaConstant TemplateArgumentReader::unary(
	const std::vector<std::string>& words, bool live)
{
	if (at_ >= words.size())
	{
		throw NotConstant("a constant expression written as a template "
		                  "argument ends where an operand belongs");
	}
	const std::string word = words[at_];
	if (word == "+" || word == "-" || word == "!" || word == "~")
	{
		++at_;
		const SemaConstant operand = unary(words, live);
		if (!live)
		{
			return operand;
		}
		const SemaConstant promoted = analyzer_.promote(operand);
		SemaConstant out;
		out.type = promoted.type;
		if (word == "+")
		{
			out.bits = promoted.bits;
		}
		else if (word == "-")
		{
			out.bits = 0ULL - promoted.bits;
		}
		else if (word == "~")
		{
			out.bits = ~promoted.bits;
		}
		else
		{
			out.type = analyzer_.types_.fundamental(FT_BOOL);
			out.bits = promoted.bits == 0 ? 1 : 0;
			return out;
		}
		return analyzer_.convert(out, out.type);
	}
	if (word == "(")
	{
		++at_;
		// 5.4p2: a parenthesized type-id followed by an operand is a cast, and
		// everything else is 5.1.1p6's grouping.  Which one was written is
		// settled by what the parentheses hold, exactly as the grammar settles
		// it: a spelling that names no type cannot have been a cast.
		const std::size_t opened = at_;
		std::size_t depth = 1;
		std::size_t close = at_;
		for (; close < words.size(); ++close)
		{
			depth += words[close] == "(" ? 1 : (words[close] == ")" ? -1 : 0);
			if (depth == 0)
			{
				break;
			}
		}
		if (close >= words.size())
		{
			throw NotConstant("a constant expression written as a template "
			                  "argument does not close a parenthesis");
		}
		if (close + 1 < words.size())
		{
			std::string held;
			for (std::size_t index = opened; index < close; ++index)
			{
				held += (index == opened ? "" : " ") + words[index];
			}
			const TypeId target = probe_type_id(held);
			if (target != kNoType)
			{
				at_ = close + 1;
				const SemaConstant operand =
					unary(words, live);
				return live ? cast(target, operand) : operand;
			}
		}
		const SemaConstant inner =
			expression(words, 0, live);
		if (at_ >= words.size() || words[at_] != ")")
		{
			throw NotConstant("a constant expression written as a template "
			                  "argument does not close a parenthesis");
		}
		++at_;
		return inner;
	}
	++at_;
	if (word == "true" || word == "false")
	{
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = word == "true" ? 1 : 0;
		return out;
	}
	if (!word.empty() && (word[0] == '\'' || word[0] == '"' ||
	                      (word[0] >= '0' && word[0] <= '9')))
	{
		return analyzer_.literal_constant(word);
	}
	if (word == "sizeof" || word == "alignof")
	{
		return type_trait(word, words, live);
	}
	if (is_cast_word(word))
	{
		// 5.2.9p1: the target is written inside the cast's own angle brackets,
		// and the operand is the parenthesized expression after it.
		const TypeId target = analyzer_.template_argument_type(cast_target(word), ctx_);
		if (at_ >= words.size() || words[at_] != "(")
		{
			throw NotConstant("a cast written as a template argument has no "
			                  "operand");
		}
		++at_;
		const SemaConstant operand =
			expression(words, 0, live);
		if (at_ >= words.size() || words[at_] != ")")
		{
			throw NotConstant("a cast written as a template argument does not "
			                  "close its operand");
		}
		++at_;
		return live ? cast(target, operand) : operand;
	}
	if (at_ < words.size() && words[at_] == "(")
	{
		// 5.2.3p1: `T(x)` is the same cast written in functional notation, and
		// 5.2.3p2's `T()` is the zero 8.5p7 value-initializes with.  What tells
		// either from a call is whether the word names a type, which is the
		// question 5.4p2 above is settled by too.
		const TypeId target = probe_type_id(word);
		if (target != kNoType && analyzer_.arithmetic_type(target) != kNoType)
		{
			++at_;
			if (at_ < words.size() && words[at_] == ")")
			{
				++at_;
				SemaConstant zero;
				zero.type = target;
				zero.bits = 0;
				return zero;
			}
			const SemaConstant operand = expression(words, 0, live);
			if (at_ >= words.size() || words[at_] != ")")
			{
				throw NotConstant("a conversion written as a template argument "
				                  "does not close its operand");
			}
			++at_;
			return live ? cast(target, operand) : operand;
		}
	}
	return name(word, live);
}

// 5.3.3p1 and 5.3.6p1 over a type-id, which is where a template argument writes
// them.  5.3.3p1's other arm is an expression, and what it asks of one is its
// type - so a name that is no type-id is looked up and answered for.
SemaConstant TemplateArgumentReader::type_trait(
	const std::string& word, const std::vector<std::string>& words, bool live)
{
	if (at_ >= words.size() || words[at_] != "(")
	{
		throw NotConstant(word + " written as a template argument has no "
		                  "parenthesized operand");
	}
	std::size_t depth = 0;
	std::size_t close = at_;
	std::string held;
	for (; close < words.size(); ++close)
	{
		depth += words[close] == "(" ? 1 : (words[close] == ")" ? -1 : 0);
		if (depth == 0)
		{
			break;
		}
		if (close != at_)
		{
			held += (held.empty() ? "" : " ") + words[close];
		}
	}
	if (close >= words.size())
	{
		throw NotConstant(word + " written as a template argument does not "
		                  "close its operand");
	}
	at_ = close + 1;
	TypeId type = probe_type_id(held);
	if (type == kNoType)
	{
		// 5.3.3p1: the operand is an expression, and its *type* is what the
		// clause asks for - which for the id-expressions this milestone folds
		// is the declaration's own.
		SemaEntity* const named = analyzer_.resolve(held, ctx_, LookupKind::Any);
		if (named == nullptr)
		{
			throw NotConstant(held + " is written inside " + word +
			                  " as a template argument and names no type");
		}
		type = named->type;
	}
	if (!live)
	{
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_UNSIGNED_LONG_INT);
		out.bits = 1;
		return out;
	}
	if (analyzer_.types_.is_dependent(type))
	{
		// 14.6p8: how large a type an argument list has yet to settle is, that
		// list is what says - so the argument this stands in is dependent_ too.
		dependent_ = true;
		++analyzer_.stood_in_;
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_UNSIGNED_LONG_INT);
		out.bits = 1;
		return out;
	}
	SemaConstant out;
	out.type = analyzer_.types_.fundamental(FT_UNSIGNED_LONG_INT);
	out.bits = word == "sizeof" ? analyzer_.size_of(type) : analyzer_.types_.object_align(type);
	return out;
}

// 8.1p1's type-id where the spelling is one, and `kNoType` where it is not.
// 5.4p2's ambiguity is settled by asking, so the ask itself cannot be an error.
TypeId TemplateArgumentReader::probe_type_id(const std::string& spelling)
{
	if (spelling.empty())
	{
		return kNoType;
	}
	try
	{
		const ReadingDepth probing(analyzer_.checking_);
		return analyzer_.template_argument_type(spelling, ctx_);
	}
	catch (const std::exception&)
	{
		return kNoType;
	}
}

// 5.19p2: an id-expression naming a constant binding, which is what an
// enumerator, a const object of integral type with a constant initializer, and
// 14.1p4's non-type parameter each are.
SemaConstant TemplateArgumentReader::name(const std::string& spelling,
                                          bool live)
{
	SemaEntity* const named = analyzer_.resolve(spelling, ctx_, LookupKind::Any);
	if (named == nullptr)
	{
		throw NotConstant(spelling + " is written as a template argument and "
		                  "names no constant");
	}
	if (named->constant)
	{
		SemaConstant out;
		out.type = named->type;
		out.bits = named->value;
		return out;
	}
	if (analyzer_.types_.is_dependent(named->type) ||
	    analyzer_.types_.parameter_value_type(named->type) != kNoType)
	{
		// 14.6.2p2: what a name that depends on a template parameter is worth,
		// an argument list is what says - so the argument this stands in is
		// dependent_ and the value is never read.
		dependent_ = true;
		++analyzer_.stood_in_;
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_INT);
		out.bits = 1;
		return out;
	}
	if (!live)
	{
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_INT);
		out.bits = 0;
		return out;
	}
	throw NotConstant(spelling + " is written as a template argument and is "
	                  "not a constant expression");
}

// 5.4p4 and 4.7: an integral value read as another integral type.
SemaConstant TemplateArgumentReader::cast(TypeId target,
                                          const SemaConstant& operand)
{
	if (analyzer_.arithmetic_type(target) == kNoType)
	{
		throw NotConstant("a cast written as a template argument names a type "
		                  "that is not integral");
	}
	SemaConstant out = analyzer_.convert(operand, target);
	out.type = target;
	return out;
}

// 5.6 through 5.15 over two values, which is 5.19's own arithmetic.  It is the
// same arithmetic `binary_constant` does over a tree; what differs is only
// where the operands came from.
SemaConstant TemplateArgumentReader::binary(unsigned op,
                                            const SemaConstant& left_value,
                                            const SemaConstant& right_value)
{
	const SemaConstant left = analyzer_.promote(left_value);
	const SemaConstant right = analyzer_.promote(right_value);
	const bool comparison = op == OP_LT || op == OP_GT || op == OP_LE ||
		op == OP_GE || op == OP_EQ || op == OP_NE;
	// 5.8p1: a shift takes 5p10's conversions on neither operand; the result
	// has the type of the promoted left one.
	const bool shifted = op == OP_LSHIFT || op == OP_RSHIFT;
	const TypeId type =
		shifted ? left.type : analyzer_.common_type(left.type, right.type);
	const unsigned long long lhs = analyzer_.convert(left, type).bits;
	const unsigned long long rhs =
		shifted ? right.bits : analyzer_.convert(right, type).bits;
	const bool sign = analyzer_.is_signed(type);
	const long long signed_lhs = static_cast<long long>(lhs);
	const long long signed_rhs = static_cast<long long>(rhs);
	if (comparison)
	{
		bool answer = false;
		switch (op)
		{
		case OP_LT: answer = sign ? signed_lhs < signed_rhs : lhs < rhs; break;
		case OP_GT: answer = sign ? signed_lhs > signed_rhs : lhs > rhs; break;
		case OP_LE: answer = sign ? signed_lhs <= signed_rhs : lhs <= rhs; break;
		case OP_GE: answer = sign ? signed_lhs >= signed_rhs : lhs >= rhs; break;
		case OP_EQ: answer = lhs == rhs; break;
		default: answer = lhs != rhs; break;
		}
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = answer ? 1 : 0;
		return out;
	}
	SemaConstant out;
	out.type = type;
	switch (op)
	{
	case OP_PLUS: out.bits = lhs + rhs; break;
	case OP_MINUS: out.bits = lhs - rhs; break;
	case OP_STAR: out.bits = lhs * rhs; break;

	case OP_DIV:
	case OP_MOD:
		if (rhs == 0)
		{
			throw NotConstant("a constant expression written as a template "
			                  "argument divides by zero");
		}
		if (sign)
		{
			out.bits = static_cast<unsigned long long>(
				op == OP_DIV ? signed_lhs / signed_rhs
				             : signed_lhs % signed_rhs);
			break;
		}
		out.bits = op == OP_DIV ? lhs / rhs : lhs % rhs;
		break;

	case OP_LSHIFT:
	case OP_RSHIFT:
	{
		const unsigned long long count =
			analyzer_.is_signed(right.type) && static_cast<long long>(rhs) < 0
				? static_cast<unsigned long long>(analyzer_.width_of(type))
				: rhs;
		if (count >= analyzer_.width_of(type))
		{
			throw NotConstant("a constant expression written as a template "
			                  "argument shifts by more than the width of its "
			                  "type");
		}
		if (op == OP_LSHIFT && sign &&
		    (signed_lhs < 0 ||
		     (count != 0 && (lhs >> (analyzer_.width_of(type) - count)) != 0)))
		{
			// 5.8p2: a signed left operand shall be non-negative and the value
			// shall be representable in the unsigned type of the same width.
			throw NotConstant("a constant expression written as a template "
			                  "argument overflows a shift");
		}
		out.bits = op == OP_LSHIFT
			? lhs << count
			: (sign ? static_cast<unsigned long long>(signed_lhs >> count)
			        : lhs >> count);
		break;
	}

	case OP_AMP: out.bits = lhs & rhs; break;
	case OP_BOR: out.bits = lhs | rhs; break;
	case OP_XOR: out.bits = lhs ^ rhs; break;

	default:
		throw NotConstant("a constant expression written as a template "
		                  "argument holds an operator PA20 does not evaluate");
	}
	return analyzer_.convert(out, type);
}

// 14.3.2p1: the argument a template-argument-list bound to a value place.
//
// The place says what type the value has, so the expression is read and then
// 14.3.2p5's conversion brings it to that type - which is what makes `f<3>` and
// `f<'\3'>` one specialization and `A<1>` and `A<true>` two.  An expression the
// reading cannot settle is one an argument list has yet to say something about,
// and stands for itself: a lone parameter stands for the place it names, so
// that 14.6.1p1's injected-class-name is the current instantiation, and
// anything else stands for the spelling it was written as.
TypeId SemaAnalyzer::template_argument_value(const std::string& spelling,
                                             TypeId place, const Context& ctx)
{
	// The split is a fact of the text and of nothing else, so a template-id
	// written n times is split once however many regions read it back.
	std::unordered_map<std::string, std::vector<std::string> >::iterator held =
		value_words_.find(spelling);
	if (held == value_words_.end())
	{
		std::vector<std::string> split;
		if (!split_value_expression(spelling, split))
		{
			split.clear();
		}
		held = value_words_.insert(std::make_pair(spelling, split)).first;
	}
	const std::vector<std::string>& words = held->second;
	if (words.empty())
	{
		throw NotConstant(spelling + " is not a constant expression this "
		                  "milestone reads as a template argument");
	}
	if (words.size() == 1)
	{
		// 14.6.1p1: a name that stands for a place stands for it in an argument
		// list too, which is what makes the template's own definition name its
		// own specialization.
		SemaEntity* const named = resolve(words[0], ctx, LookupKind::Any);
		if (named != nullptr && !named->constant &&
		    types_.parameter_value_type(named->type) != kNoType)
		{
			return named->type;
		}
	}
	TemplateArgumentReader reader(*this, ctx);
	const Constant value = reader.read(words);
	if (!reader.finished(words))
	{
		throw NotConstant(spelling + " is not a constant expression this "
		                  "milestone reads as a template argument");
	}
	if (reader.dependent() ||
	    (place != kNoType && types_.is_dependent(place)))
	{
		return dependent_value(spelling);
	}
	// 14.3.2p5: the argument is a converted constant expression of the type the
	// place declared, so what tells two arguments apart is the value after that
	// conversion and not the expression that wrote it.
	const TypeId type = place == kNoType ? value.type : place;
	if (arithmetic_type(type) == kNoType)
	{
		throw NotConstant("a template argument is bound to a place whose type "
		                  "is outside the PA20 subset");
	}
	return types_.value_type(type, convert(value, type).bits);
}

// 14.6.2p2: an argument an argument list has yet to settle, which is a
// declaration of nothing and never written out.  One per spelling, so that a
// template naming `A<N + 1>` twice names one specialization of it.
TypeId SemaAnalyzer::dependent_value(const std::string& spelling)
{
	const std::unordered_map<std::string, TypeId>::const_iterator held =
		dependent_values_.find(spelling);
	if (held != dependent_values_.end())
	{
		return held->second;
	}
	const TypeId type = types_.template_parameter_type(model_.type_entity_id(),
	                                                   false, spelling);
	dependent_values_.insert(std::make_pair(spelling, type));
	return type;
}
