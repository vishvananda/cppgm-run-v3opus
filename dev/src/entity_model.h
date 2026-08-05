#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "name_table.h"
#include "type_model.h"

class Namespace;

// A translation unit that the standard does not allow.  Behaviour is
// undefined for an ill-formed program, so the tool refuses rather than
// describing something the source did not say.
class SemanticError : public std::runtime_error
{
public:
	explicit SemanticError(const std::string& what)
		: std::runtime_error(what)
	{}
};

// What a name denotes.  A namespace scope in PA7 holds no more than this.
enum class EntityKind
{
	Variable,
	Function,
	Typedef,
	Namespace
};

// One entity.  A name is bound to an entity, and several names in several
// namespaces can be bound to the same one: a using-declaration and a namespace
// alias both add a binding without declaring anything new, which is why `home`
// and not the binding decides whose member list an entity appears in.
struct Entity
{
	EntityKind kind;
	NameId name;
	TypeId type;       // Variable, Function and Typedef
	Namespace* space;  // Namespace
	Namespace* home;   // the namespace that declared it
};

// Which entities a lookup accepts.  3.4 gives contexts that see only part of
// what a name is bound to, and a filtered-out binding does not hide what an
// enclosing scope has: in `namespace M { int N; using namespace N; }` the
// variable `N` must not stop the namespace `N` from being found.
enum class LookupFilter
{
	Any,
	Type,
	Space
};

// A namespace: the declarative region of 3.3.6, the members of 7.3, and the
// using-directives written in it.
//
// The three ordered lists are the output format's, and they hold what this
// namespace declared, in the order it first declared it.  The binding map is
// larger than their union, because it also holds the names a using-declaration
// or a namespace alias introduced.
class Namespace
{
public:
	Namespace(NameId name, bool is_inline, Namespace* parent);

	NameId name() const { return name_; }
	bool is_inline() const { return inline_; }
	Namespace* parent() const { return parent_; }
	// The number of namespaces between this one and the global namespace,
	// which turns "is an ancestor of" into an integer compare and a walk of
	// known length.
	std::size_t depth() const { return depth_; }

	// The binding of `name` in this declarative region alone.
	Entity* find(NameId name) const;

	const std::vector<Entity*>& variables() const { return variables_; }
	const std::vector<Entity*>& functions() const { return functions_; }
	const std::vector<Namespace*>& members() const { return members_; }
	const std::vector<Namespace*>& nominated() const { return nominated_; }

	// The one unnamed namespace this namespace can hold: 7.3.1.1 gives every
	// `namespace { }` in one namespace the same unique member.
	Namespace* unnamed() const { return unnamed_; }

private:
	friend class TranslationUnitModel;

	NameId name_;
	bool inline_;
	Namespace* parent_;
	std::size_t depth_;
	Namespace* unnamed_;
	std::unordered_map<NameId, Entity*> bindings_;
	std::vector<Entity*> variables_;
	std::vector<Entity*> functions_;
	std::vector<Namespace*> members_;
	std::vector<Namespace*> nominated_;

	// The namespaces 3.4.1 searches for a name written here, innermost level
	// first, as of `levels_epoch_`.  See TranslationUnitModel::levels.
	std::vector<Namespace*> levels_;
	std::uint64_t levels_epoch_;
	std::uint64_t mark_;
};

// The object model of one translation unit: its namespaces, its entities, and
// the two name lookups of 3.4 over them.
class TranslationUnitModel
{
public:
	explicit TranslationUnitModel(TypeTable& types);

	Namespace& global() { return *global_; }

	// 7.3.1: the namespace `name` denotes in `parent`, defined there if this
	// is its first definition.  Reopening a namespace that was not declared
	// inline as an inline one is the error 7.3.1p8 names.
	Namespace& open_namespace(Namespace& parent, NameId name, bool is_inline);
	Namespace& open_unnamed_namespace(Namespace& parent, bool is_inline);

	// A using-directive in `where` nominating `space`, which 7.3.1p8 and
	// 7.3.1.1p1 also insert for an inline and for an unnamed namespace.
	void nominate(Namespace& where, Namespace& space);

	// Declares `name` in `where`, or returns the entity already declared there
	// with its type completed by this declaration.
	Entity& declare(Namespace& where, EntityKind kind, NameId name, TypeId type);

	// A redeclaration reached through a qualified declarator-id, which names
	// an entity that must already exist.
	void redeclare(Entity& entity, EntityKind kind, TypeId type);

	// Binds an existing entity to a name in `where`, as a using-declaration
	// and a namespace-alias-definition do.
	void bind(Namespace& where, NameId name, Entity& entity);

	Entity* lookup_unqualified(Namespace& from, NameId name, LookupFilter filter);
	Entity* lookup_qualified(Namespace& in, NameId name, LookupFilter filter);

private:
	Namespace& create(Namespace& parent, NameId name, bool is_inline);
	Entity& create_entity(EntityKind kind, NameId name, TypeId type, Namespace* home);
	TypeId merged(TypeId declared, TypeId again);

	// The namespaces 3.4.1 searches from `from`, in order.
	//
	// The list is the scope chain with, after each of its members, the
	// namespaces whose declarations 7.3.4p2 makes appear there: a
	// using-directive puts the names it nominates in the nearest namespace
	// enclosing both the directive and the nominated namespace, and a
	// directive reached through another directive counts as well.  Only a new
	// using-directive can change any of that, so the answer is kept per
	// namespace and rebuilt when `epoch_` has moved.
	const std::vector<Namespace*>& levels(Namespace& from);

	// Drops every kept list.  A list is as long as the closure it describes,
	// so keeping stale ones would cost a translation unit one closure per
	// namespace that ever looked a name up; releasing them when the answer
	// stops being true bounds the whole cache by the closures reached since
	// the last using-directive.
	void forget_levels();

	TypeTable& types_;
	std::deque<Namespace> spaces_;
	std::deque<Entity> entities_;
	Namespace* global_;
	std::uint64_t epoch_;
	std::uint64_t visit_;
	std::vector<Namespace*> cached_;
	std::vector<Namespace*> reachable_;
	std::vector<Namespace*> search_;
};

// Writes the description of `space` and of everything it declares.
void write_namespace(std::ostream& out, const Namespace& space,
                     const TypeTable& types, const NameTable& names);
