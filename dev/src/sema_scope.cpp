#include "sema_scope.h"

#include <ostream>
#include <stdexcept>
#include <utility>

bool names_a_type(const SemaEntity& entity)
{
	switch (entity.kind)
	{
	case SemaKind::Class:
	case SemaKind::Enum:
	case SemaKind::Typedef:
	case SemaKind::TemplateType:
		return true;

	default:
		return false;
	}
}

bool names_a_space(const SemaEntity& entity)
{
	return entity.kind == SemaKind::Namespace ||
		entity.kind == SemaKind::NamespaceAlias;
}

SemaModel::SemaModel()
	: global_(nullptr)
	, root_(nullptr)
	, unit_(nullptr)
	, type_entities_(0)
	, visit_(0)
{
	dumps_.push_back(DumpScope());
	root_ = &dumps_.back();
	root_->header = "translation-unit";
	nodes_.push_back(DumpNode());
	unit_ = &nodes_.back();
	unit_->text = "translation-unit";

	DumpScope& global_dump = open_dump(*root_, "scope namespace <global>");
	scopes_.emplace_back(ScopeKind::Namespace, nullptr, nullptr, &global_dump, 0);
	global_ = &scopes_.back();
}

Scope& SemaModel::open(ScopeKind kind, Scope& parent, SemaEntity* owner,
                       DumpScope* dump)
{
	scopes_.emplace_back(kind, &parent, owner, dump,
	                     static_cast<std::uint32_t>(scopes_.size()));
	return scopes_.back();
}

DumpScope& SemaModel::open_dump(DumpScope& parent, const std::string& header)
{
	dumps_.push_back(DumpScope());
	DumpScope& scope = dumps_.back();
	scope.header = header;
	parent.children.push_back(&scope);
	return scope;
}

DumpNode& SemaModel::open_node(DumpNode& parent, const std::string& text)
{
	nodes_.push_back(DumpNode());
	DumpNode& node = nodes_.back();
	node.text = text;
	parent.children.push_back(&node);
	return node;
}

DumpNode& SemaModel::wrap_node(DumpNode& node, const std::string& text)
{
	nodes_.push_back(DumpNode());
	DumpNode& held = nodes_.back();
	held.text.swap(node.text);
	held.children.swap(node.children);
	node.text = text;
	node.children.push_back(&held);
	return node;
}

SemaEntity& SemaModel::create(SemaKind kind, const std::string& name, TypeId type)
{
	entities_.push_back(SemaEntity());
	SemaEntity& entity = entities_.back();
	entity.kind = kind;
	entity.name = name;
	entity.type = type;
	entity.scope = nullptr;
	entity.defined = false;
	entity.constant = false;
	entity.value = 0;
	entity.next = nullptr;
	entity.tail = nullptr;
	entity.region = nullptr;
	entity.storage = nullptr;
	entity.constructor = nullptr;
	entity.implicit_object = false;
	entity.id = static_cast<std::uint32_t>(entities_.size() - 1);
	entity.dump_name = name;
	return entity;
}

namespace
{

std::uint64_t overload_key(const SemaEntity& head, std::uint32_t signature)
{
	return (static_cast<std::uint64_t>(head.id) << 32) | signature;
}

}

SemaEntity* SemaModel::overload_of(const SemaEntity& head,
                                   std::uint32_t signature) const
{
	const std::unordered_map<std::uint64_t, SemaEntity*>::const_iterator found =
		overloads_.find(overload_key(head, signature));
	return found == overloads_.end() ? nullptr : found->second;
}

void SemaModel::hold_overload(const SemaEntity& head, std::uint32_t signature,
                              SemaEntity& entity)
{
	overloads_.insert(std::make_pair(overload_key(head, signature), &entity));
}

void SemaModel::own_type(TypeId type, SemaEntity& entity)
{
	type_owners_[type] = &entity;
}

SemaEntity* SemaModel::type_owner(TypeId type) const
{
	const std::unordered_map<TypeId, SemaEntity*>::const_iterator found =
		type_owners_.find(type);
	return found == type_owners_.end() ? nullptr : found->second;
}

