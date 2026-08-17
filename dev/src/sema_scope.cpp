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
	, template_head(enclosing == nullptr
		? nullptr
		: (enclosing->kind == ScopeKind::TemplateParameters
			? enclosing
			: enclosing->template_head))
	, unnamed_region(enclosing != nullptr && enclosing->unnamed_region)
	, local_function(nullptr)
	, local_occurrence(0)
	, local_unnamed(false)
	, visit(0)
	, dependent_base(false)
	, inheriting_constructors(false)
	, searchers_at(0)
{}

// 10.2p2 and 14.6.2p3: the region 3.4.1's search of `scope` looks in after
// `scope` itself.
//
// They are the base classes', except the ones whose base-specifier named a type
// that depends on a template parameter: which class such a base is only an
// argument list says, so a name written in the class's own definition is looked
// up without it and the specialization an argument list makes answers the same
// way the definition did.  It is a fact of each specifier and not of the clause,
// so a class deriving from a settled class *and* a dependent one is searched in
// the settled one exactly as it would be with the other left unwritten.  The
// link is dropped only where the search *reaches* the class from inside it - the
// base subobject a class further down holds is found through that class's own
// link, which no template head stands over.
const std::vector<Scope*>& unqualified_bases(const Scope& scope)
{
	return scope.dependent_base ? scope.open_bases : scope.bases;
}

bool encloses(const Scope& outer, const Scope& inner)
{
	for (const Scope* at = &inner; at != nullptr; at = at->parent)
	{
		if (at == &outer)
		{
			return true;
		}
	}
	return false;
}

bool declares_subobject(const SemaEntity& member, const Scope& scope)
{
	return member.kind == SemaKind::Variable && member.object_member &&
		member.region == &scope && member.shadowed == nullptr;
}

// 8.5.1p1: whether an object of the class `scope` declares is initialized from
// a braced-init-list by initializing its members with the clauses.  A class
// with a base class is no aggregate, which the caller asks before this, and the
// PA16 slice has no virtual function - so what is left to ask is whether every
// non-static data member is public, none was written with a
// brace-or-equal-initializer, and the program provided no constructor - which
// 12.1p4 does not count `= default` or `= delete` as doing.
bool aggregate_class(Scope& scope)
{
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (member.shadowed != nullptr)
		{
			// 7.3.3p1: the declaration is of a member of a base class, and a
			// class with a base class is no aggregate anyway.
			continue;
		}
		if (member.kind == SemaKind::Function)
		{
			if (member.special == kConstructorFunction && member.user_provided)
			{
				return false;
			}
			continue;
		}
		if (member.kind != SemaKind::Variable || !member.object_member)
		{
			continue;
		}
		if (member.access != kPublicAccess || member.default_initializer)
		{
			return false;
		}
	}
	return true;
}

bool holds_written_definitions(const Scope& scope)
{
	return scope.kind == ScopeKind::Class && scope.owner != nullptr &&
		(scope.parent == nullptr || scope.parent->kind != ScopeKind::Class);
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
	, folding_depth_(0)
	, reach_(0)
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
	Scope& scope = scopes_.back();
	// 9.8p1: the region a function's parameters and body stand in is inside
	// that function, and every region opened under it is too - a block, a class
	// the block declares, the body of that class's members.  Each of the three
	// answers the question the same way, so it is settled once here and read
	// afterwards rather than walked outwards from each declaration.
	if (kind == ScopeKind::Function && owner != nullptr &&
	    owner->kind == SemaKind::Function)
	{
		scope.local_function = owner;
	}
	else if (kind == ScopeKind::Class && owner != nullptr)
	{
		// A member is named through its class, so it is the class's own place
		// among what the function declared that its name carries.
		scope.local_function = owner->local_function;
		scope.local_occurrence = owner->local_occurrence;
		scope.local_unnamed = owner->local_unnamed;
	}
	else
	{
		scope.local_function = parent.local_function;
		scope.local_occurrence = parent.local_occurrence;
		scope.local_unnamed = parent.local_unnamed;
	}
	return scope;
}

