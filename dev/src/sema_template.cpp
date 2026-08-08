#include "sema_analyzer.h"

#include <stdexcept>
#include <utility>

#include "ast_model.h"
#include "ast_tokens.h"
#include "token_model.h"

// Function templates, as far as one specialization of each.
//
// 14p1 makes a template a pattern rather than a declaration: the unit declares
// the functions its instantiations declare, and the output describes those.  So
// the layer has three steps, and each has one owner.  The template-argument
// list is read here, because 14.2 writes it inside a name and the name layer is
// what turns a spelling back into what was written.  Substitution is the type
// table's, because 14.3 binds an argument to a parameter and rebuilding a type
// with one replaced is a fact about types alone.  The specialization is the
// model's, because 14.7.1 makes it one declaration however many times it is
// named, which is the same interning a redeclaration asks for.
//
// A specialization is bound to no name.  It is reached from the template-id
// that wrote its arguments or from the call that deduced them, which is why
// ordinary lookup keeps finding the template and never a declaration the
// program did not write.
//
// PA19 adds the class tier on the same three steps.  14p1 makes a class
// template a pattern too, so the walk records what the template-declaration
// parameterises instead of reading it, and 14.7.1p1's instantiation is that
// same pattern read once more against a region that binds each parameter to
// its argument.  Nothing is substituted into syntax and no text is replayed:
// the bindings are typedef-names of the argument types, so every name the body
// writes is looked up with the arguments already in hand and the ordinary
// PA16-PA18 class machinery settles the specialization exactly as it settles a
// class the program wrote out.

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
					continue;
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
		const QualifiedName spelled(written);
		SemaEntity* const named =
			written.compare(0, 8, "typename") == 0
				? resolve(written.substr(written.find_first_not_of(' ', 8)), ctx,
				          LookupKind::Type)
				: resolve(written, ctx, LookupKind::Type);
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

SemaEntity& SemaAnalyzer::specialize(SemaEntity& primary,
                                     const std::vector<TypeId>& arguments)
{
	// 14.7.1p1: one specialization per template and argument list, however many
	// times it is named, so a second naming is a probe rather than a second
	// substitution.
	const std::uint32_t list = types_.type_list(arguments);
	SemaEntity* made = model_.specialization_of(primary, list);
	if (made == nullptr)
	{
		// 14.3p1: the arguments are bound to the parameters in order, and
		// 14.8.2 builds the declaration by substituting them into the type the
		// template-declaration made.
		std::unordered_map<TypeId, TypeId> bindings;
		const std::vector<SemaEntity*>& parameters =
			primary.template_parameters->declarations;
		for (std::size_t index = 0; index < arguments.size(); ++index)
		{
			bindings.insert(std::make_pair(parameters[index]->type,
			                               arguments[index]));
		}
		std::unordered_map<TypeId, TypeId> memo;
		made = &model_.create(SemaKind::Function, primary.name,
		                      substituted(primary.type, bindings, memo));
		made->primary = &primary;
		made->region = primary.region;
		made->object_member = primary.object_member;
		// 14.7.1p1: the specialization is a declaration of the template's own
		// name, which is what the output calls it wherever it names a
		// declaration rather than repeating what a use wrote.
		made->dump_name = primary.dump_name;
		made->abi_name = primary.abi_name;
		// 14.2: the arguments that made it, which the object file writes after
		// the template's name and which no spelling of the declaration holds.
		made->template_arguments = list;
		model_.hold_specialization(primary, list, *made);
	}
	return *made;
}

SemaEntity* SemaAnalyzer::template_specializations(const std::string& spelling,
                                                   const Context& ctx,
                                                   std::vector<SemaEntity*>& found)
{
	const QualifiedName written(spelling);
	const std::string component = written.last();
	const TemplateId id(component);
	if (!id.valid())
	{
		return nullptr;
	}
	// The template-id is the last component of the name, so what the
	// nested-name-specifier before it reaches is looked up as it is for any
	// other name.
	const std::string named =
		spelling.substr(0, spelling.size() - component.size()) + id.name();
	SemaEntity* const primary = resolve(named, ctx, LookupKind::Any);
	if (primary == nullptr || primary->kind != SemaKind::Function)
	{
		return nullptr;
	}

	std::vector<TypeId> arguments;
	arguments.reserve(id.arguments().size());
	for (std::size_t index = 0; index < id.arguments().size(); ++index)
	{
		arguments.push_back(template_argument_type(id.arguments()[index], ctx));
	}

	// 14.8.1p2 and 13.4p1: the argument list is written once and makes one
	// specialization of each declaration of the name it fits, which is still an
	// overload set for a target type or a call to choose from.  Each of them is a
	// declaration of its own that no region's chain holds, so the set the use
	// carries is what holds them.
	for (SemaEntity* at = primary; at != nullptr; at = at->next)
	{
		if (at->template_parameters == nullptr ||
		    at->template_parameters->declarations.size() != arguments.size())
		{
			continue;
		}
		found.push_back(&specialize(*at, arguments));
	}
	if (found.empty())
	{
		throw std::runtime_error(spelling + " names no template its argument "
		                         "list fits");
	}
	return found[0];
}

