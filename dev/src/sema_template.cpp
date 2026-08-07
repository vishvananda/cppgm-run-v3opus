#include "sema_analyzer.h"

#include <stdexcept>
#include <utility>

#include "ast_model.h"

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

namespace
{

bool is_name_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '$';
}

// The terminals a type-id was written from, recovered from the spelling PA10
// handed on.  A template-argument reaches here inside a name rather than as a
// tree of its own, and the PA12 subset writes one out of identifiers, keywords,
// `::` and the three declarator operators that build a pointer or a reference.
// False for anything else, which is an argument this assignment does not read.
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
			while (at < spelling.size() && is_name_char(spelling[at]))
			{
				++at;
			}
			out.push_back(spelling.substr(start, at - start));
			continue;
		}
		if (c == ':' && at + 1 < spelling.size() && spelling[at + 1] == ':')
		{
			out.push_back("::");
			at += 2;
			continue;
		}
		if (c == '&' && at + 1 < spelling.size() && spelling[at + 1] == '&')
		{
			out.push_back("&&");
			at += 2;
			continue;
		}
		if (c == '*' || c == '&')
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
	// 8.1p1: a type-id is a type-specifier-seq and the declarator operators
	// written after it.  7.1.6.1p1 lets a cv-qualifier stand on either side of
	// the specifiers it qualifies, so the seq is read until the first operator.
	unsigned cv = kCvNone;
	std::string written;
	std::size_t at = 0;
	for (; at < words.size(); ++at)
	{
		const std::string& word = words[at];
		if (word == "*" || word == "&" || word == "&&")
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
		SemaEntity* const named = resolve(written, ctx, LookupKind::Type);
		if (named == nullptr || !names_a_type(*named))
		{
			throw std::runtime_error(spelling + " does not name a type");
		}
		type = named->type;
	}
	type = types_.qualified(type, cv);
	for (; at < words.size(); ++at)
	{
		const std::string& word = words[at];
		if (word == "*")
		{
			type = types_.pointer_to(type);
		}
		else if (word == "&" || word == "&&")
		{
			type = types_.reference_to(type, word == "&&");
		}
		else if (word == "const" || word == "volatile")
		{
			// 8.3.1p1: a cv-qualifier after a `*` qualifies the pointer.
			type = types_.qualified(type,
			                        word == "const" ? kCvConst : kCvVolatile);
		}
		else
		{
			throw std::runtime_error(spelling + " is not a type-id PA12 reads");
		}
	}
	return type;
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
		                      types_.substitute(primary.type, bindings, memo));
		made->primary = &primary;
		made->region = primary.region;
		made->object_member = primary.object_member;
		// 14.7.1p1: the specialization is a declaration of the template's own
		// name, which is what the output calls it wherever it names a
		// declaration rather than repeating what a use wrote.
		made->dump_name = primary.dump_name;
		made->abi_name = primary.abi_name;
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
                          std::unordered_map<TypeId, TypeId>& bindings)
{
	if (types_.kind(pattern) == TypeKind::TemplateParameter)
	{
		// 14.8.2.5p4: the qualifiers the parameter writes are matched by the
		// argument's own, and what is left of the argument is what the
		// parameter names.
		if ((types_.cv(pattern) & ~types_.cv(argument)) != 0)
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
	    types_.cv(pattern) != types_.cv(argument))
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

	default:
		// A fundamental type, a class or an enumeration holds no parameter to
		// deduce, so the two agree exactly when they are the same type.
		return pattern == argument;
	}
}

SemaEntity* SemaAnalyzer::deduce_specialization(
	SemaEntity& primary, const std::vector<Value>& arguments)
{
	// 14.8.2.1p1: each parameter is deduced from the argument passed to it, so
	// a call that passes another number of them deduces nothing.
	const std::vector<TypeId>& pattern = types_.parameters(primary.type);
	if (arguments.size() != pattern.size() || types_.variadic(primary.type))
	{
		return nullptr;
	}
	std::unordered_map<TypeId, TypeId> bindings;
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		// 13.4p1: an argument that is an unresolved overload set has no type of
		// its own, and 14.8.2.1p6 leaves it deducing nothing.
		if (arguments[index].type == kNoType ||
		    !deduce(pattern[index], decayed(arguments[index]), bindings))
		{
			return nullptr;
		}
	}

	// 14.8.2p5: a parameter no argument deduced leaves the specialization
	// unmade, because there is nothing to substitute for it.
	const std::vector<SemaEntity*>& parameters =
		primary.template_parameters->declarations;
	std::vector<TypeId> deduced;
	deduced.reserve(parameters.size());
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		const std::unordered_map<TypeId, TypeId>::const_iterator bound =
			bindings.find(parameters[index]->type);
		if (bound == bindings.end())
		{
			return nullptr;
		}
		deduced.push_back(bound->second);
	}
	return &specialize(primary, deduced);
}

void SemaAnalyzer::instantiate(SemaEntity& function)
{
	if (function.instantiated)
	{
		return;
	}
	function.instantiated = true;
	if (function.primary->defined)
	{
		// 14.7.1p1: instantiating a template that has a definition instantiates
		// the definition, which is a body read again against the arguments
		// rather than a declaration built from them.  PA12 has no rule that
		// reads one, so the program is refused rather than described as though
		// the definition were not there.
		throw std::runtime_error("a function template with a definition is "
		                         "instantiated, which PA12 does not describe");
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
