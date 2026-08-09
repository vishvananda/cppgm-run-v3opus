#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"
#include "token_model.h"

// 8.1p1's type-id, read out of the spelling PA10 handed on.
//
// 14.2 writes a template-argument-list inside a name, so an argument reaches
// this layer as text rather than as a tree of its own - and this file is the one
// place that turns such a spelling back into what was written.  It reads it the
// way the grammar does: a type-specifier-seq, and then 8.3p1's
// abstract-declarator from the outside in, with a name kept whole however many
// components and argument lists it carries.  Nothing here knows what a template
// is - what it answers is what a type-id says, and the only reason the question
// is asked of a spelling at all is that 14.2 left it as one.

namespace
{

bool is_name_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '$';
}

// The end of a balanced run that `spelling[at]` opens, one past its closer, or
// `npos` where the run does not close.  8.1p1's type-id writes three of them -
// a template-argument-list, a parameter-clause, an array bound - and each may
// hold the others.
std::string::size_type balanced_end(const std::string& spelling,
                                    std::string::size_type at)
{
	const char open = spelling[at];
	const char close = open == '<' ? '>' : (open == '(' ? ')' : ']');
	unsigned depth = 0;
	for (; at < spelling.size(); ++at)
	{
		const char c = spelling[at];
		if (c == open)
		{
			++depth;
			continue;
		}
		if (c != close)
		{
			continue;
		}
		if (--depth == 0)
		{
			return at + 1;
		}
	}
	return std::string::npos;
}

// The terminals a type-id was written from, recovered from the spelling PA10
// handed on.  A template-argument reaches here inside a name rather than as a
// tree of its own, so the one place that turns a spelling back into what was
// written splits this one too.
//
// A name is one word however many components it has: 3.4.3 looks a qualified
// name up in one reading, and a template-argument-list written inside it
// belongs to the component around it rather than to this list - which is what
// makes `V<V<int>*>` one argument and one name.  Everything else a type-id
// writes - the declarator operators, the parentheses of a parameter-clause or
// a grouping, an array bound - is a word of its own, because the declarator is
// what reads them.  False for a spelling that does not close what it opened.
bool split_type_id(const std::string& spelling, std::vector<std::string>& out)
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
		if (is_name_char(c))
		{
			const std::string::size_type start = at;
			while (at < spelling.size())
			{
				if (is_name_char(spelling[at]))
				{
					++at;
					continue;
				}
				// A `<` or a `(` written directly after a name belongs to it:
				// 14.2 writes a template-argument-list there and 7.1.6.2p1 a
				// decltype-specifier's expression.  A `(` after anything else
				// opens a parameter-clause or a grouping.
				if ((spelling[at] == '<' &&
				     spelling.compare(start, at - start, "operator") != 0) ||
				    (spelling[at] == '(' &&
				     spelling.compare(start, at - start, "decltype") == 0))
				{
					const std::string::size_type closed =
						balanced_end(spelling, at);
					if (closed == std::string::npos)
					{
						return false;
					}
					at = closed;
					// What stands after the run belongs to the name only where a
					// `::` joins it: 7.1.6.2p1 writes one after a
					// decltype-specifier and 14.2 after a template-id.  Anything
					// else is a word of its own, because a template-id may carry
					// 7.1.6.1p1's cv-qualifier written after it and PA10 spells
					// the two with no space between - so `A<T>const` is a
					// const-qualified `A<T>` and not a name.
					if (spelling.compare(at, 2, "::") == 0)
					{
						at += 2;
						continue;
					}
					break;
				}
				if (spelling.compare(at, 2, "::") == 0)
				{
					at += 2;
					continue;
				}
				break;
			}
			out.push_back(spelling.substr(start, at - start));
			continue;
		}
		if (spelling.compare(at, 2, "::") == 0)
		{
			// 3.4.3p1: a name written `::x` is one word too, and the empty
			// first component is what says the global namespace.
			const std::string::size_type start = at;
			at += 2;
			while (at < spelling.size() &&
			       (is_name_char(spelling[at]) ||
			        spelling.compare(at, 2, "::") == 0))
			{
				at += is_name_char(spelling[at]) ? 1 : 2;
			}
			out.push_back(spelling.substr(start, at - start));
			continue;
		}
		if (spelling.compare(at, 3, "...") == 0)
		{
			out.push_back("...");
			at += 3;
			continue;
		}
		if (c == '&' && at + 1 < spelling.size() && spelling[at + 1] == '&')
		{
			out.push_back("&&");
			at += 2;
			continue;
		}
		if (c == '*' || c == '&' || c == '(' || c == ')' || c == '[' ||
		    c == ']' || c == ',')
		{
			out.push_back(std::string(1, c));
			++at;
			continue;
		}
		return false;
	}
	return !out.empty();
}