Scope* SemaModel::region_of(const SemaEntity& entity) const
{
	if (entity.scope != nullptr)
	{
		return entity.scope;
	}
	if (entity.kind != SemaKind::Typedef)
	{
		return nullptr;
	}
	// 3.4.3p1: a typedef-name written before `::` names the class or
	// enumeration it is an alias for, and that is the region looked into.
	const SemaEntity* owner = type_owner(entity.type);
	return owner == nullptr ? nullptr : owner->scope;
}

SemaEntity* SemaModel::find(const Scope& where, const std::string& name,
                            LookupKind filter) const
{
	const std::unordered_map<std::string, Binding>::const_iterator found =
		where.names.find(name);
	if (found == where.names.end())
	{
		return nullptr;
	}
	const Binding& binding = found->second;
	switch (filter)
	{
	case LookupKind::Type:
		if (binding.ordinary != nullptr && names_a_type(*binding.ordinary))
		{
			return binding.ordinary;
		}
		return binding.tag;

	case LookupKind::Space:
		if (binding.ordinary != nullptr && names_a_space(*binding.ordinary))
		{
			return binding.ordinary;
		}
		return nullptr;

	case LookupKind::Region:
		if (binding.ordinary != nullptr &&
		    (names_a_space(*binding.ordinary) || names_a_type(*binding.ordinary)))
		{
			return binding.ordinary;
		}
		return binding.tag;

	default:
		break;
	}
	return binding.ordinary != nullptr ? binding.ordinary : binding.tag;
}

void SemaModel::bind(Scope& where, const std::string& name, SemaEntity& entity)
{
	const std::pair<std::unordered_map<std::string, Binding>::iterator, bool>
		placed = where.names.insert(std::make_pair(name, Binding()));
	if (placed.second)
	{
		declarers_[name].push_back(&where);
	}
	Binding& binding = placed.first->second;
	const bool is_tag =
		entity.kind == SemaKind::Class || entity.kind == SemaKind::Enum;
	if (is_tag)
	{
		binding.tag = &entity;
		if (binding.ordinary == nullptr)
		{
			binding.ordinary = &entity;
		}
		return;
	}
	// 7.3.1p2 and 3.3.1p4: a namespace name and any other declaration of the
	// same name in one region are two declarations of two things.
	if (binding.ordinary != nullptr && binding.ordinary != &entity &&
	    (names_a_space(*binding.ordinary) != names_a_space(entity)))
	{
		throw std::runtime_error("a declaration of " + name +
		                         " conflicts with the namespace of that name");
	}
	binding.ordinary = &entity;
}

void SemaModel::declare_in(Scope& where, SemaEntity& entity)
{
	where.declarations.push_back(&entity);
	if (entity.region == nullptr)
	{
		// The region the declaration was written in is the first one it is
		// recorded in; 9.5p1 records an anonymous union's member a second time,
		// in the region the union was declared in, and that is a binding rather
		// than another declaration of it.
		entity.region = &where;
	}
}

void SemaModel::nominate(Scope& where, Scope& space)
{
	const std::uint64_t key =
		(static_cast<std::uint64_t>(where.id) << 32) | space.id;
	if (!nominations_.insert(key).second)
	{
		return;
	}
	where.nominated.push_back(&space);
	space.nominated_by.push_back(&where);
	// A gathered set of searchers may have grown, and the namespace the
	// directive named is what says whose.
	nominees_.push_back(&space);
}

SemaEntity* SemaModel::merge_found(SemaEntity* found, SemaEntity* again)
{
	if (found == nullptr || found == again)
	{
		return again;
	}
	if (again == nullptr)
	{
		return found;
	}
	throw std::runtime_error("a name is declared in two of the namespaces a "
	                         "lookup reached");
}

const std::vector<Scope*>* SemaModel::declarers(const std::string& name) const
{
	const std::unordered_map<std::string, std::vector<Scope*> >::const_iterator
		found = declarers_.find(name);
	return found == declarers_.end() ? nullptr : &found->second;
}

