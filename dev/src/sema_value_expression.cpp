#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "ast_model.h"
#include "sema_constexpr.h"
#include "sema_pack.h"
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

// 2.14.3p1 and 2.14.5p1: the encoding-prefixes a literal may be written after,
// which are the only names that close up with a quoted run rather than
// standing for a declaration.
bool is_encoding_prefix(const std::string& word)
{
	return word == "L" || word == "u" || word == "U" || word == "u8" ||
		word == "R" || word == "LR" || word == "uR" || word == "UR" ||
		word == "u8R";
}

// 2.11p1's identifier written on its own, which is the only spelling that can
// name 14.1p4's place: a head declares a place under an identifier and 14.6.1p1
// binds it under that identifier, so a qualified name or a template-id names
// something else and there is nothing for that question to find in one.
bool names_one_identifier(const std::string& word)
{
	if (word.empty() || (word[0] >= '0' && word[0] <= '9'))
	{
		return false;
	}
	for (std::string::size_type at = 0; at < word.size(); ++at)
	{
		if (!is_name_char(word[at]))
		{
			return false;
		}
	}
	return true;
}

// True where the word is one of 2.14's literals rather than a name: a quoted
// run, an encoding-prefix before one, or a run of digits.
bool is_literal_word(const std::string& word)
{
	const std::string::size_type quote = word.find_first_of("'\"");
	if (quote != std::string::npos)
	{
		return quote == 0 || is_encoding_prefix(word.substr(0, quote));
	}
	return !word.empty() && word[0] >= '0' && word[0] <= '9';
}

