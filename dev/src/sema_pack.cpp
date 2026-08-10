#include "sema_pack.h"

#include <stdexcept>

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

PackReading::PackReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

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
		const std::string name = pattern.substr(start, at - start);
		SemaEntity* const found =
			analyzer_.model_.lookup(*ctx.scope, name, LookupKind::Any);
		if (found == nullptr)
		{
			continue;
		}
		if (analyzer_.types_.is_template_pack(found->type) ||
		    analyzer_.types_.is_pack_expansion(found->type))
		{
			// 14.6.2p1: the pattern names a place of the head being read, so
			// how long the run is only an argument list says.
			run.found = true;
			run.settled = false;
			continue;
		}
		if (!analyzer_.types_.is_pack(found->type) ||
		    analyzer_.types_.is_pack_expansion(found->type))
		{
			continue;
		}
		const std::size_t held =
			analyzer_.types_.pack_elements(found->type).size();
		if (run.found && run.settled && held != run.length)
		{
			// 14.5.3p6: two packs expanded together shall have the same length.
			throw std::runtime_error("a pack expansion names two parameter "
			                         "packs of different lengths");
		}
		run.found = true;
		run.length = held;
		run.packs.push_back(found);
	}
	return run;
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
	SemaEntity* const found =
		analyzer_.model_.lookup(*ctx.scope, name, LookupKind::Any);
	if (found == nullptr)
	{
		throw std::runtime_error(name + " is written as the operand of "
		                         "sizeof... and names nothing");
	}
	if (analyzer_.types_.is_template_pack(found->type))
	{
		return -1;
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