// 7.3.4p2: the declarations of a nominated namespace appear where the directive
// is, and a directive reached through another counts as well, so the regions a
// declaration of `declaring` can be looked up from are `declaring` and the
// closure of the directives that reach it.
//
// The set is gathered once and then kept up with the directives: a walk that has
// already followed a region's directives follows only the ones written since, so
// a unit that writes one directive between each pair of lookups costs each
// lookup the one edge it added rather than the whole set again.
void SemaModel::gather_searchers(Scope& declaring)
{
	if (declaring.searchers_at == nominees_.size())
	{
		return;
	}
	pending_.clear();
	if (declaring.searchers.size() <=
	    nominees_.size() - static_cast<std::size_t>(declaring.searchers_at))
	{
		// More directives have been written than this set holds regions, so
		// walking it again costs less than asking of each directive whether it
		// touched one.  Either way a gathering costs no more than deriving the
		// answer from nothing.
		declaring.searchers.clear();
		declaring.expanded.clear();
		declaring.searcher_at.clear();
	}
	if (declaring.searchers.empty())
	{
		declaring.searchers.push_back(&declaring);
		declaring.expanded.push_back(0);
		declaring.searcher_at.insert(std::make_pair(&declaring, 0u));
		pending_.push_back(0);
	}
	else
	{
		for (std::size_t written = static_cast<std::size_t>(declaring.searchers_at);
		     written < nominees_.size(); ++written)
		{
			const std::unordered_map<Scope*, std::uint32_t>::const_iterator place =
				declaring.searcher_at.find(nominees_[written]);
			if (place != declaring.searcher_at.end())
			{
				pending_.push_back(place->second);
			}
		}
	}
	declaring.searchers_at = nominees_.size();

	for (std::size_t index = 0; index < pending_.size(); ++index)
	{
		const std::uint32_t place = pending_[index];
		const Scope& scope = *declaring.searchers[place];
		for (std::size_t edge = declaring.expanded[place];
		     edge < scope.nominated_by.size(); ++edge)
		{
			Scope* next = scope.nominated_by[edge];
			const std::uint32_t added =
				static_cast<std::uint32_t>(declaring.searchers.size());
			if (declaring.searcher_at.insert(std::make_pair(next, added)).second)
			{
				declaring.searchers.push_back(next);
				declaring.expanded.push_back(0);
				pending_.push_back(added);
			}
		}
		declaring.expanded[place] =
			static_cast<std::uint32_t>(scope.nominated_by.size());
	}
}

bool SemaModel::reaches(Scope& in, Scope& declaring)
{
	if (&in == &declaring)
	{
		return true;
	}
	if (declaring.nominated_by.empty())
	{
		return false;
	}
	gather_searchers(declaring);
	return declaring.searcher_at.find(&in) != declaring.searcher_at.end();
}

SemaEntity* SemaModel::lookup_unique(Scope& from, const Scope* stop,
                                     const std::string& name, LookupKind filter,
                                     Scope& declarer)
{
	SemaEntity* found = find(declarer, name, filter);
	if (found == nullptr)
	{
		// One region declares the name and this is what it declared, so no
		// region the search could reach has anything else to say.
		return nullptr;
	}
	// Which enclosing region reaches the declaration does not matter, because
	// one region declares the name and every route to it is a route to the same
	// declaration.  So the cheap question is asked of all of them first: the
	// region that declared it encloses the lookup far more often than a
	// using-directive nominates it.
	for (Scope* scope = &from; scope != stop; scope = scope->parent)
	{
		if (scope == &declarer)
		{
			return found;
		}
	}
	for (Scope* scope = &from; scope != stop; scope = scope->parent)
	{
		if (reaches(*scope, declarer))
		{
			return found;
		}
	}
	return nullptr;
}

