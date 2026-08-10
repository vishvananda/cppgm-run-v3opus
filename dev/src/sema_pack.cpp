#include "sema_pack.h"

#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "ast_model.h"
#include "sema_analyzer.h"

// 14.5.3's parameter packs, read out of the spellings PA10 handed on.
//
// A pack place is the one place of a template head that binds more than one
// argument, and 14.5.3p4's expansion is the one entry of a list that stands for
// more than one.  Everything else about a pack is already the tier's: the run
// is a type-table entry so that two namings of one argument list are one
// specialization, and each element of it is bound to the pack's name exactly as
// a single argument is bound to an ordinary place - so a pattern read against
// that binding is read by the machinery that was already there.

namespace
{

// 2.11p1: the identifiers a spelling writes, which is what a pattern names its
// packs by.  Everything else in the spelling - operators, literals, the `::`
// between two components - names nothing a region declares.
bool identifier_start(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool identifier_char(char c)
{
	return identifier_start(c) || (c >= '0' && c <= '9');
}

}

bool written_pack_expansion(const std::string& spelling, std::string& pattern)
{
	if (spelling.size() < 4 ||
	    spelling.compare(spelling.size() - 3, 3, "...") != 0)
	{
		return false;
	}
	std::string::size_type end = spelling.size() - 3;
	while (end > 0 && spelling[end - 1] == ' ')
	{
		--end;
	}
	if (end == 0)
	{
		return false;
	}
	pattern = spelling.substr(0, end);
	return true;
}

TypeId bound_run(TypeTable& types, const std::vector<TypeId>& arguments,
                 std::size_t from)
{
	const std::vector<TypeId> run(
		arguments.begin() + (arguments.size() < from ? arguments.size() : from),
		arguments.end());
	if (run.size() == 1 && types.is_pack_expansion(run[0]))
	{
		return run[0];
	}
	return types.pack_type(run);
}

std::size_t function_pack_place(const TypeTable& types,
                                const std::vector<SemaEntity*>& parameters)
{
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		if (types.is_template_pack(parameters[index]->type))
		{
			return index;
		}
	}
	return parameters.size();
}

std::string pack_element_name(const std::string& name, std::size_t index)
{
	if (index == 0 || name.empty())
	{
		return name;
	}
	std::string digits;
	std::size_t rest = index + 1;
	while (rest != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
		rest /= 10;
	}
	return name + "__pack" + digits;
}

PackReading::PackReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

void PackReading::packs_in(TypeId pattern, std::vector<TypeId>& runs,
                           std::vector<TypeId>& places) const
{
	TypeTable& types = analyzer_.types_;
	if (pattern == kNoType)
	{
		return;
	}
	switch (types.kind(pattern))
	{
	case TypeKind::TemplateParameter:
	{
		if (!types.is_template_pack(pattern))
		{
			return;
		}
		const TypeId place = types.strip_cv(pattern);
		for (std::size_t index = 0; index < places.size(); ++index)
		{
			if (places[index] == place)
			{
				return;
			}
		}
		places.push_back(place);
		return;
	}

	case TypeKind::Pack:
		if (types.is_pack_expansion(pattern))
		{
			packs_in(types.target(pattern), runs, places);
			return;
		}
		for (std::size_t index = 0; index < runs.size(); ++index)
		{
			if (runs[index] == pattern)
			{
				return;
			}
		}
		runs.push_back(pattern);
		return;

	case TypeKind::Class:
	{
		// 14.6.2p1: a specialization is written over its arguments, so a pack
		// it was named with is one this pattern names.
		const std::vector<TypeId>& arguments = types.template_arguments(pattern);
		for (std::size_t index = 0; index < arguments.size(); ++index)
		{
			packs_in(arguments[index], runs, places);
		}
		return;
	}

	case TypeKind::Function:
	{
		const std::vector<TypeId>& given = types.parameters(pattern);
		for (std::size_t index = 0; index < given.size(); ++index)
		{
			packs_in(given[index], runs, places);
		}
		packs_in(types.target(pattern), runs, places);
		return;
	}

	case TypeKind::MemberPointer:
		packs_in(types.member_class(pattern), runs, places);
		packs_in(types.target(pattern), runs, places);
		return;

	case TypeKind::Pointer:
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
	case TypeKind::Array:
	case TypeKind::Value:
		packs_in(types.target(pattern), runs, places);
		return;

	default:
		return;
	}
}

