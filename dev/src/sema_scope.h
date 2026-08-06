#pragma once

#include <cstdint>
#include <deque>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "type_model.h"

// The declarations, scopes and bindings of one translation unit.
//
// This is the first semantic layer: what a declaration introduced, which
// region it introduced it into, and the two lookups of 3.4 over the result.
// It knows nothing of syntax, so a later assignment that needs a fact about a
// declaration asks the model rather than the tree it was read from.

// What a name denotes (3.3, 3.4, 7.1.3, 7.2, 7.3).
enum class SemaKind
{
	Namespace,
	NamespaceAlias,
	Class,
	Enum,
	Typedef,
	Variable,
	Function,
	Parameter,
	Enumerator,
	TemplateType
};

// The declarative regions of 3.3, which are also the scopes the dump names.
enum class ScopeKind
{
	Namespace,
	TemplateParameters,
	Class,
	Enum,
	Function,
	Block
};

// Which declarations a lookup accepts.  3.4 gives contexts that see only part
// of what a name is bound to: an elaborated-type-specifier sees a class that
// an ordinary declaration hides (3.4.4p2), and a namespace-name sees only a
// namespace or an alias of one (7.3.1p3).
enum class LookupKind
{
	Any,
	Type,
	Space,
	// A name written before `::`, which 3.4.3p1 lets name a namespace, a
	// class, an enumeration, or an alias of one.  Asking for all four at once
	// is what keeps a nested-name-specifier one lookup rather than one per
	// kind it might be.
	Region
};

class Scope;

// One declared entity.
//
// A name is bound to an entity, and several names can be bound to the same
// one: a using-declaration and a namespace alias add a binding without
// declaring anything new, so the entity stays where it was declared.
struct SemaEntity
{
	SemaKind kind;
	std::string name;
	TypeId type;
	// Namespace, NamespaceAlias, Class and Enum: the region the declaration
	// introduced, which is what a qualified name looks into.
	Scope* scope;
	// A class with a definition, a function with a body, an enumeration whose
	// enumerators have been read.
	bool defined;
	// 5.19: a value the translation knows, which an enumerator always has and
	// a const object of integral type has when its initializer is constant.
	bool constant;
	unsigned long long value;
};

// One line-oriented scope of the dump.
//
// The output is a tree of scopes whose shape is not the scope tree: a reopened
// namespace continues one node, an enumeration writes one per declaration, and
// an unscoped enumeration writes none at all.  Keeping the dump apart from the
// model lets each be what its own rules say, and lets a line be written as the
// declaration spells it while a use of the same type is written canonically.
struct DumpScope
{
	std::string header;
	std::vector<std::string> lines;
	std::vector<DumpScope*> children;
};

// A binding of one name in one region.
//
// 3.3.10p2 lets a class or enumeration name be hidden by a later declaration of
// the same name in the same region while staying reachable through an
// elaborated-type-specifier, so a binding holds both.
struct Binding
{
	Binding()
		: ordinary(nullptr)
		, tag(nullptr)
	{}

	SemaEntity* ordinary;
	SemaEntity* tag;
};

// One declarative region.
class Scope
{
public:
	Scope(ScopeKind scope_kind, Scope* enclosing, SemaEntity* scope_owner,
	      DumpScope* scope_dump, std::uint32_t scope_id)
		: kind(scope_kind)
		, parent(enclosing)
		, owner(scope_owner)
		, dump(scope_dump)
		, id(scope_id)
		, visit(0)
		, searchers_at(0)
	{}

	ScopeKind kind;
	Scope* parent;
	SemaEntity* owner;
	// The node a declaration written in this region writes its line to.
	DumpScope* dump;
	std::unordered_map<std::string, Binding> names;
	// The declarations of this region in order, which is what a class layout
	// and an anonymous union's member injection walk.
	std::vector<SemaEntity*> declarations;
	// The regions 7.3.4p2 makes the declarations of this one also appear in: a
	// using-directive's target, and an inline namespace member (7.3.1p8).
	std::vector<Scope*> nominated;
	// The regions that wrote those directives, which is the same relation read
	// the other way.
	std::vector<Scope*> nominated_by;
	// This region among the run's, which is how a fact about a pair of them is
	// keyed.
	std::uint32_t id;
	// Scratch of one walk: the walk that reached this region, so that one with
	// several paths into one namespace holds it once.
	std::uint64_t visit;

	// Every region a lookup written in which reaches this one's declarations,
	// which is the closure of 7.3.4p2 read from the region that declares rather
	// than from the region that asks.
	//
	// That way round is what makes it worth gathering: a name is declared in
	// one region and looked up from many, so one gathering answers every
	// lookup of it, while a closure per asking region would gather the same
	// namespaces again for each.  It is gathered only for a lookup a
	// using-directive has to answer, and it grows with the directives rather
	// than being gathered again: `searchers` holds the regions in the order the
	// walk reached them, `searcher_at` gives each its place, `expanded` says how
	// many of each one's directives the walk has followed, and `searchers_at`
	// how many of the run's it has seen.  A directive written afterwards
	// therefore costs the walk the edges it added and nothing for the rest.
	std::vector<Scope*> searchers;
	std::vector<std::uint32_t> expanded;
	std::unordered_map<Scope*, std::uint32_t> searcher_at;
	std::uint64_t searchers_at;

private:
	Scope(const Scope&);
	Scope& operator=(const Scope&);
};

// The entities, scopes and dump nodes of one translation unit.
class SemaModel
{
public:
	SemaModel();