bool SemaAnalyzer::deduce(TypeId pattern, TypeId argument,
                          std::unordered_map<TypeId, TypeId>& bindings,
                          bool relaxed)
{
	if (types_.kind(pattern) == TypeKind::TemplateParameter)
	{
		// 14.8.2.5p4: the qualifiers the parameter writes are matched by the
		// argument's own, and what is left of the argument is what the
		// parameter names.  14.8.2.1p4 lets the top of a reference parameter
		// write qualifiers the argument does not have, because binding the
		// reference adds them.
		if (!relaxed && (types_.cv(pattern) & ~types_.cv(argument)) != 0)
		{
			return false;
		}
		const TypeId deduced =
			types_.qualified(types_.strip_cv(argument),
			                 types_.cv(argument) & ~types_.cv(pattern));
		const std::pair<std::unordered_map<TypeId, TypeId>::iterator, bool> held =
			bindings.insert(std::make_pair(types_.strip_cv(pattern), deduced));
		// 14.8.2.5p2: two arguments that deduce one parameter differently
		// deduce nothing.
		return held.second || held.first->second == deduced;
	}
	if (types_.kind(pattern) != types_.kind(argument) ||
	    (relaxed ? (types_.cv(argument) & ~types_.cv(pattern)) != 0
	             : types_.cv(pattern) != types_.cv(argument)))
	{
		return false;
	}
	switch (types_.kind(pattern))
	{
	case TypeKind::Pointer:
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		return deduce(types_.target(pattern), types_.target(argument), bindings);

	case TypeKind::Array:
		return types_.bounded(pattern) == types_.bounded(argument) &&
			types_.bound(pattern) == types_.bound(argument) &&
			deduce(types_.target(pattern), types_.target(argument), bindings);

	case TypeKind::MemberPointer:
		return deduce(types_.member_class(pattern), types_.member_class(argument),
		              bindings) &&
			deduce(types_.target(pattern), types_.target(argument), bindings);

	case TypeKind::Function:
	{
		const std::vector<TypeId>& expected = types_.parameters(pattern);
		const std::vector<TypeId>& given = types_.parameters(argument);
		if (expected.size() != given.size() ||
		    types_.variadic(pattern) != types_.variadic(argument) ||
		    !deduce(types_.target(pattern), types_.target(argument), bindings))
		{
			return false;
		}
		for (std::size_t index = 0; index < expected.size(); ++index)
		{
			if (!deduce(expected[index], given[index], bindings))
			{
				return false;
			}
		}
		return true;
	}

	case TypeKind::Class:
		// 14.8.2.5p4: `A<T>` against `A<int>` deduces `T` from `int`, which is
		// the one class-typed pattern that holds a parameter at all.  The two
		// facts a specialization records - the template it was made of and the
		// arguments that made it - are what the match is over.
		if (pattern != argument && types_.is_specialization(pattern) &&
		    types_.is_specialization(argument) &&
		    types_.template_name(pattern) == types_.template_name(argument))
		{
			const std::vector<TypeId>& wanted = types_.template_arguments(pattern);
			const std::vector<TypeId>& given = types_.template_arguments(argument);
			if (wanted.size() != given.size())
			{
				return false;
			}
			for (std::size_t index = 0; index < wanted.size(); ++index)
			{
				if (!deduce(wanted[index], given[index], bindings))
				{
					return false;
				}
			}
			return true;
		}
		return pattern == argument;

	default:
		// A fundamental type or an enumeration holds no parameter to deduce,
		// so the two agree exactly when they are the same type.
		return pattern == argument;
	}
}

SemaEntity* SemaAnalyzer::deduce_specialization(
	SemaEntity& primary, const std::vector<Value>& arguments)
{
	// 14.8.2.1p1: each parameter is deduced from the argument passed to it, so
	// a call that passes more of them deduces nothing.  8.3.6p1 lets a call
	// leave the trailing parameters a default-argument stands for unwritten;
	// those deduce nothing, and 14.8.2p5 below is what says whether what is
	// left named every parameter.
	const std::vector<TypeId>& pattern = types_.parameters(primary.type);
	if (arguments.size() > pattern.size() || types_.variadic(primary.type) ||
	    (arguments.size() < pattern.size() &&
	     !accepts_arity(primary, arguments.size())))
	{
		return nullptr;
	}
	std::unordered_map<TypeId, TypeId> bindings;
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		// 14.8.2.5p3: a parameter written over no template parameter deduces
		// nothing at all, so whether the argument reaches it is 13.3's question
		// about a conversion rather than this one about a substitution.
		if (!types_.is_dependent(pattern[index]))
		{
			continue;
		}
		// 13.4p1: an argument that is an unresolved overload set has no type of
		// its own, and 14.8.2.1p6 leaves it deducing nothing.
		// 14.8.2.1p2: where the parameter is a reference, what deduces the
		// arguments is the type it refers to, and the argument's own type is
		// used as it stands rather than decayed.
		const TypeKind kind = types_.kind(pattern[index]);
		const bool reference = kind == TypeKind::LValueReference ||
			kind == TypeKind::RValueReference;
		const TypeId expected =
			reference ? types_.target(pattern[index]) : pattern[index];
		if (arguments[index].type == kNoType)
		{
			// 14.8.2.1p6: an argument that is an overload set has no type of its
			// own, so the deduction is tried against each declaration in it and
			// stands only where exactly one of them deduces.  A set holding a
			// template is left alone: 13.4p1 has no target type here to make one
			// of its specializations, so the parameter deduces nothing at all.
			if (!deduce_overload_set(arguments[index], expected, bindings,
			                         reference))
			{
				return nullptr;
			}
			continue;
		}
		const TypeId given =
			reference ? arguments[index].type : decayed(arguments[index]);
		if (!deduce(expected, given, bindings, reference))
		{
			return nullptr;
		}
	}

	std::vector<TypeId> deduced;
	if (!deduced_arguments(primary, bindings, deduced))
	{
		return nullptr;
	}
	return &specialize(primary, deduced);
}