bool SemaModel::walk_reached(Scope& in, std::size_t budget)
{
	++visit_;
	reached_.clear();
	reached_.push_back(&in);
	in.visit = visit_;
	for (std::size_t index = 0; index < reached_.size(); ++index)
	{
		const Scope& scope = *reached_[index];
		for (std::size_t edge = 0; edge < scope.nominated.size(); ++edge)
		{
			Scope* next = scope.nominated[edge];
			if (next->visit == visit_)
			{
				continue;
			}
			if (reached_.size() >= budget)
			{
				return false;
			}
			next->visit = visit_;
			reached_.push_back(next);
		}
	}
	return true;
}

SemaEntity* SemaModel::search_declarers(Scope& in, const std::string& name,
                                        LookupKind filter,
                                        const std::vector<Scope*>& regions)
{
	if (in.nominated.empty())
	{
		// No using-directive is written here, so the only declarations that
		// appear here are the ones written here.
		return find(in, name, filter);
	}
	// Several regions declare the name, so 3.4p1 asks which of them the lookup
	// reaches rather than which one it reaches first.  There are two ways to
	// ask, and the cheaper one is whichever set is smaller: the regions this
	// lookup reaches, or the regions that declare the name.  So the walk of the
	// first runs on a budget of the second's size and gives up when it is the
	// larger, which makes a lookup cost the smaller of the two however lopsided
	// they are.
	SemaEntity* found = nullptr;
	if (walk_reached(in, regions.size()))
	{
		for (std::size_t index = 0; index < reached_.size(); ++index)
		{
			found = merge_found(found, find(*reached_[index], name, filter));
		}
		return found;
	}
	for (std::size_t index = 0; index < regions.size(); ++index)
	{
		Scope* region = regions[index];
		if (reaches(in, *region))
		{
			found = merge_found(found, find(*region, name, filter));
		}
	}
	return found;
}

SemaEntity* SemaModel::lookup(Scope& from, const std::string& name,
                              LookupKind filter)
{
	// A name no region declares is answered without touching the region chain,
	// which is what keeps an unqualified lookup that fails from costing one
	// probe per enclosing region.
	const std::vector<Scope*>* regions = declarers(name);
	if (regions == nullptr)
	{
		return nullptr;
	}
	if (regions->size() == 1)
	{
		return lookup_unique(from, nullptr, name, filter, *(*regions)[0]);
	}
	for (Scope* scope = &from; scope != nullptr; scope = scope->parent)
	{
		SemaEntity* found = search_declarers(*scope, name, filter, *regions);
		if (found != nullptr)
		{
			return found;
		}
	}
	return nullptr;
}

SemaEntity* SemaModel::lookup_in(Scope& in, const std::string& name,
                                 LookupKind filter)
{
	const std::vector<Scope*>* regions = declarers(name);
	if (regions == nullptr)
	{
		return nullptr;
	}
	// 3.4.3.2p2: the declarations `in` itself has are the answer, and the
	// namespaces its using-directives nominate are searched only when it has
	// none.  A region that declares the name therefore hides what a directive
	// written in it would otherwise have reached, rather than being ambiguous
	// with it.
	SemaEntity* declared_here = find(in, name, filter);
	if (declared_here != nullptr)
	{
		return declared_here;
	}
	if (regions->size() == 1)
	{
		return lookup_unique(in, in.parent, name, filter, *(*regions)[0]);
	}
	return search_declarers(in, name, filter, *regions);
}

void write_dump(std::ostream& out, const DumpScope& scope, unsigned depth)
{
	const std::string indent(depth * 2, ' ');
	out << indent << scope.header << '\n';
	for (std::size_t index = 0; index < scope.lines.size(); ++index)
	{
		out << indent << "  " << scope.lines[index] << '\n';
	}
	for (std::size_t index = 0; index < scope.children.size(); ++index)
	{
		write_dump(out, *scope.children[index], depth + 1);
	}
}

void write_nodes(std::ostream& out, const DumpNode& node, unsigned depth)
{
	out << std::string(depth * 2, ' ') << node.text << '\n';
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		write_nodes(out, *node.children[index], depth + 1);
	}
}
