#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
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
// alias both add a binding without declaring anything new, so a namespace's
// member lists are written where the entity is declared and a later binding
// does not move it.
struct Entity
{
	EntityKind kind;
	NameId name;
	TypeId type;       // Variable, Function and Typedef
	Namespace* space;  // Namespace
	// 3.5 and 13.1: two functions of one name in one namespace are two
	// entities whenever their parameter type lists differ, so the binding of
	// the name reaches an overload set rather than one declaration.
	Entity* overload;
	// PA8: the object of the program image this entity denotes, or zero.  A
	// typedef-name and a namespace denote no object, and neither does an
	// entity a tool that only describes declarations ever asks about.
	std::uint32_t symbol;
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
	Namespace(NameId name, bool is_inline, Namespace* parent, std::uint32_t id);

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

	// The namespaces a using-directive written here nominates, which 7.3.1p8
	// and 7.3.1.1p1 also write for an inline and for an unnamed member.
	const std::vector<Namespace*>& nominated() const { return nominated_; }

	// The inline members of 7.3.1p8.  They are also nominated, because for
	// unqualified lookup an inline namespace is exactly an implicit
	// using-directive; 3.4.3.2p2 is the one place the two differ, because it
	// asks what a namespace and its inline members declare before it asks
	// anything of a using-directive.
	const std::vector<Namespace*>& inlines() const { return inlines_; }

	// The one unnamed namespace this namespace can hold: 7.3.1.1 gives every
	// `namespace { }` in one namespace the same unique member.
	Namespace* unnamed() const { return unnamed_; }

private:
	friend class TranslationUnitModel;

	NameId name_;
	bool inline_;
	Namespace* parent_;
	std::size_t depth_;
	std::uint32_t id_;
	Namespace* unnamed_;
	std::unordered_map<NameId, Entity*> bindings_;
	std::vector<Entity*> variables_;
	std::vector<Entity*> functions_;
	std::vector<Namespace*> members_;
	std::vector<Namespace*> nominated_;
	std::vector<Namespace*> inlines_;

	// The namespaces 3.4.1 searches for a name written here, innermost level
	// first, as of `levels_epoch_`.  See TranslationUnitModel::levels.
	std::vector<Namespace*> levels_;
	std::uint64_t levels_epoch_;
	// Scratch of one walk: `mark_` says this namespace has been reached and
	// `chain_` that it is one of the scopes the walk started from.
	std::uint64_t mark_;
	std::uint64_t chain_;
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

	// 8.3.4p4 and 3.9p7: the one type two declarations of an entity agree on,
	// which is either the same type twice or an array a bound completed.
	TypeId merged(TypeId declared, TypeId again);

	// Binds an existing entity to a name in `where`, as a using-declaration
	// does.
	void bind(Namespace& where, NameId name, Entity& entity);

	// 7.3.2p3: a namespace-alias-definition, which may name what an alias
	// already declared here names and nothing else.  The alias is remembered
	// as such because 7.3.1 will not let a namespace-definition reopen a
	// namespace through one.
	void bind_alias(Namespace& where, NameId name, Entity& entity);

	Entity* lookup_unqualified(Namespace& from, NameId name, LookupFilter filter);
	Entity* lookup_qualified(Namespace& in, NameId name, LookupFilter filter);

private:
	// What tells two functions of one name in one namespace apart: 3.5 and
	// 13.1 say it is their parameter type lists, and nothing else.
	struct OverloadKey
	{
		std::uint64_t binding;
		std::uint32_t signature;

		bool operator==(const OverloadKey& other) const
		{
			return binding == other.binding && signature == other.signature;
		}
	};

	struct OverloadKeyHash
	{
		std::size_t operator()(const OverloadKey& key) const;
	};

	Namespace& create(Namespace& parent, NameId name, bool is_inline);
	Entity& create_entity(EntityKind kind, NameId name, TypeId type);
	Entity& create_function(Namespace& where, NameId name, TypeId type);
	// The name in the namespace, as one word, which is how a fact about a
	// binding rather than about an entity is keyed.
	static std::uint64_t binding_key(const Namespace& where, NameId name);
	// Adds every namespace of `edges` not yet reached by this walk to `out`.
	void reach(const std::vector<Namespace*>& edges, std::vector<Namespace*>& out);

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
	// The directives written so far, as `where` and `space` identifiers in one
	// word, so writing the same one twice costs a probe rather than a scan of
	// everything the namespace already nominates.
	std::unordered_set<std::uint64_t> nominations_;
	// The names a namespace-alias-definition introduced, keyed the same way,
	// so that a namespace which never wrote an alias - almost all of them -
	// carries nothing for the ones that did.
	std::unordered_set<std::uint64_t> aliases_;
	// Every function declared so far, by what makes it its own entity, so that
	// redeclaring one costs a probe rather than a walk of everything the name
	// already reaches.
	std::unordered_map<OverloadKey, Entity*, OverloadKeyHash> overloads_;
	std::vector<Namespace*> cached_;
	// Scratch kept between walks so that a walk allocates nothing: the
	// namespaces reached, the queue of a qualified lookup, and one bucket per
	// level of the namespace a lookup starts from.
	std::vector<Namespace*> reachable_;
	std::vector<Namespace*> search_;
	std::vector<std::vector<Namespace*> > anchored_;
};

// 3.3.6: true when `outer` is `inner` or is one of the namespaces enclosing
// it, which is what 7.3.1.2p2 asks of a declaration written outside the
// namespace whose member it declares.
bool encloses(const Namespace& outer, const Namespace& inner);

// Writes the description of `space` and of everything it declares.
void write_namespace(std::ostream& out, const Namespace& space,
                     const TypeTable& types, const NameTable& names);