// 14.8.2p5: the argument each parameter of `primary` was deduced, or false
// where one of them was left without a value - which leaves the specialization
// unmade, because there is nothing to substitute for it.
bool SemaAnalyzer::deduced_arguments(
	const SemaEntity& primary,
	const std::unordered_map<TypeId, TypeId>& bindings,
	std::vector<TypeId>& out)
{
	const std::vector<SemaEntity*>& parameters =
		primary.template_parameters->declarations;
	out.reserve(parameters.size());
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		const std::unordered_map<TypeId, TypeId>::const_iterator bound =
			bindings.find(parameters[index]->type);
		if (bound == bindings.end())
		{
			return false;
		}
		out.push_back(bound->second);
	}
	return true;
}

// 14.8.2.1p6: the deduction one parameter gets from an argument that is an
// unresolved overload set.  Each declaration in the set is tried on a copy of
// the bindings so far, and the deduction stands only where exactly one of them
// succeeded - two that both deduce leave the parameter as ambiguous as the name
// was.  A set holding a function template names no one type to try, so the
// parameter is left deducing nothing rather than refused.
bool SemaAnalyzer::deduce_overload_set(
	const Value& argument, TypeId expected,
	std::unordered_map<TypeId, TypeId>& bindings, bool reference)
{
	if (argument.functions == nullptr)
	{
		// 13.3.3.1.5p1's braced-init-list is the other value with no type, and
		// it names nothing a parameter can be deduced from.
		return false;
	}
	std::unordered_map<TypeId, TypeId> only;
	std::size_t deduced = 0;
	for (std::size_t index = 0; index < argument.functions->size(); ++index)
	{
		for (SemaEntity* at = (*argument.functions)[index]; at != nullptr;
		     at = at->next)
		{
			if (at->template_parameters != nullptr || at->object_member)
			{
				// 13.4p1 chooses between the non-member declarations of the name;
				// a member's pointer is a pointer to member, which 14.8.2.5 makes
				// a pair of its own that this argument did not write.
				continue;
			}
			// 4.3p1: what the name stands for where it is passed is a pointer to
			// the function, which is what a non-reference parameter is deduced
			// from and what a reference parameter refers to.
			std::unordered_map<TypeId, TypeId> tried(bindings);
			if (!deduce(expected, reference ? at->type
			                                : types_.pointer_to(at->type),
			            tried, reference))
			{
				continue;
			}
			++deduced;
			only.swap(tried);
		}
	}
	if (deduced != 1)
	{
		// 14.8.2.1p6: a set no member of deduces, and one two members deduce
		// differently, both leave the parameter a non-deduced context - so what
		// it is written over is deduced from the arguments elsewhere and 13.4p1
		// then picks the declaration the substituted parameter names.
		return true;
	}
	bindings.swap(only);
	return true;
}

SemaEntity* SemaAnalyzer::deduce_target(SemaEntity& primary, TypeId wanted)
{
	// 14.8.2.2p1: the target type is the one A the deduction is over, so the
	// result type and every parameter type of the template are matched against
	// it at once - which is what `deduce` does with a function type.
	std::unordered_map<TypeId, TypeId> bindings;
	if (!deduce(primary.type, wanted, bindings))
	{
		return nullptr;
	}
	std::vector<TypeId> deduced;
	if (!deduced_arguments(primary, bindings, deduced))
	{
		return nullptr;
	}
	SemaEntity& made = specialize(primary, deduced);
	// 14.8.2.2p2: what the target names is a declaration of exactly that type,
	// so a substitution that produced another one chose nothing.
	return made.type == wanted ? &made : nullptr;
}

void SemaAnalyzer::instantiate(SemaEntity& function)
{
	if (function.instantiated)
	{
		return;
	}
	function.instantiated = true;
	SemaEntity& primary = *function.primary;
	if (primary.defined)
	{
		// 14.7.1p1: instantiating a template that has a definition instantiates
		// the definition, which is the body read again against the arguments
		// rather than a declaration built from them.
		if (primary.templated == nullptr ||
		    primary.templated->pattern == nullptr)
		{
			throw std::runtime_error("a function template with a definition is "
			                         "instantiated, which this milestone does "
			                         "not describe");
		}
		const TemplateInfo& info = *primary.templated;
		const std::vector<TypeId>& arguments =
			types_.type_list_at(function.template_arguments);
		Context inner;
		inner.scope = &model_.open(ScopeKind::TemplateParameters, *info.region,
		                           nullptr, info.dump);
		inner.dump = info.dump;
		// 9.2p2 and 14.7.1p1: the definition is written where the output puts
		// one the program did not write - among the pending ones, at the end of
		// the unit - so it stands under no declaration's node.
		inner.node = nullptr;
		for (std::size_t index = 0;
		     index < info.parameters.size() && index < arguments.size(); ++index)
		{
			if (info.parameters[index].empty())
			{
				continue;
			}
			SemaEntity& bound = model_.create(SemaKind::Typedef,
			                                  info.parameters[index],
			                                  arguments[index]);
			model_.bind(*inner.scope, bound.name, bound);
			model_.declare_in(*inner.scope, bound);
		}
		// 14p1 and 3.2p3: every unit that names this specialization
		// instantiates the same definition from the same template, so the
		// definition is not this unit's to own - it binds the way an inline
		// one does and the program keeps one of them.
		function.inline_function = true;
		SemaEntity* const enclosing = instantiating_;
		instantiating_ = &function;
		function_definition(*info.pattern, inner);
		instantiating_ = enclosing;
		return;
	}
	// The declaration the specialization stands for is written where the output
	// puts a definition the program did not write: at the end of the unit, in
	// the order the specializations were asked for.
	Pending pending;
	pending.function = &function;
	pending.instantiation = true;
	pending_.push_back(pending);
}

