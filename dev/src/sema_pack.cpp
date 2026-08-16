#include "sema_pack.h"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
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

// A spelling with the spaces 14.2's list writes between its terminals taken
// out, which is what tells a pattern that *is* a name from one that computes
// something out of it.
std::string bare_spelling(const std::string& text)
{
	std::string out;
	for (std::string::size_type at = 0; at < text.size(); ++at)
	{
		if (text[at] != ' ')
		{
			out += text[at];
		}
	}
	return out;
}

bool dots_at(const std::string& text, std::string::size_type at)
{
	return at + 3 <= text.size() && text.compare(at, 3, "...") == 0;
}

std::string::size_type past_spaces(const std::string& text,
                                   std::string::size_type at)
{
	while (at < text.size() && text[at] == ' ')
	{
		++at;
	}
	return at;
}

// The first character of the operand written before the `...` at `at`, which is
// the pattern that inner expansion is over.  A pattern is a postfix-expression
// or a type-id, so it is read backwards the way it was written: a bracketed run
// closes up with the name before it, and a name carries its
// nested-name-specifier with it.
std::string::size_type operand_start(const std::string& text,
                                     std::string::size_type at)
{
	std::string::size_type start = at;
	while (start > 0)
	{
		std::string::size_type back = start;
		while (back > 0 && text[back - 1] == ' ')
		{
			--back;
		}
		if (back == 0)
		{
			break;
		}
		const char last = text[back - 1];
		if (last == ')' || last == ']' || last == '>')
		{
			const char open = last == ')' ? '(' : (last == ']' ? '[' : '<');
			std::size_t depth = 0;
			std::string::size_type scan = back;
			while (scan > 0)
			{
				--scan;
				if (text[scan] == last)
				{
					++depth;
				}
				else if (text[scan] == open && --depth == 0)
				{
					break;
				}
			}
			if (depth != 0)
			{
				// 5.9: a `<` this spelling wrote is an operator rather than a
				// list, so there is no group to leave out and the names before
				// it stand.
				break;
			}
			start = scan;
			continue;
		}
		if (!identifier_char(last))
		{
			break;
		}
		std::string::size_type scan = back;
		while (scan > 0 && identifier_char(text[scan - 1]))
		{
			--scan;
		}
		start = scan;
		while (scan > 0 && text[scan - 1] == ' ')
		{
			--scan;
		}
		if (scan < 2 || text[scan - 1] != ':' || text[scan - 2] != ':')
		{
			break;
		}
		// 3.4.3p1: the components before the `::` are part of the one name this
		// operand is, so they were expanded with it.
		start = scan - 2;
	}
	return start;
}

