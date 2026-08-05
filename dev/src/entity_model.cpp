#include "entity_model.h"

#include <ostream>

// The depths make the walk one of known length and let a mismatch be rejected
// before it starts.
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

namespace
{

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

Namespace::Namespace(NameId name, bool is_inline, Namespace* parent, std::uint32_t id)
	: name_(name)
	, inline_(is_inline)
	, parent_(parent)
	, depth_(parent == nullptr ? 0 : parent->depth() + 1)
	, id_(id)
	, unnamed_(nullptr)
	, levels_epoch_(0)
	, mark_(0)
	, chain_(0)
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
	spaces_.push_back(Namespace(kNoName, false, nullptr, 0));
	global_ = &spaces_.back();
}

Namespace& TranslationUnitModel::create(Namespace& parent, NameId name, bool is_inline)
{
	const std::uint32_t id = static_cast<std::uint32_t>(spaces_.size());
	spaces_.push_back(Namespace(name, is_inline, &parent, id));
	Namespace& space = spaces_.back();
	parent.members_.push_back(&space);
	if (is_inline)
	{
		parent.inlines_.push_back(&space);
		nominate(parent, space);
	}
	return space;
}

Entity& TranslationUnitModel::create_entity(EntityKind kind, NameId name, TypeId type)
{
	Entity entity;
	entity.kind = kind;
	entity.name = name;
	entity.type = type;
	entity.space = nullptr;
	entity.overload = nullptr;
	entity.symbol = 0;
	entities_.push_back(entity);
	return entities_.back();
}

std::uint64_t TranslationUnitModel::binding_key(const Namespace& where, NameId name)
{
	return (static_cast<std::uint64_t>(where.id_) << 32) | name;
}

std::size_t TranslationUnitModel::OverloadKeyHash::operator()(const OverloadKey& key) const
{
	const std::uint64_t mixed = (key.binding ^ (key.binding >> 29)) * 1099511628211ULL;
	const std::uint64_t hash = (mixed ^ key.signature) * 1099511628211ULL;
	return static_cast<std::size_t>(hash ^ (hash >> 32));
}