void SemaAnalyzer::write_instantiation(const Pending& pending)
{
	const SemaEntity& function = *pending.function;
	const SemaEntity& primary = *function.primary;
	// 14.7.1p1: the specialization stands for a declaration of the template's
	// own name, written with the types the arguments made of its parameters.
	DumpNode& line = model_.open_node(model_.unit(), "function-declaration " +
	                                  primary.dump_name + " " +
	                                  function_description(
		                                  function.type,
		                                  function.object_member));
	const std::unordered_map<std::uint32_t, std::vector<Parameter> >::const_iterator
		written = templates_.find(primary.id);
	const std::vector<TypeId>& parameters = types_.parameters(function.type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		// The names are the template's declarator's; the types are what the
		// substitution made of them.
		const bool named = written != templates_.end() &&
			index < written->second.size();
		model_.open_node(line, "parameter " +
		                 (named ? written->second[index].name : std::string()) +
		                 " " + types_.description(parameters[index]));
	}
}

// --- the class tier ------------------------------------------------------

namespace
{

// 9p1: which class-key a class-specifier or elaborated-type-specifier wrote.
ClassTag class_tag_of(const AstNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind != AstKind::ClassKey)
		{
			continue;
		}
		if (child.token == KW_CLASS)
		{
			return ClassTag::Class;
		}
		return child.token == KW_UNION ? ClassTag::Union : ClassTag::Struct;
	}
	return ClassTag::Struct;
}

std::string decimal_text(unsigned long long value)
{
	std::string digits;
	unsigned long long rest = value;
	while (rest != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
		rest /= 10;
	}
	return digits.empty() ? std::string("0") : digits;
}

const AstNode* first_child(const AstNode& node, AstKind kind)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->kind == kind)
		{
			return node.children[index];
		}
	}
	return nullptr;
}

}

// 14.1p2: the parameters a template-parameter-clause declared, in the order it
// wrote them, and 14.1p9's default arguments beside them.  A parameter this
// milestone gives no meaning to leaves the head unsupported rather than
// refusing it here: 14p1 lets a program declare a template it never names, and
// the declaration says nothing about a type until an instantiation asks.
void SemaAnalyzer::read_template_head(const AstNode& clause, TemplateInfo& info)
{
	const AstNode* const list =
		first_child(clause, AstKind::TemplateParameterList);
	if (list == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < list->children.size(); ++index)
	{
		const AstNode& parameter = *list->children[index];
		const AstNode* const id = first_child(parameter, AstKind::Identifier);
		if (parameter.kind != AstKind::TypeParameter ||
		    first_child(parameter, AstKind::TemplateTemplateParameter) != nullptr ||
		    first_child(parameter, AstKind::ParameterPack) != nullptr)
		{
			// 14.1p2's non-type parameter, 14.1p1's template parameter and
			// 14.5.3's pack all belong to later milestones.
			info.supported = false;
			info.parameters.push_back(std::string());
			info.defaults.push_back(nullptr);
			continue;
		}
		// 14.1p3: a type parameter with no identifier declares nothing a
		// dependent name can reach, but it still takes an argument.
		info.parameters.push_back(id == nullptr ? std::string() : id->text);
		const AstNode* const written =
			first_child(parameter, AstKind::DefaultTemplateArgument);
		info.defaults.push_back(
			written == nullptr ? nullptr
			                   : first_child(*written, AstKind::TypeId));
	}
}