bool PackReading::expand_type(TypeId pattern, std::vector<TypeId>& out)
{
	TypeTable& types = analyzer_.types_;
	std::vector<TypeId> runs;
	std::vector<TypeId> places;
	packs_in(pattern, runs, places);
	if (runs.empty() && places.empty())
	{
		return false;
	}
	if (!places.empty())
	{
		// 14.6.2p1: the expansion stands until an argument list settles the run.
		out.push_back(types.pack_expansion(pattern));
		return true;
	}
	const std::size_t length = types.pack_elements(runs[0]).size();
	for (std::size_t index = 1; index < runs.size(); ++index)
	{
		if (types.pack_elements(runs[index]).size() != length)
		{
			// 14.5.3p6: two packs expanded together shall be the same length.
			throw std::runtime_error("a pack expansion is written over two "
			                         "parameter packs of different lengths");
		}
	}
	for (std::size_t element = 0; element < length; ++element)
	{
		std::unordered_map<TypeId, TypeId> bindings;
		for (std::size_t index = 0; index < runs.size(); ++index)
		{
			bindings.insert(std::make_pair(
				runs[index], types.pack_elements(runs[index])[element]));
		}
		std::unordered_map<TypeId, TypeId> memo;
		out.push_back(types.substitute(pattern, bindings, memo));
	}
	return true;
}

void PackReading::substitute_entry(
	TypeId written, const std::unordered_map<TypeId, TypeId>& bindings,
	std::unordered_map<TypeId, TypeId>& memo, std::vector<TypeId>& out)
{
	if (!analyzer_.types_.is_pack_expansion(written))
	{
		out.push_back(analyzer_.substituted(written, bindings, memo));
		return;
	}
	// 14.5.3p4: the pattern is substituted first - which is what puts the run
	// where the pack stood - and the entry is then the elements of that run.
	const TypeId pattern =
		analyzer_.substituted(analyzer_.types_.target(written), bindings, memo);
	if (!expand_type(pattern, out))
	{
		out.push_back(pattern);
	}
}

void PackReading::note_name(const std::string& name, const SemaContext& ctx,
                            Run& run) const
{
	SemaEntity* const found =
		analyzer_.model_.lookup(*ctx.scope, name, LookupKind::Any);
	if (found == nullptr)
	{
		return;
	}
	if (analyzer_.types_.is_template_pack(found->type) ||
	    analyzer_.types_.is_pack_expansion(found->type))
	{
		// 14.6.2p1: the pattern names a place of the head being read, so how
		// long the run is only an argument list says.
		run.found = true;
		run.settled = false;
		return;
	}
	// 14.5.3p1: a pack the reading has settled is either a run of arguments or
	// 8.3.5p10's places one expansion of a function parameter pack declared.
	const bool declared = found->pack_run != 0;
	if (!declared && !analyzer_.types_.is_pack(found->type))
	{
		return;
	}
	const std::size_t held =
		declared ? found->pack_run
		         : analyzer_.types_.pack_elements(found->type).size();
	if (run.found && run.settled && held != run.length)
	{
		// 14.5.3p6: two packs expanded together shall have the same length.
		throw std::runtime_error("a pack expansion names two parameter packs "
		                         "of different lengths");
	}
	run.found = true;
	run.length = held;
	run.packs.push_back(found);
}

PackReading::Run PackReading::run_of(const std::string& pattern,
                                     const SemaContext& ctx) const
{
	Run run;
	std::string::size_type at = 0;
	while (at < pattern.size())
	{
		if (!identifier_start(pattern[at]))
		{
			++at;
			continue;
		}
		const std::string::size_type start = at;
		while (at < pattern.size() && identifier_char(pattern[at]))
		{
			++at;
		}
		note_name(pattern.substr(start, at - start), ctx, run);
	}
	return run;
}

namespace
{

// The names a tree wrote, which for an expression is every spelling its nodes
// carry - an id-expression, the callee of a call, a template-id's own text.
void names_in(const AstNode& node, std::vector<std::string>& out)
{
	std::string::size_type at = 0;
	while (at < node.text.size())
	{
		if (!identifier_start(node.text[at]))
		{
			++at;
			continue;
		}
		const std::string::size_type start = at;
		while (at < node.text.size() && identifier_char(node.text[at]))
		{
			++at;
		}
		out.push_back(node.text.substr(start, at - start));
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		names_in(*node.children[index], out);
	}
}

}