Entity& TranslationUnitModel::create_function(Namespace& where, NameId name, TypeId type)
{
	Entity& entity = create_entity(EntityKind::Function, name, type);
	const OverloadKey key = {binding_key(where, name), types_.signature(type)};
	overloads_.insert(std::make_pair(key, &entity));
	where.functions_.push_back(&entity);
	return entity;
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
	    bound->space->parent() == &parent &&
	    aliases_.find(binding_key(parent, name)) == aliases_.end())
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
	Entity& entity = create_entity(EntityKind::Namespace, name, kNoType);
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
	// Writing a directive that is already there says nothing new, and saying
	// nothing new must not move the epoch, or a namespace that repeats one
	// would rebuild every kept level list for no change in the answer.
	const std::uint64_t edge =
		(static_cast<std::uint64_t>(where.id_) << 32) | space.id_;
	if (!nominations_.insert(edge).second)
	{
		return;
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
	if (bound != nullptr && kind == EntityKind::Function &&
	    bound->kind == EntityKind::Function)
	{
		// 3.5 and 13.1: this declares the function already declared here only
		// if their parameter type lists are the same; otherwise it adds
		// another function to the overload set the name reaches.  The set is
		// a list only so that an expression can see that it has more than one
		// member, so a new member goes in behind the binding rather than at
		// the end of it.
		const OverloadKey key = {binding_key(where, name), types_.signature(type)};
		const std::unordered_map<OverloadKey, Entity*, OverloadKeyHash>::iterator found =
			overloads_.find(key);
		if (found != overloads_.end())
		{
			redeclare(*found->second, kind, type);
			return *found->second;
		}
		Entity& entity = create_function(where, name, type);
		entity.overload = bound->overload;
		bound->overload = &entity;
		return entity;
	}
	if (bound != nullptr)
	{
		redeclare(*bound, kind, type);
		return *bound;
	}

	if (kind == EntityKind::Function)
	{
		Entity& entity = create_function(where, name, type);
		where.bindings_[name] = &entity;
		return entity;
	}

	Entity& entity = create_entity(kind, name, type);
	where.bindings_[name] = &entity;
	if (kind == EntityKind::Variable)
	{
		where.variables_.push_back(&entity);
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

void TranslationUnitModel::bind_alias(Namespace& where, NameId name, Entity& entity)
{
	const std::uint64_t key = binding_key(where, name);
	Entity* bound = where.find(name);
	if (bound != nullptr)
	{
		// 7.3.2p3: only an alias declared in this region may be written
		// again, and only for the namespace it already names.
		if (bound != &entity || aliases_.find(key) == aliases_.end())
		{
			throw SemanticError("a namespace alias redeclares a name of this namespace");
		}
		return;
	}
	where.bindings_[name] = &entity;
	aliases_.insert(key);
}

const std::vector<Namespace*>& TranslationUnitModel::levels(Namespace& from)
{
	if (from.levels_epoch_ == epoch_)
	{
		return from.levels_;
	}

	const std::size_t height = from.depth() + 1;
	if (anchored_.size() < height)
	{
		anchored_.resize(height);
	}
	for (std::size_t level = 0; level < height; ++level)
	{
		anchored_[level].clear();
	}

	++visit_;
	for (Namespace* space = &from; space != nullptr; space = space->parent())
	{
		space->chain_ = visit_;
	}

	// One pass per scope in the chain.  A directive is in scope for the whole
	// chain, so a namespace already reached from an inner scope is not reached
	// again; a namespace reached first from scope `context` has its names
	// appear in the nearest namespace enclosing both it and `context`.  A scope
	// of the chain is already a level of its own, so reaching one adds nothing
	// - but 7.3.4p4 still makes its directives read from the inner scope that
	// reached it, which is nearer than reading them from the scope itself.
	for (Namespace* context = &from; context != nullptr; context = context->parent())
	{
		reachable_.clear();
		reachable_.push_back(context);
		context->mark_ = visit_;
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
				reachable_.push_back(space);
				if (space->chain_ == visit_)
				{
					continue;
				}
				Namespace* anchor = space;
				while (!encloses(*anchor, *context))
				{
					anchor = anchor->parent();
				}
				anchored_[from.depth() - anchor->depth()].push_back(space);
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
		from.levels_.insert(from.levels_.end(), anchored_[level].begin(),
		                    anchored_[level].end());
		++level;
	}
	from.levels_epoch_ = epoch_;
	return from.levels_;
}

Entity* TranslationUnitModel::lookup_unqualified(Namespace& from, NameId name,
                                                 LookupFilter filter)
{
	// 3.4.1 searches the innermost scope first, and 7.3.4p2 can put what a
	// using-directive nominates no nearer than that same scope, so a name this
	// namespace declares is already the answer.  Asking here first is what
	// keeps a lookup from having to know what else is in scope, which is the
	// expensive question: the set of namespaces a directive makes visible has
	// to be worked out again every time a new directive is written.
	Entity* declared = from.find(name);
	if (declared != nullptr && accepts(filter, *declared))
	{
		return declared;
	}

	// `levels` starts at `from`, which has just been asked.
	const std::vector<Namespace*>& order = levels(from);
	for (std::size_t index = 1; index < order.size(); ++index)
	{
		Entity* entity = order[index]->find(name);
		if (entity != nullptr && accepts(filter, *entity))
		{
			return entity;
		}
	}
	return nullptr;
}

void TranslationUnitModel::reach(const std::vector<Namespace*>& edges,
                                 std::vector<Namespace*>& out)
{
	for (std::size_t index = 0; index < edges.size(); ++index)
	{
		if (edges[index]->mark_ != visit_)
		{
			edges[index]->mark_ = visit_;
			out.push_back(edges[index]);
		}
	}
}

Entity* TranslationUnitModel::lookup_qualified(Namespace& in, NameId name,
                                               LookupFilter filter)
{
	// 3.4.3.2p2: what a namespace and its inline namespaces declare, and only
	// if they declare nothing of that name, the same question again of every
	// namespace their using-directives nominate.  The two are separate waves
	// rather than one queue, because an inline member is not merely nominated:
	// what it declares hides what a using-directive would have reached.
	++visit_;
	search_.clear();
	search_.push_back(&in);
	in.mark_ = visit_;

	std::size_t wave = 0;
	while (wave < search_.size())
	{
		for (std::size_t index = wave; index < search_.size(); ++index)
		{
			reach(search_[index]->inlines(), search_);
		}
		const std::size_t end = search_.size();

		for (std::size_t index = wave; index < end; ++index)
		{
			Entity* entity = search_[index]->find(name);
			if (entity != nullptr && accepts(filter, *entity))
			{
				return entity;
			}
		}

		for (std::size_t index = wave; index < end; ++index)
		{
			reach(search_[index]->nominated(), search_);
		}
		wave = end;
	}
	return nullptr;
}

namespace
{

void write_header(std::ostream& out, const Namespace& space, const TypeTable& types,
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
}

// One namespace being described, and how many of its members already are.
struct Open
{
	const Namespace* space;
	std::size_t next;
};

}

void write_namespace(std::ostream& out, const Namespace& space, const TypeTable& types,
                     const NameTable& names)
{
	// A namespace description closes after its members' descriptions, so the
	// walk keeps the namespaces it is inside on a stack of its own rather than
	// on the machine stack, which a translation unit could nest past.
	std::vector<Open> open;
	write_header(out, space, types, names);
	const Open root = {&space, 0};
	open.push_back(root);

	while (!open.empty())
	{
		const std::vector<Namespace*>& members = open.back().space->members();
		if (open.back().next == members.size())
		{
			out << "end namespace\n";
			open.pop_back();
			continue;
		}
		const Namespace& member = *members[open.back().next++];
		write_header(out, member, types, names);
		const Open next = {&member, 0};
		open.push_back(next);
	}
}