// 14p1: records what a template-declaration parameterises rather than reading
// it.  A class template declares no class until 14.7.1p1 instantiates one, so
// the name the class-head wrote is bound in the region the template-declaration
// stands in - which is what a use of the template looks in - and the body is
// left as the syntax an instantiation reads.
bool SemaAnalyzer::record_template(const AstNode& node, const Context& ctx)
{
	const AstNode* clause = nullptr;
	const AstNode* declared = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::TemplateParameterClause)
		{
			if (clause != nullptr)
			{
				// 14.5.2: a member template writes a second clause, which this
				// milestone leaves out.
				return false;
			}
			clause = &child;
			continue;
		}
		declared = &child;
	}
	if (clause == nullptr || declared == nullptr)
	{
		return false;
	}
	// 14.5.1.3p1: a definition written outside its class belongs to the
	// template that class is one of, and is read for a specialization after
	// the body - including for one the unit already made.  A class-head-name
	// with a nested-name-specifier is one of those, so the question is asked
	// before the class tier's.
	SemaEntity* const owner = member_definition_owner(*declared, ctx);
	if (owner != nullptr)
	{
		owner->templated->members.push_back(declared);
		for (std::size_t index = 0;
		     index < owner->templated->specializations.size(); ++index)
		{
			instantiate_member(*owner->templated->specializations[index],
			                   *declared);
		}
		return true;
	}
	if (declared->kind != AstKind::ClassSpecifier &&
	    declared->kind != AstKind::ClassForwardDeclaration)
	{
		return false;
	}
	const QualifiedName spelled(declared->text);
	if (spelled.qualified() || declared->text.empty())
	{
		// A class-head-name with a nested-name-specifier that reaches no class
		// template is not what a class template declares.
		return false;
	}
	const std::string& name = declared->text;
	const bool define = declared->kind == AstKind::ClassSpecifier;

	// 9.1p2 and 14p1: a second template-declaration of one name declares the
	// same template, and the one that wrote a body is what an instantiation
	// reads.
	SemaEntity* entity = model_.find(*ctx.scope, name, LookupKind::Type);
	if (entity != nullptr &&
	    (entity->kind != SemaKind::Class || entity->templated == nullptr))
	{
		throw std::runtime_error("a template declaration of " + name +
		                         " redeclares a name that is not a class "
		                         "template");
	}
	if (entity == nullptr)
	{
		const std::uint32_t id = model_.type_entity_id();
		// The template itself names no type an object can be made of; the
		// spelling is what a diagnostic and a specialization's name are built
		// from.
		const TypeId type = types_.class_type(
			id, class_tag_of(*declared), dump_name(*ctx.scope, name),
			abi_name(*ctx.scope, name));
		entity = &model_.create(SemaKind::Class, name, type);
		own_type(type, *entity);
		model_.bind(*ctx.scope, name, *entity);
		model_.declare_in(*ctx.scope, *entity);
		template_patterns_.push_back(TemplateInfo());
		entity->templated = &template_patterns_.back();
		entity->templated->region = ctx.scope;
		entity->templated->dump = ctx.dump;
		read_template_head(*clause, *entity->templated);
	}
	else if (define && entity->templated->pattern != nullptr &&
	         entity->templated->pattern->kind == AstKind::ClassSpecifier)
	{
		throw std::runtime_error("a class template is defined twice");
	}
	if (define)
	{
		entity->templated->pattern = declared;
		// 14.1p2: the parameter names of the definition are the ones its body
		// wrote, whatever an earlier declaration called them.
		TemplateInfo head;
		read_template_head(*clause, head);
		if (head.parameters.size() != entity->templated->parameters.size())
		{
			throw std::runtime_error("two declarations of the class template " +
			                         name + " write different numbers of "
			                         "template parameters");
		}
		entity->templated->parameters = head.parameters;
		entity->templated->supported =
			entity->templated->supported && head.supported;
		// 14.1p10: the defaults of every declaration are merged, and the one
		// that wrote a default keeps it.
		for (std::size_t index = 0; index < head.defaults.size(); ++index)
		{
			if (head.defaults[index] != nullptr)
			{
				entity->templated->defaults[index] = head.defaults[index];
			}
		}
		// 14.7.1p1: a specialization the unit named before the definition
		// arrived is an incomplete class until here, and the definition is
		// what completes it.
		for (std::size_t index = 0;
		     index < entity->templated->specializations.size(); ++index)
		{
			complete_specialization(
				*entity->templated->specializations[index]);
		}
	}
	// The dump names the template where it was written, as the declaration it
	// parameterises spells it.
	ctx.dump->lines.push_back("type " + name + " " +
	                          (class_tag_of(*declared) == ClassTag::Union
	                               ? "union "
	                               : (class_tag_of(*declared) == ClassTag::Class
	                                      ? "class "
	                                      : "struct ")) +
	                          name);
	return true;
}

// 9.1p2: the declaration a type was made by, recorded where the declaration is
// read.
//
// The model answers it for a walk that already has the type in hand.  The type
// table carries it because an object-file name encoded from a *use* of the
// type - a parameter, a template argument, the type a table is named after -
// has only the type, and the one spelling the type carries cannot say which of
// its components a template made.
void SemaAnalyzer::own_type(TypeId type, SemaEntity& entity)
{
	model_.own_type(type, entity);
	types_.set_declaration(type, &entity);
}

// The source spelling of a type, which is what a specialization is named by.
// 14.7.1p1 makes one declaration of every naming of one argument list, so two
// spellings of one type - a typedef-name and what it names - have to reach the
// same name; the spelling is therefore written from the type rather than from
// the terminals a use wrote.
std::string SemaAnalyzer::type_spelling(TypeId type) const
{
	std::string out;
	TypeId at = type;
	std::string suffix;
	for (;;)
	{
		const unsigned cv = types_.cv(at);
		const TypeKind kind = types_.kind(at);
		if (kind == TypeKind::Pointer || kind == TypeKind::LValueReference ||
		    kind == TypeKind::RValueReference)
		{
			std::string mark = kind == TypeKind::Pointer
				? "*"
				: (kind == TypeKind::LValueReference ? "&" : "&&");
			if ((cv & kCvConst) != 0)
			{
				mark += "const";
			}
			if ((cv & kCvVolatile) != 0)
			{
				mark += "volatile";
			}
			suffix = mark + suffix;
			at = types_.target(at);
			continue;
		}
		if (kind == TypeKind::Array)
		{
			suffix = (types_.bounded(at)
				          ? "[" + decimal_text(types_.bound(at)) + "]"
				          : std::string("[]")) + suffix;
			at = types_.target(at);
			continue;
		}
		if ((cv & kCvConst) != 0)
		{
			out += "const ";
		}
		if ((cv & kCvVolatile) != 0)
		{
			out += "volatile ";
		}
		switch (kind)
		{
		case TypeKind::Class:
		case TypeKind::Enum:
		case TypeKind::TemplateParameter:
			out += types_.user_qualified_name(at);
			break;

		case TypeKind::Function:
		{
			out += type_spelling(types_.target(at)) + "(";
			const std::vector<TypeId>& given = types_.parameters(at);
			for (std::size_t index = 0; index < given.size(); ++index)
			{
				if (index != 0)
				{
					out += ",";
				}
				out += type_spelling(given[index]);
			}
			if (types_.variadic(at))
			{
				out += given.empty() ? "..." : ",...";
			}
			out += ")";
			break;
		}

		case TypeKind::MemberPointer:
			out += type_spelling(types_.target(at)) + " " +
				type_spelling(types_.member_class(at)) + "::";
			suffix = "*" + suffix;
			break;

		default:
			out += fundamental_type_name(types_.fundamental_type(at));
			break;
		}
		break;
	}
	return out + suffix;
}