// The names a spelling wrote that an expansion of it is over - the same
// question `names_in` answers of a tree, asked of the text 14.2 writes an
// argument list in.
//
// 14.5.3p5 leaves a pack named inside the pattern of an inner `...` to that
// expansion and 5.3.3p5 makes `sizeof...` a value rather than a use of the pack
// it counts, so neither says how long this run is.  A tree tells the two by
// node kind; here they are the operand an inner `...` was written after and the
// parenthesized name `sizeof...` was written before.
void spelled_names_in(const std::string& text, std::vector<std::string>& out)
{
	std::vector<std::pair<std::string::size_type, std::string> > found;
	std::string::size_type at = 0;
	while (at < text.size())
	{
		if (identifier_start(text[at]))
		{
			const std::string::size_type start = at;
			while (at < text.size() && identifier_char(text[at]))
			{
				++at;
			}
			const std::string name = text.substr(start, at - start);
			const std::string::size_type after = past_spaces(text, at);
			if (name != "sizeof" || !dots_at(text, after))
			{
				found.push_back(std::make_pair(start, name));
				continue;
			}
			at = past_spaces(text, after + 3);
			while (at < text.size() && text[at] != ')' && text[at] != ',')
			{
				++at;
			}
			continue;
		}
		if (!dots_at(text, at))
		{
			++at;
			continue;
		}
		const std::string::size_type from = operand_start(text, at);
		while (!found.empty() && found[found.size() - 1].first >= from)
		{
			found.pop_back();
		}
		at += 3;
	}
	for (std::size_t index = 0; index < found.size(); ++index)
	{
		out.push_back(found[index].second);
	}
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

std::size_t trailing_pack_place(const TypeTable& types,
                                const std::vector<SemaEntity*>& parameters)
{
	if (!parameters.empty() &&
	    types.is_template_pack(parameters[parameters.size() - 1]->type))
	{
		return parameters.size() - 1;
	}
	return parameters.size();
}

TypeId place_argument(TypeTable& types, const std::vector<TypeId>& arguments,
                      std::size_t index, std::size_t places, bool pack)
{
	if (pack && index + 1 == places)
	{
		// 14.5.3p1: the last place takes every argument the ones before it did
		// not, which is a run of none where the list stopped at it.
		return bound_run(types, arguments, index);
	}
	return index < arguments.size() ? arguments[index] : kNoType;
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

namespace
{

// The packs one pattern is written over, walked once per type it is built from.
//
// A type is a DAG rather than a tree - `pair<T, T>` holds one entry twice - so
// a walk that asked each edge would ask 2^depth questions of a pattern a few
// hundred characters long.  What the walk answers is a fact of the type and not
// of the path that reached it, so a type already asked is not asked again.
void collect_packs(TypeTable& types, TypeId pattern,
                   std::vector<TypeId>& runs, std::vector<TypeId>& places,
                   std::unordered_set<TypeId>& seen)
{
	if (pattern == kNoType || !seen.insert(pattern).second)
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
			collect_packs(types, types.target(pattern), runs, places, seen);
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
			collect_packs(types, arguments[index], runs, places, seen);
		}
		return;
	}

	case TypeKind::Function:
	{
		const std::vector<TypeId>& given = types.parameters(pattern);
		for (std::size_t index = 0; index < given.size(); ++index)
		{
			collect_packs(types, given[index], runs, places, seen);
		}
		collect_packs(types, types.target(pattern), runs, places, seen);
		return;
	}

	case TypeKind::MemberPointer:
		collect_packs(types, types.member_class(pattern), runs, places, seen);
		collect_packs(types, types.target(pattern), runs, places, seen);
		return;

	case TypeKind::Pointer:
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
	case TypeKind::Array:
	case TypeKind::Value:
		collect_packs(types, types.target(pattern), runs, places, seen);
		return;

	default:
		return;
	}
}

}

void PackReading::packs_in(TypeId pattern, std::vector<TypeId>& runs,
                           std::vector<TypeId>& places) const
{
	std::unordered_set<TypeId> seen;
	collect_packs(analyzer_.types_, pattern, runs, places, seen);
}

namespace
{

// 8.3.5p10: the name a parameter-declaration binds, read out of its declarator
// rather than off a type - which is what a run of no elements leaves, because
// there is no element to read the pattern against and still a pack to declare.
std::string declarator_name(const AstNode& declarator)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		if (declarator.children[index]->kind == AstKind::Identifier)
		{
			return declarator.children[index]->text;
		}
	}
	return std::string();
}

}

