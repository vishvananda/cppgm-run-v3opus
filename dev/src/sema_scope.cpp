#include "sema_scope.h"

#include <ostream>
#include <stdexcept>

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
	, type_entities_(0)
	, visit_(0)
{
	dumps_.push_back(DumpScope());
	root_ = &dumps_.back();
	root_->header = "translation-unit";

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
	return entity;
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
	std::vector<Scope*>& declarers = declarers_[name];
	if (declarers.empty() || declarers.back() != &where)
	{
		declarers.push_back(&where);
	}
	Binding& binding = where.names[name];
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

SemaEntity* SemaModel::search_closure(Scope& in, const std::string& name,
                                      LookupKind filter)
{
	if (in.nominated.empty())
	{
		return find(in, name, filter);
	}

	const std::unordered_map<std::string, std::vector<Scope*> >::const_iterator
		declared = declarers_.find(name);
	if (declared == declarers_.end())
	{
		return nullptr;
	}
	// One declaration of the name in the whole unit cannot be two declarations
	// of it in one declarative region, so the first one reached is the answer.
	const bool unique = declared->second.size() == 1;

	// 7.3.4p2: the declarations of a nominated namespace appear where the
	// directive is, and a directive reached through another counts as well, so
	// the search is the closure of the nomination edges.  Each region carries
	// the stamp of the walk that reached it, so a closure with several paths
	// into one namespace probes it once.
	++visit_;
	search_.clear();
	search_.push_back(&in);
	in.visit = visit_;
	SemaEntity* found = nullptr;
	for (std::size_t index = 0; index < search_.size(); ++index)
	{
		Scope& scope = *search_[index];
		found = merge_found(found, find(scope, name, filter));
		if (found != nullptr && unique)
		{
			return found;
		}
		for (std::size_t edge = 0; edge < scope.nominated.size(); ++edge)
		{
			Scope* next = scope.nominated[edge];
			if (next->visit != visit_)
			{
				next->visit = visit_;
				search_.push_back(next);
			}
		}
	}
	return found;
}

SemaEntity* SemaModel::lookup(Scope& from, const std::string& name,
                              LookupKind filter)
{
	for (Scope* scope = &from; scope != nullptr; scope = scope->parent)
	{
		SemaEntity* found = search_closure(*scope, name, filter);
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
	return search_closure(in, name, filter);
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