DumpScope& SemaModel::open_dump(DumpScope& parent, const std::string& header)
{
	dumps_.push_back(DumpScope());
	DumpScope& scope = dumps_.back();
	scope.header = header;
	parent.children.push_back(&scope);
	return scope;
}

DumpScope& SemaModel::detached_dump()
{
	dumps_.push_back(DumpScope());
	return dumps_.back();
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
	entity.fold_local = false;
	entity.address = 0;
	entity.object_definition = false;
	entity.c_linkage = false;
	entity.internal_linkage = false;
	entity.thread_storage = false;
	entity.local_static = false;
	entity.declared_begin = 0;
	entity.declared_end = 0;
	entity.builtin = kNotBuiltin;
	entity.nonthrowing = false;
	entity.wrote_exception_specification = false;
	entity.promotion = kNoType;
	entity.value = 0;
	entity.real = 0;
	entity.next = nullptr;
	entity.tail = nullptr;
	entity.region = nullptr;
	entity.storage = nullptr;
	entity.constructor = nullptr;
	entity.destructor = nullptr;
	entity.member_constructor = nullptr;
	entity.member_entry = false;
	entity.bases.clear();
	entity.empty_class = true;
	entity.virtual_function = false;
	entity.pure_virtual = false;
	entity.final_virtual = false;
	entity.override_written = false;
	entity.overridden = nullptr;
	entity.vtable_index = kNoVtableIndex;
	entity.vtable.clear();
	entity.key_function = nullptr;
	entity.deleting_release = nullptr;
	entity.polymorphic = false;
	entity.introduces_vptr = false;
	entity.abstract = false;
	entity.special = kOrdinaryFunction;
	entity.transfer = kNotTransfer;
	for (unsigned index = 0; index < kTransferKinds; ++index)
	{
		entity.transfers[index] = nullptr;
	}
	entity.explicit_function = false;
	entity.conversion_function = false;
	entity.conversions.clear();
	entity.conversions_above.clear();
	entity.complete_object_entry = false;
	entity.base_object_entry = false;
	entity.source_base_entry = false;
	entity.implicit_declaration = false;
	entity.out_of_class_definition = false;
	entity.instantiated_use = false;
	entity.shadowed = nullptr;
	entity.inherited = nullptr;
	entity.delegates_to = nullptr;
	entity.surrogate_for = nullptr;
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
	entity.own_source_definition = false;
	entity.object_member = false;
	entity.mutable_member = false;
	entity.template_parameters = nullptr;
	entity.templated = nullptr;
	entity.primary = nullptr;
	entity.partial_of = nullptr;
	entity.instantiated = false;
	entity.explicitly_instantiated = false;
	entity.explicit_specialization = false;
	entity.definition_required = false;
	entity.instantiated_definition = false;
	entity.member_specialized = false;
	entity.storage_demanded = false;
	entity.reached_at = 0;
	entity.template_arguments = 0;
	entity.id = static_cast<std::uint32_t>(entities_.size() - 1);
	entity.dump_name = name;
	entity.local_function = nullptr;
	entity.local_occurrence = 0;
	entity.local_unnamed = false;
	entity.pack_run = 0;
	entity.pack_element_of = nullptr;
	entity.constexpr_function = false;
	entity.constexpr_body = nullptr;
	entity.constexpr_region = nullptr;
	entity.covered_constant = true;
	entity.literal_class = kLiteralUnsettled;
	entity.valued_class = kLiteralUnsettled;
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

TypeId SemaModel::folded_call(const SemaEntity& callee,
                              std::uint32_t arguments) const
{
	const std::unordered_map<std::uint64_t, TypeId>::const_iterator found =
		folded_calls_.find(overload_key(callee, arguments));
	return found == folded_calls_.end() ? kNoType : found->second;
}

void SemaModel::hold_folded_call(const SemaEntity& callee,
                                 std::uint32_t arguments, TypeId value)
{
	folded_calls_.insert(std::make_pair(overload_key(callee, arguments), value));
}

TypeId SemaModel::value_initialized(TypeId type) const
{
	const std::unordered_map<TypeId, TypeId>::const_iterator found =
		value_initialized_.find(type);
	return found == value_initialized_.end() ? kNoType : found->second;
}

void SemaModel::hold_value_initialized(TypeId type, TypeId value)
{
	value_initialized_.insert(std::make_pair(type, value));
}

void SemaModel::befriend(const SemaEntity& granting, const SemaEntity& friendly)
{
	friendships_.insert((static_cast<std::uint64_t>(granting.id) << 32) |
	                    friendly.id);
}

// 14.5.4p1: a friend of a class may be a template, and a grant written inside
// a class template's own definition was made by a template - so the pair a
// grant was recorded between is a pair of *templates* where either side was
// written under a head, while every access check asks the question of the two
// declarations a use actually named.  `primary` is the one fact that joins
// them: both tiers put it on every specialization they make, so the pair is
// asked as the use spelled it and then with each side replaced by the template
// it came from.  A grant recorded between two specializations reaches no other
// one, because the substitution only ever runs in this direction.
bool SemaModel::befriended(const SemaEntity& granting,
                           const SemaEntity& friendly) const
{
	if (holds_friendship(granting, friendly))
	{
		return true;
	}
	if (friendly.primary != nullptr && holds_friendship(granting, *friendly.primary))
	{
		return true;
	}
	if (granting.primary == nullptr)
	{
		return false;
	}
	return holds_friendship(*granting.primary, friendly) ||
		(friendly.primary != nullptr &&
		 holds_friendship(*granting.primary, *friendly.primary));
}

bool SemaModel::holds_friendship(const SemaEntity& granting,
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
	// 3.3.2p6 asks the same of 3.3.7p1's region: it holds the places one
	// declarator wrote and is gone with that declarator, so a class an
	// elaborated-type-specifier in a parameter-declaration-clause first
	// declares belongs to the region around it.
	while ((where->kind == ScopeKind::TemplateParameters ||
	        where->kind == ScopeKind::Prototype) &&
	       where->parent != nullptr)
	{
		where = where->parent;
	}
	return *where;
}

bool declares_member_template(Scope* scope, const Scope* head)
{
	return scope != nullptr && head == scope &&
		scope->kind == ScopeKind::TemplateParameters &&
		declaring_region(*scope).kind == ScopeKind::Class &&
		declaring_region(*scope).owner != nullptr;
}

void require_template_special_member(const std::string& spelled,
                                     const Scope* head, bool destructor,
                                     bool defaulted)
{
	if (head == nullptr)
	{
		return;
	}
	if (destructor)
	{
		throw std::runtime_error(spelled + " declares a destructor template, "
		                         "which 14.5.2p1 does not allow");
	}
	if (defaulted)
	{
		throw std::runtime_error(spelled + " is explicitly defaulted under a "
		                         "template head, which 8.4.2p1 does not allow");
	}
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
		settle_local_name(where, entity);
	}
}

