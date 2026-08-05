#include "entity_model.h"

#include <ostream>

namespace
{

// True when `outer` is `inner` or encloses it.  The depths make the walk one
// of known length and let a mismatch be rejected before it starts.
bool encloses(const Namespace& outer, const Namespace& inner)
{
	const Namespace* space = &inner;
	while (space != nullptr && space->depth() >= outer.depth())
	{
		if (space == &outer)
		{
			return true;
		}
		space = space->parent();
	}
	return false;
}

bool accepts(LookupFilter filter, const Entity& entity)
{
	switch (filter)
	{
	case LookupFilter::Type:
		return entity.kind == EntityKind::Typedef;
	case LookupFilter::Space:
		return entity.kind == EntityKind::Namespace;
	default:
		return true;
	}
}

}

Namespace::Namespace(NameId name, bool is_inline, Namespace* parent)
	: name_(name)
	, inline_(is_inline)
	, parent_(parent)
	, depth_(parent == nullptr ? 0 : parent->depth() + 1)
	, unnamed_(nullptr)
	, levels_epoch_(0)
	, mark_(0)
{}

Entity* Namespace::find(NameId name) const
{
	const std::unordered_map<NameId, Entity*>::const_iterator entry = bindings_.find(name);
	return entry == bindings_.end() ? nullptr : entry->second;
}

TranslationUnitModel::TranslationUnitModel(TypeTable& types)
	: types_(types)
	, global_(nullptr)
	, epoch_(1)
	, visit_(0)
{
	spaces_.push_back(Namespace(kNoName, false, nullptr));
	global_ = &spaces_.back();
}

Namespace& TranslationUnitModel::create(Namespace& parent, NameId name, bool is_inline)
{
	spaces_.push_back(Namespace(name, is_inline, &parent));
	Namespace& space = spaces_.back();
	parent.members_.push_back(&space);
	if (is_inline)
	{
		nominate(parent, space);
	}
	return space;
}

Entity& TranslationUnitModel::create_entity(EntityKind kind, NameId name, TypeId type,
                                            Namespace* home)
{
	Entity entity;
	entity.kind = kind;
	entity.name = name;
	entity.type = type;
	entity.space = nullptr;
	entity.home = home;
	entities_.push_back(entity);
	return entities_.back();
}

Namespace& TranslationUnitModel::open_namespace(Namespace& parent, NameId name,
                                                bool is_inline)
{
	Entity* bound = parent.find(name);
	// 7.3.1: a namespace-definition extends a namespace that is already a
	// member of the enclosing one.  A name bound here by a using-declaration
	// or an alias names an entity that lives elsewhere, and extending it is
	// not what this definition means.
	if (bound != nullptr && bound->kind == EntityKind::Namespace &&
	    bound->space->parent() == &parent)
	{
		if (is_inline && !bound->space->is_inline())
		{
			throw SemanticError("a namespace first defined without `inline` is "
			                    "extended with it");
		}
		return *bound->space;
	}
	if (bound != nullptr)
	{
		throw SemanticError("a namespace is defined with the name of another entity");
	}

	Namespace& space = create(parent, name, is_inline);
	Entity& entity = create_entity(EntityKind::Namespace, name, kNoType, &parent);
	entity.space = &space;
	parent.bindings_[name] = &entity;
	return space;
}

Namespace& TranslationUnitModel::open_unnamed_namespace(Namespace& parent, bool is_inline)
{
	// 7.3.1.1: every unnamed namespace definition in one namespace defines the
	// same unique member, reached only through the using-directive that comes
	// with it.
	if (parent.unnamed() != nullptr)
	{
		if (is_inline && !parent.unnamed()->is_inline())
		{
			throw SemanticError("an unnamed namespace first defined without "
			                    "`inline` is extended with it");
		}
		return *parent.unnamed();
	}

	// The using-directive 7.3.1.1p1 writes into the enclosing namespace is
	// what makes an unnamed namespace reachable at all; `create` has already
	// written it when the definition was also inline.
	Namespace& space = create(parent, kNoName, is_inline);
	parent.unnamed_ = &space;
	nominate(parent, space);
	return space;
}

void TranslationUnitModel::nominate(Namespace& where, Namespace& space)
{
	for (std::size_t index = 0; index < where.nominated_.size(); ++index)
	{
		if (where.nominated_[index] == &space)
		{
			return;
		}
	}
	where.nominated_.push_back(&space);
	++epoch_;
	forget_levels();
}

void TranslationUnitModel::forget_levels()
{
	for (std::size_t index = 0; index < cached_.size(); ++index)
	{
		std::vector<Namespace*>().swap(cached_[index]->levels_);
		cached_[index]->levels_epoch_ = 0;
	}
	cached_.clear();
}

TypeId TranslationUnitModel::merged(TypeId declared, TypeId again)
{
	if (declared == again)
	{
		return declared;
	}
	// 8.3.4p4 and 3.9p7: a declaration with a bound completes an array that
	// was declared without one, and a later declaration may leave it out.
	if (types_.kind(declared) == TypeKind::Array && types_.kind(again) == TypeKind::Array &&
	    types_.target(declared) == types_.target(again))
	{
		if (!types_.bounded(declared))
		{
			return again;
		}
		if (!types_.bounded(again))
		{
			return declared;
		}
	}
	throw SemanticError("a redeclaration gives a name a different type");
}

