#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"
#include "sema_pack.h"
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

// 7.1.6.3p1: the class-key or `enum` an elaborated-type-specifier is written
// with, and the name after it.  False for every other type-specifier-seq,
// which is nearly all of them - a spelling that holds no space at all cannot
// be one, and that is the common case.
bool split_elaborated(const std::string& written, std::string& key,
                      std::string& name)
{
	const std::string::size_type space = written.find(' ');
	if (space == std::string::npos)
	{
		return false;
	}
	key = written.substr(0, space);
	if (key != "class" && key != "struct" && key != "union" && key != "enum")
	{
		return false;
	}
	name = written.substr(written.find_first_not_of(' ', space));
	return true;
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
	// 8.1p1's type-id writes three balanced runs - a template-argument-list, a
	// parameter-clause, an array bound - and each may hold the others.  Which
	// `<` opens one is a fact of the whole spelling, so the reading is made
	// once here and asked at each run rather than remade at every `<`.
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
		// 2.9p1: an array bound writes a number, which holds characters no name
		// holds - and the words this reading hands the value reading on are
		// rejoined with spaces, so a number split here is one that reading can
		// no longer read.
		const std::string::size_type number = pp_number_end(spelling, at);
		if (number != at)
		{
			out.push_back(spelling.substr(at, number - at));
			at = number;
			continue;
		}
		// 3.4.3p1: a name written `::x` is one word too, and the empty first
		// component is what says the global namespace - so it is read by the
		// same scan and takes 14.2's argument list with it.
		const bool rooted = !is_name_char(c) &&
			spelling.compare(at, 2, "::") == 0;
		if (is_name_char(c) || rooted)
		{
			const std::string::size_type start = at;
			at += rooted ? 2 : 0;
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
						angles.balanced_end(at);
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
		if (c == '[')
		{
			// 8.3.4p1's bound is a constant-expression, which 5.19 owns and this
			// reading does not: it writes operators no type-id writes, and the
			// words handed on are rejoined with spaces, so one split here is one
			// the value reading can no longer put back together.  The whole of
			// what the brackets hold is therefore one word - `int[1 + 1]` is
			// `int` `[` `1 + 1` `]` - which is what a bound of one terminal
			// already came to.
			const std::string::size_type closed = angles.balanced_end(at);
			if (closed == std::string::npos || closed <= at + 1)
			{
				return false;
			}
			out.push_back("[");
			if (closed > at + 2)
			{
				out.push_back(spelling.substr(at + 1, closed - at - 2));
			}
			out.push_back("]");
			at = closed;
			continue;
		}
		if (c == '*' || c == '&' || c == '(' || c == ')' ||
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

// 8.3.3p1's `nested-name-specifier *`, recognised in the words the spelling was
// split into.  The split leaves a nested-name-specifier standing on its own
// only where the character after its `::` opens no name, so a word that ends in
// one and is followed by `*` is the ptr-operator and nothing else - which is
// what keeps `int C::*` out of the type-specifier-seq that would otherwise read
// `int C::` as a name.
bool names_member_pointer(const std::vector<std::string>& words,
                          std::size_t at, std::size_t end)
{
	return at + 1 < end && words[at].size() > 2 &&
		words[at].compare(words[at].size() - 2, 2, "::") == 0 &&
		words[at + 1] == "*";
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

// 8.1p1's type-id and 7.1.6.2p1's decltype-specifier, read out of the words one
// spelling was split into.
//
// It is a reading of its own rather than a walk of the analyzer's, for the same
// reason `TemplateArgumentReader` is: what it reads is text, and where in those
// words it stands is state no walk of a tree has.  The lookups, the type table
// and the expression layer are the analyzer's, so the reader borrows it.
class SpelledTypeId
{
public:
	SpelledTypeId(SemaAnalyzer& analyzer, const SemaContext& ctx)
		: analyzer_(analyzer)
		, ctx_(ctx)
	{}

	// 8.1p1: a type-id is a type-specifier-seq and an abstract-declarator, read
	// from the words the spelling was split into.  `at` is left one past the
	// last word read, which is what lets a parameter-clause read its list one
	// type-id at a time.
	TypeId read(const std::vector<std::string>& words, std::size_t& at,
	            std::size_t end, const std::string& spelling);

private:
	TypeId decltype_specifier(const std::string& spelling);
	TypeId elaborated(const std::string& key, const std::string& name);
	// 8.3.3p1: the class a `X::*` names the members of.
	TypeId member_owner(const std::string& written, const std::string& spelling);
	// 8.3p1's abstract-declarator, and 8.3.4p1's bound and 8.3.5p1's
	// parameter-clause written after it.
	TypeId declarator(TypeId base, const std::vector<std::string>& words,
	                  std::size_t& at, std::size_t end,
	                  const std::string& spelling);
	TypeId suffix(TypeId base, const std::vector<std::string>& words,
	              std::size_t& at, std::size_t end,
	              const std::string& spelling);
	// 8.3.4p1's bound, written as `words[from, to)`: how many elements the
	// array has, and - through `place` - the template place its
	// constant-expression named where an argument list has yet to settle one.
	unsigned long long written_bound(const std::vector<std::string>& words,
	                                 std::size_t from, std::size_t to,
	                                 const std::string& spelling,
	                                 TypeId& place);

	SpelledTypeId(const SpelledTypeId&);
	SpelledTypeId& operator=(const SpelledTypeId&);

	SemaAnalyzer& analyzer_;
	const SemaContext& ctx_;
};

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
	const TypeId type =
		SpelledTypeId(*this, ctx).read(words, at, words.size(), spelling);
	if (at != words.size())
	{
		throw std::runtime_error(spelling + " is not a type-id this milestone "
		                         "reads");
	}
	return type;
}

// 7.1.6.2p1 and 14.2: a decltype-specifier standing where a template argument
// writes a type.
//
// The argument list reaches this layer as the spelling inside a name, so the
// specifier arrives as text - but 5.1.1p8's id-expression is only one of the
// expressions 7.1.6.2p1 admits, and a call, a delete-expression or an
// explicit type conversion says nothing a lookup of its spelling could answer.
// So the tree the parse read for that operand is asked back (`AstArena`) and
// the specifier is answered by the one reading that answers every other
// decltype-specifier.  Every component written after it is then 3.4.3's
// question about the region that type opens.
TypeId SpelledTypeId::decltype_specifier(const std::string& spelling)
{
	const std::string::size_type open = spelling.find('(');
	const std::string::size_type close = spelling_balanced_end(spelling, open);
	if (close == std::string::npos)
	{
		throw std::runtime_error(spelling + " is not a type-id this milestone "
		                         "reads");
	}
	const AstNode* const written = analyzer_.written_ == nullptr
		? nullptr
		: analyzer_.written_->spelled(spelling.substr(0, close));
	if (written == nullptr)
	{
		throw std::runtime_error(spelling + " holds a decltype-specifier this "
		                         "reading has no expression for");
	}
	const TypeId head = analyzer_.decltype_type(*written, ctx_);
	if (close == spelling.size())
	{
		return head;
	}
	return analyzer_.require(
		analyzer_.qualified_in_type(analyzer_.types_.strip_cv(head),
		                            QualifiedName(spelling), ctx_,
		                            LookupKind::Type, nullptr),
		spelling).type;
}

// 7.1.6.3p1 and 3.4.4p2: the class or enumeration an elaborated-type-specifier
// written in a type-id names.  The lookup ignores every declaration that is not
// a type, which is what lets `struct S` reach the class an object of that
// spelling hides; and 3.3.2p6 makes a class-key that reaches no class at all a
// *declaration* of one, in the smallest namespace or block scope around the
// declaration the specifier belongs to - not in the class or the
// parameter-clause it happens to be written inside, and not in the region
// 14.1p1 gave the head, which is gone with the template-declaration.
// 7.2p3 leaves `enum` no such arm: an elaborated enum-specifier names an
// enumeration that has already been declared.
TypeId SpelledTypeId::elaborated(const std::string& key,
                                 const std::string& name)
{
	SemaEntity* const found = analyzer_.resolve(name, ctx_, LookupKind::Type);
	const bool enumeration = key == "enum";
	if (found != nullptr &&
	    found->kind == (enumeration ? SemaKind::Enum : SemaKind::Class))
	{
		// 7.1.6.3p3: the class-key shall agree with the declaration it reached.
		if (!enumeration &&
		    (analyzer_.types_.class_tag(found->type) == ClassTag::Union) !=
			    (key == "union"))
		{
			throw std::runtime_error(key + " " + name + " names a class whose "
			                         "declaration wrote another class-key");
		}
		return found->type;
	}
	if (enumeration || QualifiedName(name).qualified())
	{
		// 3.4.4p3: an elaborated-type-specifier with a nested-name-specifier
		// declares nothing - the name it writes shall reach a class.
		throw std::runtime_error(key + " " + name + " does not name a " +
		                         (enumeration ? "enumeration" : "class"));
	}
	SemaContext where = ctx_;
	while (where.scope->parent != nullptr &&
	       (where.scope->kind == ScopeKind::TemplateParameters ||
	        where.scope->kind == ScopeKind::Prototype ||
	        where.scope->kind == ScopeKind::Class))
	{
		where.scope = where.scope->parent;
	}
	where.dump = where.scope->dump;
	const ClassTag tag = key == "class"
		? ClassTag::Class
		: (key == "union" ? ClassTag::Union : ClassTag::Struct);
	return analyzer_.class_head_entity(where, tag, QualifiedName(name), name,
	                                   false, where.scope)
		->type;
}

// 8.3.3p1 and 3.4.3p1: the class a ptr-operator's nested-name-specifier names,
// looked up the way the type-specifier-seq looks its own name up - so a place a
// head declared answers here as much as a class the program wrote, which is
// what `template<class K, class R> struct probe<R K::*>` needs.
TypeId SpelledTypeId::member_owner(const std::string& written,
                                   const std::string& spelling)
{
	SemaEntity* const named = analyzer_.resolve(written, ctx_, LookupKind::Any);
	if (named == nullptr || !names_a_type(*named))
	{
		throw std::runtime_error(spelling + " writes a pointer to a member of " +
		                         written + ", which names no class");
	}
	return named->type;
}

// 8.1p1: a type-id is a type-specifier-seq and an abstract-declarator, read
// from the words the spelling was split into.  `at` is left one past the last
// word read, which is what lets a parameter-clause read its list one type-id
// at a time.
TypeId SpelledTypeId::read(const std::vector<std::string>& words,
                           std::size_t& at, std::size_t end,
                           const std::string& spelling)
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
		if (names_member_pointer(words, at, end))
		{
			// 8.3.3p1: the nested-name-specifier belongs to the declarator's
			// ptr-operator and is no specifier of this seq.
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
	TypeId type = analyzer_.keyword_type(written);
	if (type == kNoType)
	{
		// 7.1.5p2: `typename` says the name is a type, and is no part of it.
		const std::string name = written.compare(0, 8, "typename") == 0
			? written.substr(written.find_first_not_of(' ', 8))
			: written;
		if (name.compare(0, 9, "decltype(") == 0)
		{
			return declarator(
				analyzer_.types_.qualified(decltype_specifier(name), cv), words, at,
				end, spelling);
		}
		// 7.1.6.3p1: a class-key or `enum` before the name makes an
		// elaborated-type-specifier, which 3.4.4p2 looks up as a type alone -
		// and 3.3.2p6 makes one that reaches no class a declaration of it.
		std::string key;
		std::string keyed;
		if (split_elaborated(name, key, keyed))
		{
			return declarator(
				analyzer_.types_.qualified(elaborated(key, keyed), cv),
				words, at, end, spelling);
		}
		// 3.3.10p2: a class or enumeration name is hidden wherever a variable,
		// a data member, a function or an enumerator of that name declared in
		// the same region is visible, so a name written with no class-key is
		// 3.4.1's ordinary lookup and not 3.4.4p2's - which ignores every
		// declaration that is not a type and is the reading `elaborated` above
		// is given, and is the one reading this door had for both.
		SemaEntity* const named = analyzer_.resolve(name, ctx_, LookupKind::Any);
		if (named == nullptr || !names_a_type(*named))
		{
			throw std::runtime_error(spelling + " does not name a type");
		}
		type = named->type;
	}
	return declarator(analyzer_.types_.qualified(type, cv), words, at, end,
	                  spelling);
}

// 8.3p1: the type an abstract-declarator makes of the type its specifiers
// named.  A pointer or a reference written before it is the outermost thing
// the declarator says and is read first, which is what makes an array of
// pointers out of `T *[3]` and a pointer to a function out of `T (*)()`.
TypeId SpelledTypeId::declarator(TypeId base,
                                 const std::vector<std::string>& words,
                                 std::size_t& at, std::size_t end,
                                 const std::string& spelling)
{
	if (at < end &&
	    (words[at] == "*" || words[at] == "&" || words[at] == "&&" ||
	     names_member_pointer(words, at, end)))
	{
		// 8.3.3p1: `X::*` makes a pointer to a member of `X`, which is the same
		// ptr-operator as `*` with the class the members belong to written before
		// it - so it takes the cv-qualifiers written after it the same way.
		const bool member = names_member_pointer(words, at, end);
		const std::string op = member ? "*" : words[at];
		TypeId owner = kNoType;
		if (member)
		{
			owner = member_owner(words[at].substr(0, words[at].size() - 2),
			                     spelling);
			++at;
		}
		++at;
		TypeId inner = member
			? analyzer_.types_.member_pointer_to(owner, base)
			: (op == "*" ? analyzer_.types_.pointer_to(base)
			             : analyzer_.types_.reference_to(base, op == "&&"));
		while (at < end && (words[at] == "const" || words[at] == "volatile"))
		{
			// 8.3.1p1: a cv-qualifier after a `*` qualifies the pointer.
			inner = analyzer_.types_.qualified(
				inner, words[at] == "const" ? kCvConst : kCvVolatile);
			++at;
		}
		return declarator(inner, words, at, end, spelling);
	}
	// 8.3p1: a parenthesized declarator groups what stands inside it; a `(`
	// that opens a parameter-clause is followed by the first type-id of a list
	// or by the `)` of an empty one.
	std::size_t group_begin = 0;
	std::size_t group_end = 0;
	if (at < end && words[at] == "(" && at + 1 < end &&
	    (words[at + 1] == "*" || words[at + 1] == "&" ||
	     words[at + 1] == "&&" || words[at + 1] == "(" ||
	     names_member_pointer(words, at + 1, end)))
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
	TypeId built = suffix(base, words, at, end, spelling);
	if (group_end == 0)
	{
		return built;
	}
	std::size_t inner = group_begin;
	built = declarator(built, words, inner, group_end, spelling);
	if (inner != group_end)
	{
		throw std::runtime_error(spelling + " is not a type-id this milestone "
		                         "reads");
	}
	return built;
}

// 8.3.4p1's bound, read out of the words the brackets hold.
//
// 14.2 wrote this type-id inside a name, so the bound arrives as text like
// everything else here - and what a bound *is* is 5.19's constant expression,
// which the value-argument reading already answers off a spelling.  So the
// digits a bound is nearly always written as are read here, and anything else
// is that one reading: `A<T[MAX]>` comes to the number `MAX` holds, and
// `A<T[N]>` over a place `N` comes to the place itself, which is the fact
// 14.8.2.5p13 deduces from and 14.3p1's substitution replaces.
//
// A bound naming anything else an argument list has yet to settle - `T[N + 1]`,
// `T[sizeof(U)]` - is refused rather than stood in for: it names no place a
// substitution could put a number back into, and standing one element in would
// leave a pattern silently read as `T[1]`.
unsigned long long SpelledTypeId::written_bound(
	const std::vector<std::string>& words, std::size_t from, std::size_t to,
	const std::string& spelling, TypeId& place)
{
	if (to == from + 1 &&
	    words[from].find_first_not_of("0123456789") == std::string::npos)
	{
		unsigned long long bound = 0;
		for (std::size_t index = 0; index < words[from].size(); ++index)
		{
			bound = bound * 10 +
				static_cast<unsigned>(words[from][index] - '0');
		}
		return bound;
	}
	std::string written = words[from];
	for (std::size_t index = from + 1; index < to; ++index)
	{
		// The split this reading made is 8.1p1's and the value reading makes
		// its own, so what is handed on is the spelling and not the words - a
		// space between two of them separates whatever they were separated by.
		written += " " + words[index];
	}
	TypeId read = kNoType;
	try
	{
		// 8.3.4p1: the bound is a converted constant expression of type
		// `std::size_t`, which is the type a settled one is interned as.
		read = analyzer_.template_argument_value(
			written, analyzer_.types_.fundamental(FT_UNSIGNED_LONG_INT), ctx_);
	}
	catch (const std::exception&)
	{
		throw std::runtime_error(spelling + " writes an array bound this "
		                         "milestone does not read");
	}
	if (analyzer_.types_.is_value(read))
	{
		return analyzer_.types_.value_bits(read);
	}
	if (analyzer_.types_.parameter_value_type(read) == kNoType)
	{
		throw std::runtime_error(spelling + " writes an array bound this "
		                         "milestone does not read");
	}
	// 14.6.2p1: the place stands for itself until an argument list says what it
	// is worth, and one element stands in for the number until then - which is
	// the same stand-in every other unsettled bound already has.
	place = read;
	return 1;
}

// 8.3.4p1's array bound and 8.3.5p1's parameter-clause, folded from the
// innermost outwards: `T[2][3]` is an array of two arrays of three.
TypeId SpelledTypeId::suffix(TypeId base,
                             const std::vector<std::string>& words,
                             std::size_t& at, std::size_t end,
                             const std::string& spelling)
{
	if (at >= end || (words[at] != "[" && words[at] != "("))
	{
		return base;
	}
	const bool array = words[at] == "[";
	++at;
	bool bounded = false;
	unsigned long long bound = 0;
	TypeId place = kNoType;
	std::vector<TypeId> parameters;
	bool variadic = false;
	if (array)
	{
		const std::size_t opened = at;
		for (unsigned depth = 0; at < end; ++at)
		{
			if (words[at] == "[")
			{
				++depth;
				continue;
			}
			if (words[at] != "]")
			{
				continue;
			}
			if (depth == 0)
			{
				break;
			}
			--depth;
		}
		if (at >= end)
		{
			throw std::runtime_error(spelling + " is not a type-id this "
			                         "milestone reads");
		}
		if (at != opened)
		{
			bounded = true;
			bound = written_bound(words, opened, at, spelling, place);
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
				// 8.3.5p3: a `...` that follows no parameter-declaration of its
				// own is the ellipsis, which says the list goes on and names no
				// type.
				variadic = true;
				++at;
				continue;
			}
			const TypeId one = read(words, at, end, spelling);
			if (at >= end || words[at] != "...")
			{
				parameters.push_back(one);
				continue;
			}
			// 14.5.3p4 and 8.3.5p1: a `...` written directly after a
			// parameter-declaration whose type holds an unexpanded pack expands
			// *that* parameter rather than opening 8.3.5p3's ellipsis - so
			// `R(Args...)` is a list of however many entries the run holds and
			// `R(int...)` is one entry and an ellipsis.  This is the same reading
			// `read_parameters` gives the tree PA10 handed on; a type-id reaches
			// this layer as the text 14.2 wrote it inside a name, which is why the
			// rule is answered twice.
			++at;
			std::vector<TypeId> run;
			if (!PackReading(analyzer_).expand_type(one, run))
			{
				variadic = true;
				parameters.push_back(one);
				continue;
			}
			parameters.insert(parameters.end(), run.begin(), run.end());
		}
		if (at >= end)
		{
			throw std::runtime_error(spelling + " is not a type-id this "
			                         "milestone reads");
		}
		++at;
		// 8.3.5p4: one unnamed `void` parameter is an empty parameter list.
		if (parameters.size() == 1 && analyzer_.types_.is_plain_void(parameters[0]))
		{
			parameters.clear();
		}
		// 8.3.5p5: a parameter of array or function type contributes a pointer,
		// and its top-level cv-qualifiers are dropped - so `Fun(A0(B0))` is a
		// function of a *pointer to* `A0(B0)`, which is what a pattern written
		// `call<Fun(A0)>` deduces its `A0` to.  This is the adjustment
		// `parameter_types` makes of the clause PA10 handed on as a tree, made
		// here for the one PA10 handed on as text.
		for (std::size_t index = 0; analyzer_.semantics() &&
		                            index < parameters.size(); ++index)
		{
			parameters[index] =
				analyzer_.types_.adjust_parameter(parameters[index]);
		}
	}
	// 8.3.5p1 and 8.3.5p7: the cv-qualifier-seq and the ref-qualifier written
	// after a parameter-clause are part of the function type the declarator made,
	// which is what 14.5.5's `holder<R(A...) const &>` writes a pattern over.
	// They are read before the suffixes after them, because the type is folded
	// from the innermost outwards.
	unsigned function_cv = kCvNone;
	RefQualifier function_ref = RefQualifier::None;
	if (!array)
	{
		while (at < end && (words[at] == "const" || words[at] == "volatile"))
		{
			function_cv |= words[at] == "const" ? kCvConst : kCvVolatile;
			++at;
		}
		if (at < end && (words[at] == "&" || words[at] == "&&"))
		{
			function_ref = words[at] == "&" ? RefQualifier::LValue
			                                : RefQualifier::RValue;
			++at;
		}
	}
	const TypeId rest = suffix(base, words, at, end, spelling);
	if (array)
	{
		return analyzer_.types_.array_of(rest, bounded, bound, place);
	}
	return analyzer_.types_.ref_qualified_function(
		analyzer_.types_.qualified_function(
			analyzer_.types_.function_of(rest, parameters, variadic), function_cv),
		function_ref);
}
