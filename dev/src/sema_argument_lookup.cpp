#include "sema_argument_lookup.h"

#include <unordered_set>

#include "sema_analyzer.h"

// 3.4.2: which declarations a use of a name reaches through its arguments.
//
// 11.3p6 puts a friend declaration in a namespace without binding its name
// there, and 3.4.2p2 is what reaches it: a name written where no declaration of
// it is in scope still names the functions the types of its arguments carry
// with them.  The walk over those types is the whole of this file, and the
// three places it is asked from - 13.3's call, 13.3.1.2p3's operator and a fold
// of a constant expression - each ask it once and rank the one set that comes
// out.

ArgumentLookup::ArgumentLookup(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

// 3.4.2p2: the namespaces and classes one argument type is associated with.
// A class is associated with itself, its base classes, the class it is a member
// of, and the innermost enclosing namespace of each of them; an enumeration
// with the class or namespace that declares it; a pointer, an array, a
// reference or a function with the types it is written over.  The walk follows
// the type rather than searching, so it costs the depth of the type and the
// depth of the base chain, both of which the source wrote.
void ArgumentLookup::associate_type(TypeId type, AssociatedRegions& out)
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	switch (analyzer_.types_.kind(bare))
	{
	case TypeKind::Pointer:
	case TypeKind::Array:
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		associate_type(analyzer_.types_.target(bare), out);
		return;

	case TypeKind::Function:
	{
		associate_type(analyzer_.types_.target(bare), out);
		const std::vector<TypeId>& parameters = analyzer_.types_.parameters(bare);
		for (std::size_t index = 0; index < parameters.size(); ++index)
		{
			associate_type(parameters[index], out);
		}
		return;
	}

	case TypeKind::MemberPointer:
		associate_type(analyzer_.types_.member_class(bare), out);
		associate_type(analyzer_.types_.target(bare), out);
		return;

	case TypeKind::TemplateName:
	{
		// 3.4.2p2: an argument at a template place associates the namespace or
		// class that declares the template it named.  A template is no type, so
		// that region is the whole of what it associates - there are no base
		// classes and no arguments of its own to walk.
		SemaEntity* const templated = analyzer_.model_.type_owner(bare);
		if (templated != nullptr)
		{
			associate_region(templated->region, out);
		}
		return;
	}

	default:
		break;
	}
	SemaEntity* const owner = analyzer_.model_.type_owner(bare);
	if (owner == nullptr)
	{
		return;
	}
	if (owner->kind == SemaKind::Enum)
	{
		// 3.4.2p2: an enumeration associates the region that declares it, which
		// is its class where a class declares it and its namespace otherwise.
		associate_region(owner->region, out);
		return;
	}
	if (owner->kind != SemaKind::Class)
	{
		return;
	}
	// 3.4.2p2: a class template specialization also associates whatever the
	// types its template arguments named associate.  The walk is over the
	// arguments the specialization recorded rather than over its spelling, and
	// `walked` below stops a second visit to one class - so a nest whose
	// arguments repeat costs each distinct specialization once.
	if (analyzer_.types_.is_specialization(bare) && out.arguments.insert(bare).second)
	{
		const std::vector<TypeId>& given = analyzer_.types_.template_arguments(bare);
		for (std::size_t index = 0; index < given.size(); ++index)
		{
			associate_type(given[index], out);
		}
	}
	// 10p1: the base classes of the class are associated with it, and so are
	// their own, which the chain the base-clauses left is one walk of.  What
	// stops the walk is having walked this class's chain before, not having the
	// class in the set: 3.4.2p2 also associates the class a nested type is a
	// member of, and puts that class in without its bases - so a set that holds
	// it says nothing about them.
	associate_bases(owner, out);
}

// 10p1: the class and the classes it derives from, each associated once.  What
// stops the walk is having walked this class before, not having it in the set:
// 3.4.2p2 also associates the class a nested type is a member of, and puts that
// class in without its bases - so a set that holds it says nothing about them.
void ArgumentLookup::associate_bases(SemaEntity* owner, AssociatedRegions& out)
{
	if (owner == nullptr || owner->scope == nullptr ||
	    !out.walked.insert(owner).second)
	{
		return;
	}
	if (out.held.insert(owner->scope).second)
	{
		out.classes.push_back(owner->scope);
	}
	associate_region(owner->region, out);
	for (std::size_t index = 0; index < owner->bases.size(); ++index)
	{
		associate_bases(owner->bases[index].entity, out);
	}
}