// The terminals of a type-specifier-seq joined back into the one spelling a
// lookup asks about: `long long` keeps the space two keywords need, and a
// qualified name keeps none around its `::`.
void append_specifier(std::string& out, const std::string& word)
{
	if (!out.empty() && is_name_char(out[out.size() - 1]) &&
	    is_name_char(word[0]))
	{
		out += ' ';
	}
	out += word;
}
}

// 7.1.6.2p1 and 14.2: a decltype-specifier standing where a template argument
// writes a type.
//
// The argument list reaches this layer as the spelling inside a name, so the
// expression the specifier holds was never read as an expression - and the one
// operand a spelling can answer for is 5.1.1p8's id-expression, which 3.4 looks
// up here exactly as it looks up any other name.  7.1.6.2p4 is then what the
// type is, and every component written after it is 3.4.3's question about the
// region that type opens.
TypeId SemaAnalyzer::spelled_decltype_type(const std::string& spelling,
                                           const Context& ctx)
{
	const std::string::size_type open = spelling.find('(');
	const std::string::size_type close = balanced_end(spelling, open);
	if (close == std::string::npos)
	{
		throw std::runtime_error(spelling + " is not a type-id this milestone "
		                         "reads");
	}
	std::string operand = spelling.substr(open + 1, close - open - 2);
	// 7.1.6.2p4: a parenthesized id-expression is an lvalue named again, and
	// the type is a reference to what it names.
	bool parenthesized = false;
	while (operand.size() > 1 && operand[0] == '(' &&
	       balanced_end(operand, 0) == operand.size())
	{
		parenthesized = true;
		operand = operand.substr(1, operand.size() - 2);
	}
	const SemaEntity& named =
		require(resolve(operand, ctx, LookupKind::Any), operand);
	TypeId head = named.type;
	switch (named.kind)
	{
	case SemaKind::Variable:
	case SemaKind::Parameter:
	case SemaKind::Function:
		// 3.10p1: an id-expression naming an object or a function is an lvalue.
		head = parenthesized ? types_.reference_to(head, false) : head;
		break;

	case SemaKind::Enumerator:
		break;

	default:
		throw std::runtime_error(operand + " stands inside a decltype-specifier "
		                         "written as a template argument and is not an "
		                         "object, function or enumerator");
	}
	if (close == spelling.size())
	{
		return head;
	}
	return require(qualified_in_type(types_.strip_cv(head),
	                                 QualifiedName(spelling), ctx,
	                                 LookupKind::Type, nullptr),
	               spelling).type;
}

TypeId SemaAnalyzer::template_argument_type(const std::string& spelling,
                                            const Context& ctx)
{
	std::vector<std::string> words;
	if (!split_type_id(spelling, words))
	{
		throw std::runtime_error("a template argument is written outside the "
		                         "PA12 subset");
	}
	std::size_t at = 0;
	const TypeId type = type_id_words(words, at, words.size(), spelling, ctx);
	if (at != words.size())
	{
		throw std::runtime_error(spelling + " is not a type-id this milestone "
		                         "reads");
	}
	return type;
}

// 8.1p1: a type-id is a type-specifier-seq and an abstract-declarator, read
// from the words the spelling was split into.  `at` is left one past the last
// word read, which is what lets a parameter-clause read its list one type-id
// at a time.
TypeId SemaAnalyzer::type_id_words(const std::vector<std::string>& words,
                                   std::size_t& at, std::size_t end,
                                   const std::string& spelling,
                                   const Context& ctx)
{
	// 7.1.6.1p1 lets a cv-qualifier stand on either side of the specifiers it
	// qualifies, so the seq is read until the first thing a declarator writes.
	unsigned cv = kCvNone;
	std::string written;
	for (; at < end; ++at)
	{
		const std::string& word = words[at];
		if (word == "*" || word == "&" || word == "&&" || word == "(" ||
		    word == ")" || word == "[" || word == "," || word == "...")
		{
			break;
		}
		if (word == "const")
		{
			cv |= kCvConst;
			continue;
		}
		if (word == "volatile")
		{
			cv |= kCvVolatile;
			continue;
		}
		append_specifier(written, word);
	}

	// 7.1.6.2 Table 10 names a type by which keywords appear; anything else is
	// a name, and 3.4 says which declaration it reached.
	TypeId type = keyword_type(written);
	if (type == kNoType)
	{
		// 7.1.5p2: `typename` says the name is a type, and is no part of it.
		const std::string name = written.compare(0, 8, "typename") == 0
			? written.substr(written.find_first_not_of(' ', 8))
			: written;
		if (name.compare(0, 9, "decltype(") == 0)
		{
			return abstract_declarator_words(
				types_.qualified(spelled_decltype_type(name, ctx), cv), words, at,
				end, spelling, ctx);
		}
		SemaEntity* const named = resolve(name, ctx, LookupKind::Type);
		if (named == nullptr || !names_a_type(*named))
		{
			throw std::runtime_error(spelling + " does not name a type");
		}
		type = named->type;
	}
	return abstract_declarator_words(types_.qualified(type, cv), words, at, end,
	                                 spelling, ctx);
}