// 14.3p1: the arguments a template-argument-list binds to the parameters of
// `primary`, with 14.1p9's defaults filling in the ones the list stopped short
// of.  A default is read in a region that already binds the parameters before
// it, because 14.1p9 lets it name them.
void SemaAnalyzer::bind_template_arguments(
	SemaEntity& primary, const std::vector<std::string>& written,
	const Context& ctx, std::vector<TypeId>& out)
{
	const TemplateInfo& info = *primary.templated;
	if (!info.supported)
	{
		throw std::runtime_error(primary.name + " is a template whose "
		                         "parameters PA19 does not instantiate");
	}
	if (written.size() > info.parameters.size())
	{
		throw std::runtime_error("a template-argument-list gives " +
		                         primary.name + " more arguments than it has "
		                         "parameters");
	}
	out.reserve(info.parameters.size());
	for (std::size_t index = 0; index < written.size(); ++index)
	{
		out.push_back(template_argument_type(written[index], ctx));
	}
	if (out.size() == info.parameters.size())
	{
		return;
	}
	// The defaults of one list of explicit arguments are one answer however
	// many times the template is named that way, so the region they are read
	// in is opened once and the answer is kept.
	const std::uint64_t key =
		(static_cast<std::uint64_t>(primary.id) << 32) | types_.type_list(out);
	const std::unordered_map<std::uint64_t, std::vector<TypeId> >::const_iterator
		held = default_arguments_.find(key);
	if (held != default_arguments_.end())
	{
		out = held->second;
		return;
	}
	const std::vector<TypeId> explicitly = out;
	Context inner;
	inner.scope = &model_.open(ScopeKind::TemplateParameters, *info.region,
	                           nullptr, info.dump);
	inner.dump = info.dump;
	inner.node = nullptr;
	for (std::size_t index = 0; index < info.parameters.size(); ++index)
	{
		if (index == out.size())
		{
			if (info.defaults[index] == nullptr)
			{
				throw std::runtime_error("a template-argument-list gives " +
				                         primary.name + " too few arguments");
			}
			out.push_back(type_id_type(*info.defaults[index], inner));
		}
		if (!info.parameters[index].empty())
		{
			SemaEntity& bound = model_.create(SemaKind::Typedef,
			                                  info.parameters[index],
			                                  out[index]);
			model_.bind(*inner.scope, bound.name, bound);
			model_.declare_in(*inner.scope, bound);
		}
	}
	default_arguments_.insert(std::make_pair(key, out));
	static_cast<void>(explicitly);
}

// 14.7.1p1: the class `arguments` makes of the class template `primary`.
//
// The specialization is one declaration however many times it is named, so the
// pattern is read once, against a region binding each parameter to the type its
// argument named.  It is held before the body is read: a class template whose
// body names its own specialization - a member of it, a pointer to it - has to
// find the declaration already made rather than start a second reading of it.
SemaEntity& SemaAnalyzer::instantiate_class(SemaEntity& primary,
                                            const std::vector<TypeId>& arguments)
{
	const std::uint32_t list = types_.type_list(arguments);
	SemaEntity* made = model_.specialization_of(primary, list);
	if (made != nullptr)
	{
		return *made;
	}
	const TemplateInfo& info = *primary.templated;
	std::string spelled = primary.name + "<";
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (index != 0)
		{
			spelled += ",";
		}
		spelled += type_spelling(arguments[index]);
	}
	spelled += ">";

	const ClassTag tag = info.pattern != nullptr
		? class_tag_of(*info.pattern)
		: types_.class_tag(primary.type);
	const std::uint32_t id = model_.type_entity_id();
	const std::string qualified = abi_name(*info.region, spelled);
	const TypeId type = types_.class_type(id, tag,
	                                      dump_name(*info.region, spelled),
	                                      qualified);
	made = &model_.create(SemaKind::Class, spelled, type);
	made->primary = &primary;
	made->template_arguments = list;
	made->region = info.region;
	made->access = primary.access;
	own_type(type, *made);
	// The ABI writes the template's own name and then the arguments, which the
	// one spelling above cannot be split back into.
	types_.set_template_arguments(type, abi_name(*info.region, primary.name),
	                              arguments);
	model_.hold_specialization(primary, list, *made);
	// 14.7.1p1: the specialization exists whether or not the template has a
	// definition yet; a template the unit only declared makes an incomplete
	// class, which is all a pointer or a reference to it needs, and the
	// definition completes it where it arrives.
	primary.templated->specializations.push_back(made);
	complete_specialization(*made);
	return *made;
}

