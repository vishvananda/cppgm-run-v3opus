#include "sema_scope.h"

#include <ostream>
#include <stdexcept>
#include <utility>

// 3.3p1: one declarative region.  3.5p4's internal linkage is a fact the region
// is opened with rather than one a walk outwards answers, so an unnamed
// namespace hands it to every region opened inside it here.
Scope::Scope(ScopeKind scope_kind, Scope* enclosing, SemaEntity* scope_owner,
             DumpScope* scope_dump, std::uint32_t scope_id)
	: kind(scope_kind)
	, parent(enclosing)
	, owner(scope_owner)
	, dump(scope_dump)
	, id(scope_id)
	, unnamed_region(enclosing != nullptr && enclosing->unnamed_region)
	, visit(0)
	, base(nullptr)
	, inheriting_constructors(false)
	, searchers_at(0)
{}

bool declares_subobject(const SemaEntity& member, const Scope& scope)
{
	return member.kind == SemaKind::Variable && member.object_member &&
		member.region == &scope && member.shadowed == nullptr;
}

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

const std::string& abi_qualified_name(const SemaEntity& entity)
{
	return entity.abi_name.empty() ? entity.dump_name : entity.abi_name;
}

void name_in_region(SemaEntity& entity, const Scope& scope,
                    const std::string& name)
{
	const bool named = scope.kind == ScopeKind::Namespace ||
		scope.kind == ScopeKind::Class;
	entity.dump_name = named ? scope.prefix + name : name;
	entity.abi_name = named && !scope.abi_prefix.empty()
		? scope.abi_prefix + name
		: std::string();
	// 3.5p4: a name 7.3.1.1p1's unnamed namespace declares has internal
	// linkage, and so has a member of a class it declares - no declaration of
	// either writes a specifier, so the region is the only thing that says so.
	entity.internal_linkage = entity.internal_linkage || scope.unnamed_region;
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
	// What the wrapped line stood for goes with what it said: the node that
	// stays in place is the conversion written around it, and the facts of the
	// operand belong to the one that now holds them.
	held.fact = node.fact;
	node.fact = SemaFact();
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
	entity.object_definition = false;
	entity.c_linkage = false;
	entity.internal_linkage = false;
	entity.thread_storage = false;
	entity.builtin = kNotBuiltin;
	entity.nonthrowing = false;
	entity.wrote_exception_specification = false;
	entity.promotion = kNoType;
	entity.value = 0;
	entity.next = nullptr;
	entity.tail = nullptr;
	entity.region = nullptr;
	entity.storage = nullptr;
	entity.constructor = nullptr;
	entity.destructor = nullptr;
	entity.member_constructor = nullptr;
	entity.member_entry = false;
	entity.base = nullptr;
	entity.base_access = kPublicAccess;
	entity.empty_class = true;
	entity.special = kOrdinaryFunction;
	entity.transfer = kNotTransfer;
	for (unsigned index = 0; index < kTransferKinds; ++index)
	{
		entity.transfers[index] = nullptr;
	}
	entity.explicit_function = false;
	entity.conversion_function = false;
	entity.conversions.clear();
	entity.conversions_above = nullptr;
	entity.complete_object_entry = false;
	entity.base_object_entry = false;
	entity.shadowed = nullptr;
	entity.inherited = nullptr;
	entity.delegates_to = nullptr;
	entity.friend_definition = false;
	entity.user_provided = false;
	entity.deleted = false;
	entity.defaulted = false;
	entity.access = kPublicAccess;
	entity.default_initializer = false;
	entity.aggregate = false;
	entity.offset = 0;
	entity.requested_align = 0;
	entity.bit_field = false;
	entity.bit_width = 0;
	entity.bit_offset = 0;
	entity.bit_access = kNoType;
	entity.inline_function = false;
	entity.trivial = false;
	entity.empty_body = false;
	entity.object_member = false;
	entity.mutable_member = false;
	entity.template_parameters = nullptr;
	entity.primary = nullptr;
	entity.instantiated = false;
	entity.id = static_cast<std::uint32_t>(entities_.size() - 1);
	entity.dump_name = name;
	return entity;
}