// 8.3p1: the type an abstract-declarator makes of the type its specifiers
// named.  A pointer or a reference written before it is the outermost thing
// the declarator says and is read first, which is what makes an array of
// pointers out of `T *[3]` and a pointer to a function out of `T (*)()`.
TypeId SemaAnalyzer::abstract_declarator_words(
	TypeId base, const std::vector<std::string>& words, std::size_t& at,
	std::size_t end, const std::string& spelling, const Context& ctx)
{
	if (at < end && (words[at] == "*" || words[at] == "&" || words[at] == "&&"))
	{
		const std::string op = words[at];
		++at;
		TypeId inner = op == "*" ? types_.pointer_to(base)
		                         : types_.reference_to(base, op == "&&");
		while (at < end && (words[at] == "const" || words[at] == "volatile"))
		{
			// 8.3.1p1: a cv-qualifier after a `*` qualifies the pointer.
			inner = types_.qualified(
				inner, words[at] == "const" ? kCvConst : kCvVolatile);
			++at;
		}
		return abstract_declarator_words(inner, words, at, end, spelling, ctx);
	}
	// 8.3p1: a parenthesized declarator groups what stands inside it; a `(`
	// that opens a parameter-clause is followed by the first type-id of a list
	// or by the `)` of an empty one.
	std::size_t group_begin = 0;
	std::size_t group_end = 0;
	if (at < end && words[at] == "(" && at + 1 < end &&
	    (words[at + 1] == "*" || words[at + 1] == "&" ||
	     words[at + 1] == "&&" || words[at + 1] == "("))
	{
		group_begin = at + 1;
		unsigned depth = 0;
		for (group_end = at; group_end < end; ++group_end)
		{
			depth += words[group_end] == "(" ? 1u : 0u;
			if (words[group_end] == ")" && --depth == 0)
			{
				break;
			}
		}
		if (group_end == end)
		{
			throw std::runtime_error(spelling + " is not a type-id this "
			                         "milestone reads");
		}
		at = group_end + 1;
	}
	// 8.3.4 and 8.3.5: the suffixes bind left to right, so the type they make
	// is built from the last of them inwards.
	TypeId built = suffix_words(base, words, at, end, spelling, ctx);
	if (group_end == 0)
	{
		return built;
	}
	std::size_t inner = group_begin;
	built = abstract_declarator_words(built, words, inner, group_end, spelling,
	                                  ctx);
	if (inner != group_end)
	{
		throw std::runtime_error(spelling + " is not a type-id this milestone "
		                         "reads");
	}
	return built;
}

// 8.3.4p1's array bound and 8.3.5p1's parameter-clause, folded from the
// innermost outwards: `T[2][3]` is an array of two arrays of three.
TypeId SemaAnalyzer::suffix_words(TypeId base,
                                  const std::vector<std::string>& words,
                                  std::size_t& at, std::size_t end,
                                  const std::string& spelling,
                                  const Context& ctx)
{
	if (at >= end || (words[at] != "[" && words[at] != "("))
	{
		return base;
	}
	const bool array = words[at] == "[";
	++at;
	bool bounded = false;
	unsigned long long bound = 0;
	std::vector<TypeId> parameters;
	bool variadic = false;
	if (array)
	{
		if (at < end && words[at] != "]")
		{
			const std::string& digits = words[at];
			for (std::size_t index = 0; index < digits.size(); ++index)
			{
				if (digits[index] < '0' || digits[index] > '9')
				{
					throw std::runtime_error(spelling + " writes an array bound "
					                         "this milestone does not read");
				}
				bound = bound * 10 + static_cast<unsigned>(digits[index] - '0');
			}
			bounded = true;
			++at;
		}
		if (at >= end || words[at] != "]")
		{
			throw std::runtime_error(spelling + " is not a type-id this "
			                         "milestone reads");
		}
		++at;
	}
	else
	{
		while (at < end && words[at] != ")")
		{
			if (words[at] == ",")
			{
				++at;
				continue;
			}
			if (words[at] == "...")
			{
				variadic = true;
				++at;
				continue;
			}
			parameters.push_back(type_id_words(words, at, end, spelling, ctx));
		}
		if (at >= end)
		{
			throw std::runtime_error(spelling + " is not a type-id this "
			                         "milestone reads");
		}
		++at;
		// 8.3.5p4: one unnamed `void` parameter is an empty parameter list.
		if (parameters.size() == 1 && types_.is_plain_void(parameters[0]))
		{
			parameters.clear();
		}
	}
	const TypeId rest = suffix_words(base, words, at, end, spelling, ctx);
	return array ? types_.array_of(rest, bounded, bound)
	             : types_.function_of(rest, parameters, variadic);
}