void SemaAnalyzer::complete_specialization(SemaEntity& made)
{
	const TemplateInfo& info = *made.primary->templated;
	if (made.defined || info.pattern == nullptr ||
	    info.pattern->kind != AstKind::ClassSpecifier ||
	    types_.is_dependent(made.type))
	{
		// 14.6.2p1: a template-id written inside a template over that
		// template's own parameters names no class yet.  It is a declaration
		// of one - which is what deduction matches and what a reference or a
		// pointer to it needs - and the specialization the arguments make of
		// it is completed where they are known.
		return;
	}
	Context inner;
	inner.scope = &open_template_bindings(
		info, types_.type_list_at(made.template_arguments));
	inner.dump = info.dump;
	inner.node = nullptr;
	Span span;
	span.begin = info.pattern->begin;
	span.end = info.pattern->end;
	const std::string spelled = made.name;
	class_declaration(*info.pattern, inner, span, true, std::string(), &made,
	                  &spelled);
	// 14.5.1.3p1: what the template's members were defined as outside its
	// class is read now that the class is complete, and a definition written
	// after this specialization was made is read for it where it stands.
	for (std::size_t index = 0; index < info.members.size(); ++index)
	{
		instantiate_member(made, *info.members[index]);
	}
}

// 14.2: the specialization a name written as a template-id denotes.
//
// Ordinary lookup finds the template, because that is the name the program
// declared; the arguments the id wrote are what turns it into a declaration.
// Null for a name that is no template-id and for one whose template this
// milestone does not instantiate, so every caller falls back to what the
// earlier assignments did with it.
SemaEntity* SemaAnalyzer::template_id_entity(const std::string& component,
                                             const Context& ctx, Scope* in,
                                             LookupKind filter)
{
	if (!lowering() || component.find('<') == std::string::npos)
	{
		return nullptr;
	}
	const TemplateId id(component);
	if (!id.valid())
	{
		return nullptr;
	}
	SemaEntity* primary =
		in != nullptr ? model_.lookup_in(*in, id.name(), LookupKind::Type)
		              : model_.lookup(*ctx.scope, id.name(), LookupKind::Type);
	if (primary != nullptr && primary->templated == nullptr &&
	    primary->primary != nullptr)
	{
		// 14.6.1p1: inside a specialization the injected-class-name is bound to
		// that specialization, and a template-argument-list written after it
		// names the template it was made of rather than the class it found.
		primary = primary->primary;
	}
	if (primary == nullptr || primary->kind != SemaKind::Class ||
	    primary->templated == nullptr)
	{
		return nullptr;
	}
	static_cast<void>(filter);
	std::vector<TypeId> arguments;
	bind_template_arguments(*primary, id.arguments(), ctx, arguments);
	return &instantiate_class(*primary, arguments);
}

// 14p1: the pattern a function template's declaration was written from, taken
// from the walk that is reading the template-declaration.
//
// A function template's name is declared in the region around its parameter
// clause, so the declaration is made by the ordinary path and this only hands
// it what 14.7.1p1 needs to read the same syntax again: the syntax, the region
// its names are looked up from, and the parameters its head declared.
void SemaAnalyzer::record_function_template(SemaEntity& entity,
                                            Scope& parameters, Scope& region)
{
	if (!lowering() || template_pattern_ == nullptr ||
	    template_pattern_->kind != AstKind::FunctionDefinition)
	{
		return;
	}
	template_patterns_.push_back(TemplateInfo());
	entity.templated = &template_patterns_.back();
	TemplateInfo& info = *entity.templated;
	info.pattern = template_pattern_;
	info.region = &region;
	info.dump = template_pattern_dump_;
	for (std::size_t index = 0; index < parameters.declarations.size(); ++index)
	{
		const SemaEntity& parameter = *parameters.declarations[index];
		// 14.1p2: a parameter this milestone does not bind leaves the pattern
		// unreadable, which an instantiation of it - and nothing else - finds.
		info.supported = info.supported &&
			parameter.kind == SemaKind::TemplateType;
		info.parameters.push_back(parameter.name);
		info.defaults.push_back(nullptr);
	}
}