PackReading::Run PackReading::run_of_node(const AstNode& node,
                                          const SemaContext& ctx) const
{
	std::vector<std::string> names;
	names_in(node, names);
	Run run;
	for (std::size_t index = 0; index < names.size(); ++index)
	{
		note_name(names[index], ctx, run);
	}
	return run;
}

Scope& PackReading::element_region(const Run& run, std::size_t element,
                                   const SemaContext& ctx)
{
	Scope& region = analyzer_.model_.open(ScopeKind::TemplateParameters,
	                                      *ctx.scope, nullptr, ctx.dump);
	for (std::size_t which = 0; which < run.packs.size(); ++which)
	{
		SemaEntity& pack = *run.packs[which];
		if (pack.pack_run == 0)
		{
			analyzer_.bind_argument(
				region, pack.name,
				analyzer_.types_.pack_elements(pack.type)[element],
				SemaKind::Typedef);
			continue;
		}
		// 8.3.5p10: the places the expansion declared are named after the pack,
		// so the element is the declaration that name reaches - the pack's own
		// name is bound to it for as long as this reading of the pattern
		// stands.
		SemaEntity* const place = analyzer_.model_.lookup(
			*ctx.scope, pack_element_name(pack.name, element), LookupKind::Any);
		if (place == nullptr)
		{
			throw std::runtime_error(pack.name + " is expanded and the place "
			                         "its run declared is not in scope");
		}
		analyzer_.model_.bind(region, pack.name, *place);
	}
	return region;
}

void PackReading::expand(const std::string& pattern, const SemaContext& ctx,
                         TypeId place, std::vector<TypeId>& out)
{
	const Run run = run_of(pattern, ctx);
	if (!run.found)
	{
		// 14.5.3p5: the pattern of a pack expansion shall name at least one
		// parameter pack.
		throw std::runtime_error(pattern + " is expanded and names no "
		                         "parameter pack");
	}
	if (!run.settled)
	{
		// 14.6.2p1: the expansion stands for itself until an argument list
		// says how long it is, which is one entry of the list it was written
		// in and not none.
		out.push_back(analyzer_.types_.pack_expansion(
			place == kNoType
				? analyzer_.template_argument_type(pattern, ctx)
				: analyzer_.template_argument_value(pattern, place, ctx)));
		return;
	}
	for (std::size_t index = 0; index < run.length; ++index)
	{
		// 14.5.3p4: the expansion comes to the pattern read once per element,
		// with each pack it names standing for that element.  The region is
		// this element's alone, so nothing one reading binds is standing for
		// the next.
		Scope& region = analyzer_.model_.open(ScopeKind::TemplateParameters,
		                                      *ctx.scope, nullptr, ctx.dump);
		for (std::size_t which = 0; which < run.packs.size(); ++which)
		{
			const SemaEntity& pack = *run.packs[which];
			analyzer_.bind_argument(
				region, pack.name,
				analyzer_.types_.pack_elements(pack.type)[index],
				SemaKind::Typedef);
		}
		SemaContext inner = ctx;
		inner.scope = &region;
		out.push_back(place == kNoType
			              ? analyzer_.template_argument_type(pattern, inner)
			              : analyzer_.template_argument_value(pattern, place,
			                                                  inner));
	}
}

long long PackReading::length(const std::string& name,
                              const SemaContext& ctx) const
{
	SemaEntity* const found = analyzer_.resolve(name, ctx, LookupKind::Any);
	if (found == nullptr)
	{
		throw std::runtime_error(name + " is written as the operand of "
		                         "sizeof... and names nothing");
	}
	if (analyzer_.types_.is_template_pack(found->type) ||
	    analyzer_.types_.is_pack_expansion(found->type))
	{
		return -1;
	}
	if (found->pack_run != 0)
	{
		// 8.3.5p10: the pack's own name is the first place its expansion
		// declared, and how many the expansion made is a fact of that
		// declaration - so a function parameter pack answers here.
		return static_cast<long long>(found->pack_run);
	}
	if (!analyzer_.types_.is_pack(found->type) ||
	    analyzer_.types_.is_pack_expansion(found->type))
	{
		throw std::runtime_error(name + " is written as the operand of "
		                         "sizeof... and is not a parameter pack");
	}
	return static_cast<long long>(
		analyzer_.types_.pack_elements(found->type).size());
}