void PackReading::read_places(const AstNode& declaration,
                              const AstNode& specifiers,
                              const AstNode& declarator, const Run& over,
                              SemaContext& reading, const SemaContext& ctx,
                              bool binds, std::vector<DeclaredParameter>& out)
{
	SemaSpan span;
	span.begin = declaration.begin;
	span.end = declaration.end;
	for (std::size_t element = 0; element < over.length; ++element)
	{
		// The region is this element's alone, exactly as it is for the spelling
		// reading and the tree reading, so what the specifiers name there is
		// the element's own type - `wrap<A>` is `wrap<int>` in the first and
		// `wrap<char>` in the second, each a specialization an instantiation
		// makes rather than one naming the whole run.
		SemaContext one = reading;
		one.scope = &element_region(over, element, reading);
		DeclaredParameter place;
		place.type = analyzer_.specifier_type(
			analyzer_.read_specifiers(specifiers, one, span, true,
			                          std::string()));
		place.type = analyzer_.declarator_type(declarator, place.type, one,
		                                       &place.name);
		// 8.3.5p10: the places are named after the pack and the first of them
		// keeps the pack's own name, so a use of that name reaches the first
		// place and the run is read off its declaration.
		place.name = pack_element_name(place.name, element);
		place.pack_run =
			element == 0 ? static_cast<unsigned>(over.length) : 0u;
		if (!place.name.empty() && binds)
		{
			analyzer_.bind_place(reading, ctx, place);
		}
		out.push_back(place);
	}
	if (over.length != 0)
	{
		return;
	}
	// 14.5.3p4 over a run of none: the clause declared no place at all, and the
	// pack is still declared - so what its name names is the run itself, which
	// `sizeof...` reads as zero and an expansion of comes to no arguments.  It
	// is no place, so no function type holds it and no object is laid out.
	DeclaredParameter none;
	none.name = declarator_name(declarator);
	if (none.name.empty())
	{
		return;
	}
	none.type = analyzer_.types_.pack_type(std::vector<TypeId>());
	if (binds)
	{
		analyzer_.bind_pack(reading, ctx, none);
	}
	out.push_back(none);
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
	TypeTable& types = analyzer_.types_;
	const TypeId over = types.target(written);
	std::vector<TypeId> runs;
	std::vector<TypeId> places;
	packs_in(over, runs, places);
	std::vector<TypeId> bound;
	for (std::size_t index = 0; runs.empty() && index < places.size(); ++index)
	{
		const std::unordered_map<TypeId, TypeId>::const_iterator took =
			bindings.find(places[index]);
		if (took == bindings.end() || !types.is_pack(took->second) ||
		    types.is_pack_expansion(took->second))
		{
			// 14.6.2p1: this substitution settled no run for one of the packs
			// the pattern names, so the expansion stands as the one entry it
			// was written as and a later argument list says how long it is.
			bound.clear();
			break;
		}
		bound.push_back(took->second);
	}
	if (!bound.empty())
	{
		// 14.5.3p4: the pattern is read once per element of the run - not once
		// over a run substituted *into* it, which would leave `wrap<A>...`
		// naming one specialization of `wrap` over the whole run and every
		// element the same type.  Each element is a substitution of its own, so
		// the memo `substituted` carries is this element's alone.
		const std::size_t length = types.pack_elements(bound[0]).size();
		for (std::size_t index = 1; index < bound.size(); ++index)
		{
			if (types.pack_elements(bound[index]).size() != length)
			{
				// 14.5.3p6: two packs expanded together are the same length.
				throw std::runtime_error("a pack expansion is written over two "
				                         "parameter packs of different lengths");
			}
		}
		for (std::size_t element = 0; element < length; ++element)
		{
			std::unordered_map<TypeId, TypeId> one(bindings);
			for (std::size_t index = 0; index < bound.size(); ++index)
			{
				one[places[index]] = types.pack_elements(bound[index])[element];
			}
			std::unordered_map<TypeId, TypeId> reached;
			out.push_back(analyzer_.substituted(over, one, reached));
		}
		return;
	}
	// A pattern the substitution reaches a settled run *inside* is expanded off
	// the type it built, which is the structural rebuild `expand_type` is.
	const TypeId pattern = analyzer_.substituted(over, bindings, memo);
	if (!expand_type(pattern, out))
	{
		out.push_back(pattern);
	}
}