// 14.5.1.3p1: the class template a definition written outside its class is a
// member of.
//
// The declarator-id names it, and 14.5.1.3p1 makes the nested-name-specifier a
// template-id over the same parameters the head declared - so the owner is the
// template the first such component reaches.  Null where the declaration is a
// member of no template, which is every ordinary out-of-class definition.
SemaEntity* SemaAnalyzer::member_definition_owner(const AstNode& node,
                                                  const Context& ctx)
{
	const AstNode* id = nullptr;
	std::string spelling;
	if (node.kind == AstKind::ClassSpecifier ||
	    node.kind == AstKind::ClassForwardDeclaration ||
	    node.kind == AstKind::SpecialMemberDefinition ||
	    node.kind == AstKind::SpecialMemberDeclaration)
	{
		// 9.1p2 and 12.1p1: a class-head-name and a constructor's declarator-id
		// are the name the node itself carries.
		spelling = node.text;
	}
	else if (node.kind == AstKind::FunctionDefinition && node.children.size() > 1)
	{
		id = declarator_id(*node.children[1]);
	}
	else
	{
		for (std::size_t index = 0; id == nullptr &&
		     index < node.children.size(); ++index)
		{
			const AstNode& child = *node.children[index];
			if (child.kind != AstKind::InitDeclaratorList)
			{
				continue;
			}
			for (std::size_t at = 0; id == nullptr &&
			     at < child.children.size(); ++at)
			{
				if (!child.children[at]->children.empty())
				{
					id = declarator_id(*child.children[at]->children[0]);
				}
			}
		}
	}
	if (id != nullptr)
	{
		spelling = id->text;
	}
	if (spelling.empty())
	{
		return nullptr;
	}
	const QualifiedName spelled(spelling);
	for (std::size_t index = 0; index + 1 < spelled.size(); ++index)
	{
		const TemplateId written(spelled.part(index));
		if (!written.valid())
		{
			continue;
		}
		SemaEntity* const named =
			model_.lookup(*ctx.scope, written.name(), LookupKind::Type);
		if (named != nullptr && named->kind == SemaKind::Class &&
		    named->templated != nullptr)
		{
			return named;
		}
	}
	return nullptr;
}

Scope& SemaAnalyzer::open_template_bindings(const TemplateInfo& info,
                                            const std::vector<TypeId>& arguments)
{
	Scope& bindings = model_.open(ScopeKind::TemplateParameters, *info.region,
	                              nullptr, info.dump);
	for (std::size_t index = 0;
	     index < info.parameters.size() && index < arguments.size(); ++index)
	{
		if (info.parameters[index].empty())
		{
			continue;
		}
		SemaEntity& bound = model_.create(SemaKind::Typedef,
		                                  info.parameters[index],
		                                  arguments[index]);
		model_.bind(bindings, bound.name, bound);
		model_.declare_in(bindings, bound);
	}
	return bindings;
}

void SemaAnalyzer::instantiate_member(SemaEntity& specialization,
                                      const AstNode& pattern)
{
	const TemplateInfo& info = *specialization.primary->templated;
	Context inner;
	inner.scope = &open_template_bindings(
		info, types_.type_list_at(specialization.template_arguments));
	inner.dump = info.dump;
	// 9.4.2p1 and 9.3p2: the definition declares into the class the
	// declarator-id names, and writes its lines where that class writes them.
	inner.node = &model_.unit();
	declaration(pattern, inner);
}

TypeId SemaAnalyzer::substituted(
	TypeId type, const std::unordered_map<TypeId, TypeId>& bindings,
	std::unordered_map<TypeId, TypeId>& memo)
{
	// The memo is asked first: a type this substitution has already rebuilt is
	// what it was rebuilt into, whatever it is made of.
	const std::unordered_map<TypeId, TypeId>::const_iterator held =
		memo.find(type);
	if (held != memo.end())
	{
		return held->second;
	}
	if (!types_.is_dependent(type))
	{
		return type;
	}
	TypeId out = type;
	const unsigned cv = types_.cv(type);
	const TypeId bare = types_.strip_cv(type);
	switch (types_.kind(bare))
	{
	case TypeKind::Class:
	{
		// 14.7.1p1: the arguments of the specialization are what the bindings
		// reach, and the class they then name is one an instantiation makes.
		SemaEntity* const made = model_.type_owner(bare);
		if (made == nullptr || made->primary == nullptr)
		{
			break;
		}
		const std::vector<TypeId>& written = types_.template_arguments(bare);
		std::vector<TypeId> arguments;
		arguments.reserve(written.size());
		for (std::size_t index = 0; index < written.size(); ++index)
		{
			arguments.push_back(substituted(written[index], bindings, memo));
		}
		out = types_.qualified(
			instantiate_class(*made->primary, arguments).type, cv);
		break;
	}

	case TypeKind::Pointer:
		out = types_.qualified(
			types_.pointer_to(substituted(types_.target(bare), bindings, memo)),
			cv);
		break;

	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		out = types_.reference_to(
			substituted(types_.target(bare), bindings, memo),
			types_.kind(bare) == TypeKind::RValueReference);
		break;

	case TypeKind::Array:
		out = types_.array_of(substituted(types_.target(bare), bindings, memo),
		                      types_.bounded(bare), types_.bound(bare));
		break;

	case TypeKind::MemberPointer:
		out = types_.qualified(
			types_.member_pointer_to(
				substituted(types_.member_class(bare), bindings, memo),
				substituted(types_.target(bare), bindings, memo)),
			cv);
		break;

	case TypeKind::Function:
	{
		const std::vector<TypeId>& given = types_.parameters(bare);
		std::vector<TypeId> parameters;
		parameters.reserve(given.size());
		for (std::size_t index = 0; index < given.size(); ++index)
		{
			parameters.push_back(substituted(given[index], bindings, memo));
		}
		TypeId built = types_.function_of(
			substituted(types_.target(bare), bindings, memo), parameters,
			types_.variadic(bare));
		// 8.3.5p7 and 8.3.5p1: what a member function's declarator wrote after
		// its parameter-clause is part of the type and is not a qualifier
		// `qualified` would carry.
		built = types_.qualified_function(built, cv);
		out = types_.ref_qualified_function(built,
		                                    types_.function_ref_qualifier(bare));
		break;
	}

	default:
		// A template parameter itself, which is what the bindings name.
		out = types_.substitute(type, bindings, memo);
		break;
	}
	memo.insert(std::make_pair(type, out));
	return out;
}