// 3.4.2p2: the region a class or enumeration is a member of, which associates
// that class where a class declares it and, either way, the innermost
// enclosing namespace.
void ArgumentLookup::associate_region(Scope* region, AssociatedRegions& out)
{
	// 3.4.2p2 associates the class the type is a member of, and not the classes
	// that class is in turn a member of - so only the innermost is taken.  The
	// walk still climbs past them, because what it is looking for beyond that
	// is the one namespace they all stand in.
	bool member_class = true;
	for (Scope* at = region; at != nullptr; at = at->parent)
	{
		if (at->kind == ScopeKind::Class)
		{
			if (member_class && out.held.insert(at).second)
			{
				out.classes.push_back(at);
			}
			member_class = false;
			continue;
		}
		if (at->kind != ScopeKind::Namespace)
		{
			continue;
		}
		if (out.held.insert(at).second)
		{
			out.spaces.push_back(at);
		}
		return;
	}
}

// 3.4.2p2: the declarations of `name` that the types of `arguments` reach.  The
// namespaces associated with them are searched for a declaration bound there -
// 3.4.2p4 leaves the using-directives written in them out, and the search does
// not climb the regions around them - and each associated class contributes the
// friend declarations 11.3p6 gave it, which no region binds.  Returns how many
// of the entries appended are such single declarations, which stand at the end.
std::size_t ArgumentLookup::candidates(
	const std::string& name, const std::vector<AnalyzedValue>& arguments,
	std::vector<SemaEntity*>& out)
{
	// 13.3p1 puts each declaration in the set once, and a class may declare as
	// many friends of one name as the program writes - so which declarations
	// the set already holds is a probe rather than a scan of it, and gathering
	// one call's candidates costs what the declarations there are and not their
	// square.
	std::unordered_set<SemaEntity*> gathered(out.begin(), out.end());
	AssociatedRegions associated;
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (arguments[index].type != kNoType)
		{
			associate_type(arguments[index].spelled != kNoType
			               ? arguments[index].spelled
			               : arguments[index].type, associated);
		}
	}
	const std::vector<Scope*>& spaces = associated.spaces;
	const std::vector<Scope*>& classes = associated.classes;
	for (std::size_t index = 0; index < spaces.size(); ++index)
	{
		SemaEntity* const head = analyzer_.model_.find(*spaces[index], name,
		                                     LookupKind::Any);
		if (head != nullptr && head->kind == SemaKind::Function &&
		    gathered.insert(head).second)
		{
			out.push_back(head);
		}
	}
	std::size_t singles = 0;
	for (std::size_t index = 0; index < classes.size(); ++index)
	{
		const std::vector<SemaEntity*>& friends =
			classes[index]->friend_functions;
		for (std::size_t at = 0; at < friends.size(); ++at)
		{
			if (friends[at]->name == name &&
			    gathered.insert(friends[at]).second)
			{
				out.push_back(friends[at]);
				++singles;
			}
		}
	}
	return singles;
}

// 3.4.2p2 with 14.8.1p2: the declarations a *call* reaches through its
// arguments, for a callee written as a template-id.
//
// 14.2 writes the argument list inside the name, so a search made with the
// spelling asks the associated namespaces for a name no declaration of them
// has - `make<tag>` where each declares `make`.  The name the template-id names
// is what 3.4.2 searches for, and what comes back is a template rather than a
// declaration a call can rank: 14.8.1p2's written list has still to be read
// against it, once per declaration of its chain, exactly as the ordinary
// lookup's own declarations already had it read against them.  Every entry this
// appends is a specialization of its own, so all of them stand alone.
std::size_t ArgumentLookup::call_candidates(
	const std::string& called, const std::vector<AnalyzedValue>& arguments,
	const SemaContext& ctx, std::vector<SemaEntity*>& out)
{
	const TemplateId id(QualifiedName(called).last());
	if (!id.valid())
	{
		return candidates(called, arguments, out);
	}
	// 13.3p1 puts each declaration in the set once, and the ordinary lookup
	// reached its declarations through a chain the search may reach the head
	// of - so what is already read is asked of the declaration each entry was
	// made from rather than of the entry, which is a specialization no chain
	// holds.
	std::unordered_set<const SemaEntity*> read;
	for (std::size_t index = 0; index < out.size(); ++index)
	{
		const SemaEntity* const from = out[index]->primary != nullptr
			? out[index]->primary
			: out[index];
		for (const SemaEntity* at = from; at != nullptr; at = at->next)
		{
			read.insert(at);
		}
	}
	std::vector<SemaEntity*> heads;
	candidates(id.name(), arguments, heads);
	const std::size_t before = out.size();
	std::string refused;
	for (std::size_t index = 0; index < heads.size(); ++index)
	{
		if (read.count(heads[index]) == 0)
		{
			analyzer_.explicit_specializations(*heads[index], id, ctx, out,
			                                   refused);
		}
	}
	return out.size() - before;
}

// 3.4.2p3: the lookup that named the callee suppresses the argument-dependent
// one when what it found is a member of a class, a declaration written in a
// block, or anything that is not a function.
bool ArgumentLookup::allowed(const SemaEntity* named) const
{
	if (named == nullptr)
	{
		return true;
	}
	if (named->kind != SemaKind::Function || named->region == nullptr)
	{
		return false;
	}
	return named->region->kind == ScopeKind::Namespace;
}