namespace
{

// One entity and one interned list of types, which is what tells two
// declarations of one name apart in 13.1 and two specializations of one
// template apart in 14.7.1.
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

void SemaModel::drop_overload(const SemaEntity& head, std::uint32_t signature)
{
	overloads_.erase(overload_key(head, signature));
}

SemaEntity* SemaModel::specialization_of(const SemaEntity& primary,
                                         std::uint32_t arguments) const
{
	const std::unordered_map<std::uint64_t, SemaEntity*>::const_iterator found =
		specializations_.find(overload_key(primary, arguments));
	return found == specializations_.end() ? nullptr : found->second;
}

void SemaModel::hold_specialization(const SemaEntity& primary,
                                    std::uint32_t arguments, SemaEntity& entity)
{
	specializations_.insert(
		std::make_pair(overload_key(primary, arguments), &entity));
}

void SemaModel::befriend(const SemaEntity& granting, const SemaEntity& friendly)
{
	friendships_.insert((static_cast<std::uint64_t>(granting.id) << 32) |
	                    friendly.id);
}

bool SemaModel::befriended(const SemaEntity& granting,
                           const SemaEntity& friendly) const
{
	return friendships_.find((static_cast<std::uint64_t>(granting.id) << 32) |
	                         friendly.id) != friendships_.end();
}

Scope& declaring_region(Scope& scope)
{
	Scope* where = &scope;
	// A template-declaration may parameterise another one, so the regions its
	// parameters are declared in nest and the walk out of them is a loop.
	while (where->kind == ScopeKind::TemplateParameters &&
	       where->parent != nullptr)
	{
		where = where->parent;
	}
	return *where;
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

std::vector<SemaEntity*>& SemaModel::open_overloads()
{
	overload_sets_.push_back(std::vector<SemaEntity*>());
	return overload_sets_.back();
}

SemaEntity* SemaModel::merge_found(SemaEntity* found, SemaEntity* again,
                                   std::vector<SemaEntity*>* set)
{
	if (again == nullptr || again == found)
	{
		return found;
	}
	if (found == nullptr)
	{
		if (set != nullptr)
		{
			set->push_back(again);
		}
		return again;
	}
	// 3.4p2 and 7.3.4p3: a lookup that reached two regions found two
	// declarations of the name, which is ill formed unless both of them are
	// functions - and then what it found is the two chains, which 13.1 makes one
	// set of overloaded functions for the use of the name to choose from.
	if (set != nullptr && found->kind == SemaKind::Function &&
	    again->kind == SemaKind::Function)
	{
		set->push_back(again);
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
		// 10.2p2: a class also sees what its base classes declare, which is
		// searched after the class itself and before the region around it.  A
		// region with no base-clause leaves the chain empty, so this costs one
		// probe per enclosing region in a program with no inheritance.
		for (Scope* at = scope->base; at != nullptr; at = at->base)
		{
			if (at == &declarer)
			{
				return found;
			}
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
                                        const std::vector<Scope*>& regions,
                                        std::vector<SemaEntity*>* found_set)
{
	if (in.nominated.empty())
	{
		// No using-directive is written here, so the only declarations that
		// appear here are the ones written here.
		return merge_found(nullptr, find(in, name, filter), found_set);
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
			found = merge_found(found, find(*reached_[index], name, filter),
			                    found_set);
		}
		return found;
	}
	for (std::size_t index = 0; index < regions.size(); ++index)
	{
		Scope* region = regions[index];
		if (reaches(in, *region))
		{
			found = merge_found(found, find(*region, name, filter), found_set);
		}
	}
	return found;
}

SemaEntity* SemaModel::lookup(Scope& from, const std::string& name,
                              LookupKind filter,
                              std::vector<SemaEntity*>* found_set)
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
		return merge_found(nullptr,
		                   lookup_unique(from, nullptr, name, filter,
		                                 *(*regions)[0]),
		                   found_set);
	}
	for (Scope* scope = &from; scope != nullptr; scope = scope->parent)
	{
		SemaEntity* found = search_declarers(*scope, name, filter, *regions,
		                                    found_set);
		if (found != nullptr)
		{
			return found;
		}
		// 10.2p2 and 3.4.1p8: what a base class declares is found from a member
		// of the derived class, and hides what the region around the class
		// declares rather than being hidden by it.
		for (Scope* at = scope->base; at != nullptr; at = at->base)
		{
			found = search_declarers(*at, name, filter, *regions, found_set);
			if (found != nullptr)
			{
				return found;
			}
		}
	}
	return nullptr;
}

SemaEntity* SemaModel::lookup_in(Scope& in, const std::string& name,
                                 LookupKind filter,
                                 std::vector<SemaEntity*>* found_set)
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
		return merge_found(nullptr, declared_here, found_set);
	}
	// 10.2p2 and 10.2p6: a name the class itself does not declare is looked for
	// in the classes it derives from, nearest first, and what a class declares
	// hides what its bases do - so the first base that declares it is the one
	// the name denotes.
	for (Scope* at = in.base; at != nullptr; at = at->base)
	{
		SemaEntity* const inherited = find(*at, name, filter);
		if (inherited != nullptr)
		{
			return merge_found(nullptr, inherited, found_set);
		}
	}
	if (regions->size() == 1)
	{
		return merge_found(nullptr,
		                   lookup_unique(in, in.parent, name, filter,
		                                 *(*regions)[0]),
		                   found_set);
	}
	return search_declarers(in, name, filter, *regions, found_set);
}

namespace
{

// The indent, carried down the walk rather than built per line.
//
// The dump writes one line per node and the depth changes by two spaces at a
// time, so one string that grows and shrinks with the descent costs each line
// the two characters it added.  Building it from the depth instead costs each
// line the whole indent again, which for a tree as deep as it is wide is the
// output written twice.
void write_dump_at(std::ostream& out, const DumpScope& scope, std::string& indent)
{
	out << indent << scope.header << '\n';
	indent.append(2, ' ');
	for (std::size_t index = 0; index < scope.lines.size(); ++index)
	{
		out << indent << scope.lines[index] << '\n';
	}
	for (std::size_t index = 0; index < scope.children.size(); ++index)
	{
		write_dump_at(out, *scope.children[index], indent);
	}
	indent.resize(indent.size() - 2);
}

void write_nodes_at(std::ostream& out, const DumpNode& node, std::string& indent)
{
	out << indent << node.text << '\n';
	indent.append(2, ' ');
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		write_nodes_at(out, *node.children[index], indent);
	}
	indent.resize(indent.size() - 2);
}

}

void write_dump(std::ostream& out, const DumpScope& scope, unsigned depth)
{
	std::string indent(depth * 2, ' ');
	write_dump_at(out, scope, indent);
}

void write_nodes(std::ostream& out, const DumpNode& node, unsigned depth)
{
	std::string indent(depth * 2, ' ');
	write_nodes_at(out, node, indent);
}