Entity& TranslationUnitModel::declare(Namespace& where, EntityKind kind, NameId name,
                                      TypeId type)
{
	Entity* bound = where.find(name);
	if (bound != nullptr)
	{
		redeclare(*bound, kind, type);
		return *bound;
	}

	Entity& entity = create_entity(kind, name, type, &where);
	where.bindings_[name] = &entity;
	if (kind == EntityKind::Variable)
	{
		where.variables_.push_back(&entity);
	}
	else if (kind == EntityKind::Function)
	{
		where.functions_.push_back(&entity);
	}
	return entity;
}

void TranslationUnitModel::redeclare(Entity& entity, EntityKind kind, TypeId type)
{
	if (entity.kind != kind)
	{
		throw SemanticError("a redeclaration declares a different kind of entity");
	}
	entity.type = merged(entity.type, type);
}

void TranslationUnitModel::bind(Namespace& where, NameId name, Entity& entity)
{
	Entity* bound = where.find(name);
	if (bound == &entity)
	{
		return;
	}
	if (bound != nullptr)
	{
		throw SemanticError("a name is bound to two entities in one namespace");
	}
	where.bindings_[name] = &entity;
}

const std::vector<Namespace*>& TranslationUnitModel::levels(Namespace& from)
{
	if (from.levels_epoch_ == epoch_)
	{
		return from.levels_;
	}

	std::vector<std::vector<Namespace*> > anchored(from.depth() + 1);
	++visit_;
	for (Namespace* space = &from; space != nullptr; space = space->parent())
	{
		space->mark_ = visit_;
	}

	// One pass per scope in the chain.  A directive is in scope for the whole
	// chain, so a namespace already reached from an inner scope is not
	// reached again; a namespace reached first from scope `context` has its
	// names appear in the nearest namespace enclosing both it and `context`.
	for (Namespace* context = &from; context != nullptr; context = context->parent())
	{
		reachable_.clear();
		reachable_.push_back(context);
		for (std::size_t index = 0; index < reachable_.size(); ++index)
		{
			const std::vector<Namespace*>& used = reachable_[index]->nominated();
			for (std::size_t which = 0; which < used.size(); ++which)
			{
				Namespace* space = used[which];
				if (space->mark_ == visit_)
				{
					continue;
				}
				space->mark_ = visit_;
				Namespace* anchor = space;
				while (!encloses(*anchor, *context))
				{
					anchor = anchor->parent();
				}
				anchored[from.depth() - anchor->depth()].push_back(space);
				reachable_.push_back(space);
			}
		}
	}

	if (from.levels_epoch_ == 0)
	{
		cached_.push_back(&from);
	}
	from.levels_.clear();
	std::size_t level = 0;
	for (Namespace* context = &from; context != nullptr; context = context->parent())
	{
		from.levels_.push_back(context);
		from.levels_.insert(from.levels_.end(), anchored[level].begin(),
		                    anchored[level].end());
		++level;
	}
	from.levels_epoch_ = epoch_;
	return from.levels_;
}

Entity* TranslationUnitModel::lookup_unqualified(Namespace& from, NameId name,
                                                 LookupFilter filter)
{
	const std::vector<Namespace*>& order = levels(from);
	for (std::size_t index = 0; index < order.size(); ++index)
	{
		Entity* entity = order[index]->find(name);
		if (entity != nullptr && accepts(filter, *entity))
		{
			return entity;
		}
	}
	return nullptr;
}

Entity* TranslationUnitModel::lookup_qualified(Namespace& in, NameId name,
                                               LookupFilter filter)
{
	// 3.4.3.2p2: what a namespace declares, and only if it declares nothing of
	// that name, what the namespaces its using-directives nominate declare.
	++visit_;
	search_.clear();
	search_.push_back(&in);
	in.mark_ = visit_;
	for (std::size_t index = 0; index < search_.size(); ++index)
	{
		Entity* entity = search_[index]->find(name);
		if (entity != nullptr && accepts(filter, *entity))
		{
			return entity;
		}
		const std::vector<Namespace*>& used = search_[index]->nominated();
		for (std::size_t which = 0; which < used.size(); ++which)
		{
			if (used[which]->mark_ != visit_)
			{
				used[which]->mark_ = visit_;
				search_.push_back(used[which]);
			}
		}
	}
	return nullptr;
}

void write_namespace(std::ostream& out, const Namespace& space, const TypeTable& types,
                     const NameTable& names)
{
	if (space.name() == kNoName)
	{
		out << "start unnamed namespace\n";
	}
	else
	{
		out << "start namespace " << names.text(space.name()) << "\n";
	}
	if (space.is_inline())
	{
		out << "inline namespace\n";
	}

	const std::vector<Entity*>& variables = space.variables();
	for (std::size_t index = 0; index < variables.size(); ++index)
	{
		out << "variable " << names.text(variables[index]->name) << " "
		    << types.description(variables[index]->type) << "\n";
	}
	const std::vector<Entity*>& functions = space.functions();
	for (std::size_t index = 0; index < functions.size(); ++index)
	{
		out << "function " << names.text(functions[index]->name) << " "
		    << types.description(functions[index]->type) << "\n";
	}
	const std::vector<Namespace*>& members = space.members();
	for (std::size_t index = 0; index < members.size(); ++index)
	{
		write_namespace(out, *members[index], types, names);
	}

	out << "end namespace\n";
}