// 9.8p1 and the ABI's `<local-name>`: which function's body declares this, and
// which occurrence of the name there it is.  Both are facts of the region, so
// the declaration reads them where it is recorded and no later question about
// the object-file name walks outwards to ask again.
void SemaModel::settle_local_name(Scope& where, SemaEntity& entity)
{
	entity.local_function = where.local_function;
	if (entity.local_function == nullptr)
	{
		return;
	}
	if (where.kind != ScopeKind::Function && where.kind != ScopeKind::Block)
	{
		// A member of a local class stands under the class the function
		// declared, and it is that class the occurrence number belongs to.
		entity.local_occurrence = where.local_occurrence;
		entity.local_unnamed = where.local_unnamed;
		return;
	}
	// 3.5p8 leaves a local class without linkage, so nothing but the name and
	// the function tells two of them apart - and only a declaration the object
	// file has to name takes a number, which in this subset is a type.
	if (entity.kind != SemaKind::Class && entity.kind != SemaKind::Enum)
	{
		return;
	}
	const std::uint32_t function = entity.local_function->id;
	std::string key(4, '\0');
	key[0] = static_cast<char>(function & 0xff);
	key[1] = static_cast<char>((function >> 8) & 0xff);
	key[2] = static_cast<char>((function >> 16) & 0xff);
	key[3] = static_cast<char>((function >> 24) & 0xff);
	key += entity.name;
	entity.local_occurrence = local_occurrences_[key]++;
}