void PackReading::note_name(const std::string& name, const SemaContext& ctx,
                            Run& run) const
{
	SemaEntity* found =
		analyzer_.model_.lookup(*ctx.scope, name, LookupKind::Any);
	if (found == nullptr)
	{
		return;
	}
	if (found->pack_element_of != nullptr)
	{
		// 14.5.3p4: the name stands for one element while a reading of the
		// pattern it was written in stands, and a pattern that names it as a
		// pack *again* - a nested expansion, 5.3.3p5's `sizeof...` - is written
		// over the whole run and not over that element.
		found = found->pack_element_of;
	}
	if (analyzer_.types_.is_template_pack(found->type) ||
	    analyzer_.types_.is_pack_expansion(found->type))
	{
		// 14.6.2p1: the pattern names a place of the head being read, so how
		// long the run is only an argument list says.  Which place it is, is
		// settled: 14.1p4 tells a pack of types from a pack of values, and a
		// list that wrote one where the other belongs is ill-formed however
		// long the run turns out to be.
		run.found = true;
		run.settled = false;
		run.packs.push_back(found);
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
	std::vector<std::string> names;
	spelled_names_in(pattern, names);
	Run run;
	for (std::size_t index = 0; index < names.size(); ++index)
	{
		note_name(names[index], ctx, run);
	}
	return run;
}

namespace
{

// The names a tree wrote, which for an expression is every spelling its nodes
// carry - an id-expression, the callee of a call, a template-id's own text.
//
// 14.5.3p5 and 5.3.3p5: a name already expanded where it stands says nothing
// about how long *this* run is, so the two nodes that expand one are left out
// whole - the pattern of an inner expansion, which is read once per element of
// its own run, and `sizeof...`, which is a value rather than a use of the pack
// it counts.  `sum(get<U>(t...)...)` is one reading per element of `U`, each of
// which reads the whole of `t`, and `f(x, sizeof...(A))...` is a run of `x`
// however long `A` is.
void names_in(const AstNode& node, std::vector<std::string>& out)
{
	if (node.kind == AstKind::PackExpansionExpression ||
	    node.kind == AstKind::SizeofPackExpression)
	{
		return;
	}
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

// 14.5.3p4 in a list the program wrote.  The written entries are walked once:
// until one of them is an expansion nothing is held at all, and from the first
// one on each entry is recorded with the region it is read in - which for an
// expansion is one region per element of the run its packs are bound to.
WrittenList::WrittenList(const AstNode* list, SemaAnalyzer& analyzer,
                         const SemaContext& ctx)
	: written_(list)
	, unsettled_(false)
{
	if (list == nullptr)
	{
		return;
	}
	PackReading packs(analyzer);
	for (std::size_t index = 0; index < list->children.size(); ++index)
	{
		const AstNode& entry = *list->children[index];
		if (entry.kind != AstKind::PackExpansionExpression ||
		    entry.children.empty())
		{
			if (written_ == nullptr)
			{
				entries_.push_back(std::make_pair(&entry, ctx.scope));
			}
			continue;
		}
		if (written_ != nullptr)
		{
			// The entries before this one were written where the list is, and
			// only now is there a list to hold them in.
			written_ = nullptr;
			for (std::size_t before = 0; before < index; ++before)
			{
				entries_.push_back(
					std::make_pair(list->children[before], ctx.scope));
			}
		}
		const AstNode& pattern = *entry.children[0];
		const PackReading::Run run = packs.run_of_node(pattern, ctx);
		if (!run.found)
		{
			// 14.5.3p5: the pattern of a pack expansion shall name at least one
			// parameter pack.
			throw std::runtime_error("an entry of a list is expanded and names "
			                         "no parameter pack");
		}
		if (!run.settled)
		{
			// 14.6.2p1: the expansion stands for itself until an argument list
			// says how long it is, which is one entry of the list and not none.
			unsettled_ = true;
			entries_.push_back(std::make_pair(&entry, ctx.scope));
			continue;
		}
		for (std::size_t element = 0; element < run.length; ++element)
		{
			entries_.push_back(std::make_pair(
				&pattern, &packs.element_region(run, element, ctx)));
		}
	}
}

std::size_t WrittenList::size() const
{
	if (written_ != nullptr)
	{
		return written_->children.size();
	}
	return entries_.size();
}

const AstNode& WrittenList::node(std::size_t index) const
{
	return written_ != nullptr ? *written_->children[index]
	                           : *entries_[index].first;
}

Scope* WrittenList::region(std::size_t index) const
{
	return written_ != nullptr ? nullptr : entries_[index].second;
}

void PackReading::note_node(const AstNode& node, const SemaContext& ctx,
                            Run& run) const
{
	std::vector<std::string> names;
	names_in(node, names);
	for (std::size_t index = 0; index < names.size(); ++index)
	{
		note_name(names[index], ctx, run);
	}
}

PackReading::Run PackReading::run_of_node(const AstNode& node,
                                          const SemaContext& ctx) const
{
	Run run;
	note_node(node, ctx, run);
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
			// 14.5.3p4: the element stands for the pack for as long as this
			// reading does, and the pack itself stands behind it - a pattern
			// that names it as a pack again is read over the whole run.
			analyzer_.bind_argument(
				region, pack.name,
				analyzer_.types_.pack_elements(pack.type)[element],
				SemaKind::Typedef).pack_element_of = &pack;
			continue;
		}
		// 8.3.5p10: the places the expansion declared are named after the pack,
		// so the element is the declaration that name reaches - the pack's own
		// name is bound to it for as long as this reading of the pattern
		// stands.  The first of them is the pack's own declaration, which is
		// asked for rather than looked up: an enclosing reading of a pattern
		// has that very name standing for *its* element.
		SemaEntity* const place =
			element == 0
				? &pack
				: analyzer_.model_.lookup(
					  *ctx.scope, pack_element_name(pack.name, element),
					  LookupKind::Any);
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
		// 14.3.2p1 is asked of the argument each element *is*, which is this
		// pattern only where the pattern is the pack's own name: `f<T...>`
		// writes the elements of the run at the places, and `f<sizeof(T)...>`
		// writes a value 5.3.3p1 makes of each.  So a pattern that computes
		// anything at all is left to the reading below, which answers for it
		// the way it answers for an argument no pack was written into.
		const bool writes_the_run =
			run.packs.size() == 1 && bare_spelling(pattern) == run.packs[0]->name;
		for (std::size_t which = 0;
		     writes_the_run && place != kNoType && which < run.packs.size();
		     ++which)
		{
			const TypeId named = run.packs[which]->type;
			const TypeId stands =
				analyzer_.types_.is_pack_expansion(named)
					? analyzer_.types_.target(named)
					: named;
			if (analyzer_.types_.parameter_value_type(stands) == kNoType)
			{
				// 14.3.2p1: a non-type place takes a value, and a pack of
				// types holds none - so no argument list could settle this one.
				throw std::runtime_error(pattern + " is expanded at a non-type "
				                         "place and names a pack of types");
			}
		}
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
		// the next - and it is the same region the tree reading opens, because
		// a spelling names the two kinds of settled pack a tree does: a run an
		// argument list bound, and 8.3.5p10's places one expansion declared.
		SemaContext inner = ctx;
		inner.scope = &element_region(run, index, ctx);
		out.push_back(place == kNoType
			              ? analyzer_.template_argument_type(pattern, inner)
			              : analyzer_.template_argument_value(pattern, place,
			                                                  inner));
	}
}

long long PackReading::length(const std::string& name,
                              const SemaContext& ctx) const
{
	SemaEntity* found = analyzer_.resolve(name, ctx, LookupKind::Any);
	if (found == nullptr)
	{
		throw std::runtime_error(name + " is written as the operand of "
		                         "sizeof... and names nothing");
	}
	if (found->pack_element_of != nullptr)
	{
		// 5.3.3p5: the operand is the pack, which a reading of a pattern
		// standing over it has left the name standing for one element of.
		found = found->pack_element_of;
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