	Scope& global() { return *global_; }
	const DumpScope& root() const { return *root_; }

	// A region enclosed by `parent`, writing its lines to `dump`.
	Scope& open(ScopeKind kind, Scope& parent, SemaEntity* owner, DumpScope* dump);
	DumpScope& open_dump(DumpScope& parent, const std::string& header);
	SemaEntity& create(SemaKind kind, const std::string& name, TypeId type);

	// The identifier a user-defined type is interned under, which is the
	// entity that declared it.
	std::uint32_t type_entity_id() { return ++type_entities_; }

	// The binding of `name` in `where` alone.
	SemaEntity* find(const Scope& where, const std::string& name,
	                 LookupKind filter) const;
	// Binds `name` in `where` to `entity`, which a using-declaration does to an
	// entity declared elsewhere.
	void bind(Scope& where, const std::string& name, SemaEntity& entity);
	// Records `entity` as declared in `where`, in declaration order.
	void declare_in(Scope& where, SemaEntity& entity);

	// 3.4.1: the innermost declaration of `name` visible from `where`.
	SemaEntity* lookup(Scope& from, const std::string& name, LookupKind filter);
	// 3.4.3: the declaration of `name` that `in` and everything its
	// declarations appear in have.
	SemaEntity* lookup_in(Scope& in, const std::string& name, LookupKind filter);

	// A using-directive in `where` naming `space`, which an inline namespace
	// and an unnamed one also write for themselves.
	void nominate(Scope& where, Scope& space);

	// The class or enumeration a type is: the entity whose declaration made
	// it, which is how a type reached through a typedef-name finds the region
	// its members are declared in.
	void own_type(TypeId type, SemaEntity& entity);
	SemaEntity* type_owner(TypeId type) const;

	// The region a name written before `::` looks into, following a
	// typedef-name or a namespace alias to what it names.
	Scope* region_of(const SemaEntity& entity) const;

private:
	// The regions that bind `name`, or null when no region does.
	const std::vector<Scope*>* declarers(const std::string& name) const;
	// 3.4: the one declaration of `name` there is, when the search reaches the
	// region that made it.  The search runs from `from` out to the region
	// before `stop`, which is `from` alone for a qualified lookup.
	SemaEntity* lookup_unique(Scope& from, const Scope* stop,
	                          const std::string& name, LookupKind filter,
	                          Scope& declarer);
	// 7.3.4p2: whether the declarations of `declaring` appear in `in`, which
	// they do when it is `in` itself or the using-directives written in `in`
	// reach it.
	bool reaches(Scope& in, Scope& declaring);
	// The regions whose declarations appear in `in`, gathered into `reached_`
	// when there are at most `budget` of them.  False when there are more,
	// which is when asking each region that declares the name is the cheaper
	// question.
	bool walk_reached(Scope& in, std::size_t budget);
	// The declaration of `name` in every region `in`'s declarations appear in,
	// where `regions` - what `declarers` answered - holds more than one, so
	// that 3.4p1 has to ask about each of them.
	SemaEntity* search_declarers(Scope& in, const std::string& name,
	                             LookupKind filter,
	                             const std::vector<Scope*>& regions);
	// Gathers `declaring.searchers`, following only the using-directives
	// written since the last gathering.
	void gather_searchers(Scope& declaring);
	// 3.4p1: the one entity a lookup that reached two declarations found, or
	// the error that they are two entities.
	static SemaEntity* merge_found(SemaEntity* found, SemaEntity* again);

	std::deque<Scope> scopes_;
	std::deque<SemaEntity> entities_;
	std::deque<DumpScope> dumps_;
	Scope* global_;
	DumpScope* root_;
	std::uint32_t type_entities_;
	std::unordered_map<TypeId, SemaEntity*> type_owners_;
	// The regions that bind each name, each once, in the order they first bound
	// it.
	//
	// 3.4p1 makes a lookup that reaches two declarations of a name in one
	// declarative region an error, so a lookup through using-directives has to
	// know where else the name is declared.  Answering that from the name
	// rather than by walking every namespace the directives reach is what keeps
	// a unit with many of them from costing one walk per lookup: a name
	// declared nowhere is answered without a walk, a name one region declares
	// is answered by asking whether the lookup reaches that region, and a name
	// several declare is answered from whichever is smaller, the regions that
	// declare it or the regions the lookup reaches.
	std::unordered_map<std::string, std::vector<Scope*> > declarers_;
	// The using-directives written so far, as the pair of region identifiers in
	// one word, so writing the same one twice costs a probe rather than a scan.
	std::unordered_set<std::uint64_t> nominations_;
	// The namespace each of those directives named, in order, which is how a
	// gathered set of searchers learns which of its regions were nominated
	// since it last looked.
	std::vector<Scope*> nominees_;
	// Scratch of one gathering: the places left to expand.
	std::vector<std::uint32_t> pending_;
	// Scratch of one bounded walk, kept between walks so that a lookup in a
	// region with no using-directive allocates nothing.
	std::vector<Scope*> reached_;
	std::uint64_t visit_;
};

// True when `entity` names a type: a class, an enumeration, a typedef-name or
// a template type parameter (3.4.4p2, 7.1.3p1, 14.1p3).
bool names_a_type(const SemaEntity& entity);
// True when `entity` names a namespace, which 7.3.1p3 and 7.3.2 ask for.
bool names_a_space(const SemaEntity& entity);

// Writes `scope` and everything under it, indenting two spaces per level.
void write_dump(std::ostream& out, const DumpScope& scope, unsigned depth);