// 9.8p1 and the ABI's `<unnamed-type-name>`: the same two facts for a type the
// function's body gave no name to.
//
// Nothing binds such a declaration in the region, so `declare_in` never sees
// it, and the name a declarator lends it says nothing about it: 3.5p8 leaves it
// without linkage, so what the object file names it by is the function and its
// place among the unnamed types the function declares.  A number this unit
// counted for itself would be a different type's in another unit.
void SemaModel::settle_unnamed_local_name(Scope& where, SemaEntity& entity)
{
	if (where.kind != ScopeKind::Function && where.kind != ScopeKind::Block)
	{
		// A type a local class's own body left unnamed stands under that class,
		// which is the component the function declared and the one the number
		// belongs to - so this is a question about the region a function's body
		// is, and no other.
		return;
	}
	entity.local_function = where.local_function;
	if (entity.local_function == nullptr)
	{
		return;
	}
	entity.local_unnamed = true;
	entity.local_occurrence = local_unnamed_[entity.local_function->id]++;
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

// 10.2p2: whether one of the regions a class derives from, or one of theirs in
// turn, is `declarer`.
//
// 10.1p3's repeated base is refused where a class is completed, so the regions
// below one form a tree and this is one visit per class rather than one per
// path into it.
bool SemaModel::declares_below(const std::vector<Scope*>& bases,
                               const Scope& declarer)
{
	for (std::size_t index = 0; index < bases.size(); ++index)
	{
		if (bases[index] == &declarer ||
		    declares_below(bases[index]->bases, declarer))
		{
			return true;
		}
	}
	return false;
}

// 10.2p2 and 10.2p6: the declaration of `name` a class that does not declare it
// inherits, which is what each of the classes it derives from answers.
//
// What a class declares hides what *its* bases declare, so each base is asked
// for its own declaration before its bases are asked at all; two bases that
// answer differently is 10.2p6's ambiguity, which no argument of the lookup can
// resolve and which is therefore refused where the name is written.
SemaEntity* SemaModel::find_inherited(const std::vector<Scope*>& bases,
                                      const std::string& name,
                                      LookupKind filter)
{
	SemaEntity* found = nullptr;
	for (std::size_t index = 0; index < bases.size(); ++index)
	{
		Scope& base = *bases[index];
		SemaEntity* here = find(base, name, filter);
		if (here == nullptr)
		{
			here = find_inherited(base.bases, name, filter);
		}
		if (here == nullptr || here == found)
		{
			continue;
		}
		if (found != nullptr)
		{
			throw std::runtime_error(name + " is declared in more than one of "
			                         "the base classes a lookup for it reached");
		}
		found = here;
	}
	return found;
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
		// region with no base-clause leaves the list empty, so this costs one
		// test per enclosing region in a program with no inheritance.
		if (declares_below(unqualified_bases(*scope), declarer))
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

// 7.3.4p2: a using-directive does not make the names it nominates appear where
// it was written.  They appear in the nearest region enclosing both the
// directive and the namespace it named - which is the region that wrote it only
// where that region encloses the namespace, and an outer one otherwise.  Which
// level of the chain each declaration stands at is the whole of 3.4.1's answer
// where several regions declare one name: two at one level are 3.4p1's
// ambiguity and one at a nearer level simply hides the rest.
//
// So the chain is walked outward exactly as an unqualified lookup already walks
// it, and each region that declares the name is placed on it as the walk goes
// by: a level that wrote a directive reaching one takes it up as pending, and
// the first level from there that *encloses* it is where 7.3.4p2 says its
// declarations appear.  Asking of each declaration where it appears rather than
// walking every namespace the directives reach is the cheaper of the two by the
// same measure `declarers` already takes - a name is declared in few regions and
// a directive may reach many - and 7.3.4p4's transitivity is `reaches`.
//
// A level that wrote no directive and holds nothing pending costs one probe of
// the name and nothing else, so a lookup answered where it stands pays that one
// level and never the chain above it.

// A region a directive already reached, held until the walk arrives at the
// level 7.3.4p2 puts it at.  The list is what an already-taken region is looked
// for in, because `walk_reached` below owns the visit stamp a scope carries -
// and it holds only the regions that have been reached and not yet placed,
// which is one entry per directive the chain wrote and never one per region a
// directive could reach.
void SemaModel::take_pending(Scope& declaring)
{
	for (std::size_t index = 0; index < placed_.size(); ++index)
	{
		if (placed_[index] == &declaring)
		{
			return;
		}
	}
	placed_.push_back(&declaring);
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
		// One region declares the name, so every level it appears at holds the
		// one declaration and 3.4p1 has nothing to tell apart.
		return merge_found(nullptr,
		                   lookup_unique(from, nullptr, name, filter,
		                                 *(*regions)[0]),
		                   found_set);
	}
	// Several regions declare the name, so 3.4p1 asks which of them a lookup
	// written here reaches at each level rather than which one it reaches
	// first.
	placed_.clear();
	for (Scope* scope = &from; scope != nullptr; scope = scope->parent)
	{
		SemaEntity* found = merge_found(nullptr, find(*scope, name, filter),
		                                found_set);
		// 10.2p2 and 3.4.1p8: what a base class declares is found from a member
		// of the derived class, and hides what the region around the class
		// declares rather than being hidden by it.  7.3.4p1 writes no
		// using-directive in a class, so a level a class stands at holds
		// nothing else.
		if (found == nullptr)
		{
			found = merge_found(found,
			                    find_inherited(unqualified_bases(*scope), name,
			                                   filter),
			                    found_set);
		}
		if (!scope->nominated.empty())
		{
			// A directive stands at this level, so a region it reaches appears
			// somewhere on the chain from here outward.  A region with no
			// directive nominating it appears only where it was written, which
			// the walk finds where it stands.
			//
			// There are two ways to ask which those regions are, and the cheaper
			// one is whichever set is smaller: the regions this level reaches,
			// or the regions that declare the name.  So the walk of the first
			// runs on a budget of the second's size and gives up when it is the
			// larger, which makes a level cost the smaller of the two however
			// lopsided they are - n blocks each nominating one of n namespaces
			// that all declare one name is one reached region per lookup and not
			// n probes of the declaring list.
			if (walk_reached(*scope, regions->size()))
			{
				for (std::size_t index = 0; index < reached_.size(); ++index)
				{
					Scope& declaring = *reached_[index];
					if (&declaring == scope ||
					    find(declaring, name, filter) == nullptr)
					{
						continue;
					}
					take_pending(declaring);
				}
			}
			else
			{
				for (std::size_t index = 0; index < regions->size(); ++index)
				{
					Scope& declaring = *(*regions)[index];
					if (declaring.nominated_by.empty() ||
					    !reaches(*scope, declaring))
					{
						continue;
					}
					take_pending(declaring);
				}
			}
		}
		for (std::size_t index = 0; index < placed_.size();)
		{
			Scope& declaring = *placed_[index];
			if (!encloses(*scope, declaring))
			{
				++index;
				continue;
			}
			// 7.3.4p2: this is the nearest region enclosing both the directive
			// and the namespace it named, so what that namespace declares is
			// one of this level's own as far as 3.4p1 is concerned.
			found = merge_found(found, find(declaring, name, filter),
			                    found_set);
			placed_[index] = placed_.back();
			placed_.pop_back();
		}
		if (found != nullptr)
		{
			return found;
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
	// hides what its bases do - so the nearest base that declares it is the one
	// the name denotes.
	SemaEntity* const inherited = find_inherited(in.bases, name, filter);
	if (inherited != nullptr)
	{
		return merge_found(nullptr, inherited, found_set);
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