// The operators 5.19 writes, longest first, so that `<<` is never read as two
// `<` and `>=` never as `>` and `=`.
const char* const kOperators[] = {
	// 14.5.3p4's ellipsis stands before `.`, so that an expansion written
	// inside a spelling is one word this reading has no operand for rather
	// than three member accesses.
	"...",
	"<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "::",
	"+", "-", "*", "/", "%", "<", ">", "&", "|", "^", "!", "~", "?", ":",
	"(", ")", "[", "]", "{", "}", ",", "."
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
	// Which `<` of the spelling opens 14.2's list is a fact of the whole of it,
	// so the reading is made once here and asked at each run the split steps
	// over - the same one scan `sema_type_id.cpp` makes of a type-id.
	const AngleReading angles(spelling);
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
				// 5.3.3p5: `sizeof...` is one operator and not a name
				// followed by an ellipsis, so its `...` closes up with the
				// word the way an argument list closes up with a name.
				if (spelling.compare(start, at - start, "sizeof") == 0 &&
				    spelling.compare(at, 3, "...") == 0)
				{
					at += 3;
					break;
				}
				// 2.14.3 and 2.14.5: an encoding-prefix is part of the literal
				// it was written before rather than a name of its own, so it
				// closes up with the quoted run the way `sizeof` closes up
				// with its ellipsis.
				if (at < spelling.size() &&
				    (spelling[at] == '\'' || spelling[at] == '"') &&
				    is_encoding_prefix(spelling.substr(start, at - start)))
				{
					const std::string::size_type end = quoted_end(spelling, at);
					if (end == std::string::npos)
					{
						return false;
					}
					at = end;
					break;
				}
				// 14.2 writes a template-argument-list after a name and
				// 7.1.6.2p1 a decltype-specifier's expression, each of which
				// belongs to the name it closes up with.  A `<` that opens no
				// balanced run is 5.9's operator instead.
				if (at < spelling.size() &&
				    ((spelling[at] == '<' &&
				      spelling.compare(start, at - start, "operator") != 0) ||
				     (spelling[at] == '(' &&
				      (spelling.compare(start, at - start, "decltype") == 0 ||
				       spelling.compare(start, at - start, "noexcept") == 0))))
				{
					const std::string::size_type closed =
						angles.balanced_end(at);
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
	SemaConstant operand(const std::vector<std::string>& words, bool live);
	// 5.2.5p1: the `.`s written after an operand, each reading a member of what
	// stands to its left - a subobject on its own, and 5.2.2p1's call where an
	// argument list follows.  It is the same rule the tree reading writes, asked
	// of the object and the name because that is what a spelling holds.
	SemaConstant accesses(SemaConstant value,
	                      const std::vector<std::string>& words, bool live);
	SemaConstant type_trait(const std::string& word,
	                        const std::vector<std::string>& words, bool live);
	// 5.3.7: `noexcept(E)`, whose whole spelling is one word because the
	// operand is 13.3's question and not the text's - so the answer is read off
	// the tree the parse kept beside that spelling and not off the words.
	SemaConstant noexcept_operand(const std::string& word, bool live);
	SemaConstant pack_size(const std::vector<std::string>& words);
	SemaConstant literal_operand(const std::string& word,
	                             const std::vector<std::string>& words,
	                             bool live);
	SemaConstant name(const std::string& spelling, bool live);
	// 5.2.2p1, 5.2.3p2 and p3: the operands written between the bracket `at_`
	// stands on and the one that closes it, which a call, an object of class
	// type and a functional-notation cast each write the same way.  False where
	// the run does not close, which is a spelling that is no expression.
	bool operand_list(const std::vector<std::string>& words,
	                  const std::string& close, bool live,
	                  std::vector<SemaConstant>& out);
	// 5.2.3p2 and p3: `T()` and `T{...}` where `T` names a class, which at a
	// value place is what 12.3.2p1's conversion function reads.
	SemaConstant object_operand(TypeId target,
	                            const std::vector<std::string>& words,
	                            bool live);
	// 5.2.2p1 with 7.1.5p2: a call of a constexpr function, which is the arm of
	// 5.2.3's shape whose name reaches no type.
	SemaConstant call_operand(const std::string& word,
	                          const std::vector<std::string>& words, bool live);
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
	return accesses(operand(words, live), words, live);
}

// 5.2.5p1 written after an operand, which binds tighter than every operator
// above and is therefore read here rather than in the precedence walk.
SemaConstant TemplateArgumentReader::accesses(
	SemaConstant value, const std::vector<std::string>& words, bool live)
{
	while (at_ + 1 < words.size() && words[at_] == ".")
	{
		const std::string member = words[at_ + 1];
		if (!names_one_identifier(member))
		{
			throw NotConstant("a member access written as a template argument "
			                  "names no member");
		}
		at_ += 2;
		if (at_ < words.size() && words[at_] == "(")
		{
			std::vector<SemaConstant> operands;
			if (!operand_list(words, ")", live, operands))
			{
				throw NotConstant("a call written as a template argument does "
				                  "not close its arguments");
			}
			if (!live)
			{
				continue;
			}
			const unsigned stood = analyzer_.stood_in_;
			value = ConstexprReading(analyzer_).member_call(value, member,
			                                                operands, ctx_);
			if (analyzer_.stood_in_ != stood)
			{
				// 14.6p8, as a call written on a name says it: the body read
				// something an argument list has yet to settle.
				dependent_ = true;
			}
			continue;
		}
		if (!live)
		{
			continue;
		}
		value = ConstexprReading(analyzer_).member_value(value, member, ctx_);
	}
	return value;
}

SemaConstant TemplateArgumentReader::operand(
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
		// 5.1.1p6: a parenthesized primary is that primary, which is how the
		// tree of the same expression reaches 5.19p2's subscript of a string
		// literal - the walk strips the parentheses before it asks what is
		// being subscripted - so a spelling reaches it the same way.
		std::size_t held_at = opened;
		std::size_t held_end = close;
		while (held_at + 1 < held_end && words[held_at] == "(" &&
		       words[held_end - 1] == ")")
		{
			++held_at;
			--held_end;
		}
		if (held_at + 1 == held_end && is_literal_word(words[held_at]) &&
		    close + 1 < words.size() && words[close + 1] == "[")
		{
			const std::string held = words[held_at];
			at_ = close + 1;
			return literal_operand(held, words, live);
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
	if (is_literal_word(word))
	{
		return literal_operand(word, words, live);
	}
	if (word == "sizeof...")
	{
		return pack_size(words);
	}
	if (word == "sizeof" || word == "alignof")
	{
		return type_trait(word, words, live);
	}
	if (word.compare(0, 9, "noexcept(") == 0)
	{
		return noexcept_operand(word, live);
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
	if (at_ < words.size() && (words[at_] == "(" || words[at_] == "{"))
	{
		// 5.2.3p1: `T(x)` is the same cast written in functional notation, and
		// 5.2.3p2's `T()` is the zero 8.5p7 value-initializes with.  What tells
		// either from a call is whether the word names a type, which is the
		// question 5.4p2 above is settled by too.  5.2.3p3 writes the same two
		// with braces, and what they hold is 8.5.4's one clause per operand.
		const std::string close = words[at_] == "(" ? ")" : "}";
		const TypeId target = probe_type_id(word);
		if (target != kNoType && analyzer_.integral_type(target) != kNoType)
		{
			std::vector<SemaConstant> operands;
			if (!operand_list(words, close, live, operands) ||
			    operands.size() > 1)
			{
				throw NotConstant("a conversion written as a template argument "
				                  "does not close its operand");
			}
			if (operands.empty())
			{
				SemaConstant zero;
				zero.type = target;
				zero.bits = 0;
				return zero;
			}
			return live ? cast(target, operands[0]) : operands[0];
		}
		if (target != kNoType)
		{
			return object_operand(target, words, live);
		}
		if (close == ")")
		{
			return call_operand(word, words, live);
		}
	}
	return name(word, live);
}

bool TemplateArgumentReader::operand_list(const std::vector<std::string>& words,
                                          const std::string& close, bool live,
                                          std::vector<SemaConstant>& out)
{
	++at_;
	if (at_ < words.size() && words[at_] == close)
	{
		++at_;
		return true;
	}
	for (;;)
	{
		out.push_back(expression(words, 0, live));
		if (at_ >= words.size())
		{
			return false;
		}
		if (words[at_] == close)
		{
			++at_;
			return true;
		}
		if (words[at_] != ",")
		{
			return false;
		}
		++at_;
	}
}

SemaConstant TemplateArgumentReader::object_operand(
	TypeId target, const std::vector<std::string>& words, bool live)
{
	const std::string close = words[at_] == "(" ? ")" : "}";
	std::vector<SemaConstant> operands;
	if (!operand_list(words, close, live, operands))
	{
		throw NotConstant("an object written as a template argument does not "
		                  "close its initializer");
	}
	if (analyzer_.types_.is_dependent(target))
	{
		// 14.6.2p2: what class an argument list has yet to settle names, that
		// list is what says - so the argument this stands in is dependent_ and
		// nothing about the object is read here.
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
	return ConstexprReading(analyzer_).object_of(target, operands);
}

SemaConstant TemplateArgumentReader::call_operand(
	const std::string& word, const std::vector<std::string>& words, bool live)
{
	const unsigned before = analyzer_.stood_in_;
	std::vector<SemaConstant> operands;
	if (!operand_list(words, ")", live, operands))
	{
		throw NotConstant("a call written as a template argument does not "
		                  "close its arguments");
	}
	if (!live)
	{
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_INT);
		out.bits = 0;
		return out;
	}
	if (analyzer_.stood_in_ != before)
	{
		// 14.6.2p2 and 13.3: an argument the reading stood a value in for is of
		// the type the stand-in has and not of the one the specialization will
		// pass, so no candidate 13.3 ranks here is the one the call makes -
		// `first_true_loop(values, values + sizeof...(T))` over an array as
		// long as a pack has a `bool const *` argument there and an `int` here.
		// The whole argument is the arguments' to settle, so nothing is looked
		// up and nothing is ranked.
		dependent_ = true;
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_INT);
		out.bits = 1;
		return out;
	}
	// 3.4, 3.4.2 and 14.2: which declarations the word reaches is the reading
	// `called` asks, and a call written as a template argument is the same
	// construct as one written anywhere else - so the ordinary lookup is not
	// the whole of it here either.  A template-id names its specializations and
	// an unqualified name also names what its arguments' namespaces declare.
	const unsigned stood = analyzer_.stood_in_;
	SemaConstant out =
		ConstexprReading(analyzer_).called_name(word, nullptr, operands, ctx_);
	if (analyzer_.stood_in_ != stood)
	{
		// 14.6p8: the body read something an argument list has yet to settle,
		// so what the call came out as says nothing about the argument.
		dependent_ = true;
	}
	return out;
}

// 5.3.7 written as a template argument.
//
// This is the one operator of 5.19 whose answer no reading of the words can
// give: 5.3.7p3 asks which declaration each call in the operand reached, and
// that is 13.3's choice over a typed operand rather than anything the spelling
// holds.  So the operand is not re-read here at all - the parse kept the tree
// it built beside the spelling it flattened to, and the reading that answers
// the operator over a tree is asked exactly as the declaration that wrote it
// would have asked.
SemaConstant TemplateArgumentReader::noexcept_operand(const std::string& word,
                                                      bool live)
{
	const AstNode* const written = analyzer_.written_ == nullptr
		? nullptr
		: analyzer_.written_->spelled(word);
	if (written == nullptr || written->children.empty())
	{
		throw NotConstant(word + " is written as a template argument and this "
		                  "reading has no expression for its operand");
	}
	SemaConstant out;
	out.type = analyzer_.types_.fundamental(FT_BOOL);
	if (!live)
	{
		// 5.14p1 and 5.16p1: the arm the answer does not depend on is read for
		// its shape, and reading this operand is a whole analysis of it.
		return out;
	}
	out.bits =
		ConstexprReading(analyzer_).nonthrowing_operand(*written->children[0],
		                                                ctx_)
			? 1 : 0;
	return out;
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

// 2.14 and 5.19p2: what a literal written where an operand stands comes to -
// its own value, and for a string literal the element a subscript names, which
// is the one object a constant expression reads out of storage and the one
// postfix operator an argument spelling writes.  5.1.1p6's parentheses around
// the literal change neither, so both spellings of the operand read it here.
SemaConstant TemplateArgumentReader::literal_operand(
	const std::string& word, const std::vector<std::string>& words, bool live)
{
	if (at_ >= words.size() || words[at_] != "[")
	{
		return analyzer_.literal_constant(word);
	}
	++at_;
	const SemaConstant index = expression(words, 0, live);
	if (at_ >= words.size() || words[at_] != "]")
	{
		throw NotConstant("a subscript written as a template argument does not "
		                  "close its index");
	}
	++at_;
	return analyzer_.string_element(word, index.bits);
}

// 5.3.3p5: how many elements the run bound to the pack holds, which is the
// same reading a tree of the operator is given - a pattern standing over a
// pack leaves the name standing for one element, and the pack itself is what
// the clause counts.
SemaConstant TemplateArgumentReader::pack_size(
	const std::vector<std::string>& words)
{
	if (at_ + 2 >= words.size() || words[at_] != "(" || words[at_ + 2] != ")")
	{
		throw NotConstant("sizeof... written as a template argument has no "
		                  "parenthesized parameter pack");
	}
	const std::string name = words[at_ + 1];
	at_ += 3;
	const long long run = PackReading(analyzer_).length(name, ctx_);
	SemaConstant out;
	out.type = analyzer_.types_.fundamental(FT_UNSIGNED_LONG_INT);
	if (run < 0)
	{
		// 14.6p8: how long a run the head being read declared comes to, an
		// argument list is what says - so the argument this stands in is
		// dependent_, exactly as the size of a dependent type is.
		dependent_ = true;
		++analyzer_.stood_in_;
		out.bits = 1;
		return out;
	}
	out.bits = static_cast<unsigned long long>(run);
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
	const unsigned stood = analyzer_.stood_in_;
	try
	{
		const ReadingDepth probing(analyzer_.checking_);
		return analyzer_.template_argument_type(spelling, ctx_);
	}
	catch (const std::exception&)
	{
		// 14.6p8's count is of the stand-ins a *reading* made, and this one was
		// thrown away - so a `sizeof` of a dependent type met inside a spelling
		// that turned out to be no type-id at all stood nothing in, and 7p4's
		// static_assert is still the one this reading answers.
		analyzer_.stood_in_ = stood;
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
	if (named->constant || named->address != 0)
	{
		// 5.19p2 asked of a declaration a lookup reached, which is one reading
		// and not one per door: what the name is worth, the object it
		// designates, and 8.3.2p1's reference standing for another object are
		// the same three facts here as they are over a tree.
		return ConstexprReading(analyzer_).entity_constant(*named, spelling);
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
	// 3.10p1: a name a lookup reached whose object holds no value may still
	// designate one - `static bool a[4];` is such an object, and 4.2p1's decay
	// and 5.3.1p3's `&` read it.  The one reading answers that here too, and
	// refuses where there is neither.
	return ConstexprReading(analyzer_).entity_constant(*named, spelling);
}

// 5.4p4 and 4.7: an integral value read as another integral type.
SemaConstant TemplateArgumentReader::cast(TypeId target,
                                          const SemaConstant& operand)
{
	if (analyzer_.integral_type(target) == kNoType)
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
	{
		// 5.7 and 5.9-5.10 over an address: the same reading a tree asks, and
		// asked before the promotion below, which would read the identifier of
		// the object a pointer designates as a number.
		SemaConstant address;
		if (ConstexprReading(analyzer_).address_operation(op, left_value,
		                                                  right_value, address))
		{
			return address;
		}
	}
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
	if (words.size() == 1 && names_one_identifier(words[0]))
	{
		// 14.6.1p1: a name that stands for a place stands for it in an argument
		// list too, which is what makes the template's own definition name its
		// own specialization.  Only an identifier can be that name, and asking
		// the lookup of anything else is the whole reading of that name made a
		// second time - which over `W<W<W<3>::v>::v>::v` is a reading per level
		// doubled at every level below it.
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
	// conversion and not the expression that wrote it.  4p3 makes a class
	// prvalue reach an arithmetic place only through 12.3.2p1's conversion
	// function, which is the one call this conversion may hold.
	SemaConstant given = value;
	if (ConstexprReading(*this).is_object(given))
	{
		// 14.3.2p5's converted constant expression is 5.19p3's *implicit*
		// sequence, so 12.3.2p2 leaves a conversion function declared
		// `explicit` out of it.
		given = ConstexprReading(*this).converted(
			given, place == kNoType ? kNoType : types_.strip_cv(place), false);
	}
	const TypeId type = place == kNoType ? given.type : place;
	if (integral_type(type) == kNoType)
	{
		throw NotConstant("a template argument is bound to a place whose type "
		                  "is outside the PA20 subset");
	}
	return types_.value_type(type, convert(given, type).bits);
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
