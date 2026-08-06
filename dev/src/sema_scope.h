#pragma once

#include <cstdint>
#include <deque>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sema_facts.h"
#include "type_model.h"

// The declarations, scopes and bindings of one translation unit.
//
// This is the first semantic layer: what a declaration introduced, which
// region it introduced it into, and the two lookups of 3.4 over the result.
// It knows nothing of syntax, so a later assignment that needs a fact about a
// declaration asks the model rather than the tree it was read from.

// What a name denotes (3.3, 3.4, 7.1.3, 7.2, 7.3).
// 11p1: the access specifier a member was declared under.
const unsigned char kPublicAccess = 0;
const unsigned char kProtectedAccess = 1;
const unsigned char kPrivateAccess = 2;

// 12.1 and 12.4: which special member function a declaration declares.  A
// constructor and a destructor have no name of their own that a lookup finds,
// they are reached from the class rather than through a binding, and the object
// file names them by what they are, so what they are is a fact the declaration
// carries.
const unsigned char kOrdinaryFunction = 0;
const unsigned char kConstructorFunction = 1;
const unsigned char kDestructorFunction = 2;

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
	// 13.1: the other declarations of this name in this region, in declaration
	// order.  A name is bound to the first of them and the rest are reached
	// from it, so collecting the candidates of a call costs one walk of the
	// ones there are.
	SemaEntity* next;
	// The last of that chain, held on the first, so a declaration joins it
	// without walking what is already there.
	SemaEntity* tail;
	// The region this declaration was made in, which is what tells a member of
	// a class from a variable of a block: 9.2p1 makes the first reachable only
	// through an object, and the region it was declared in is the one fact
	// about it that says so.  It is the region of the declaration rather than
	// of a binding, so a using-declaration or 9.5p1 injection leaves it alone.
	Scope* region;
	// 9.5p1: the object an anonymous union's member is a member of.  The union
	// has no name, so its members are reached through the object the union
	// declared rather than through one the program named.
	SemaEntity* storage;
	// Class: 12.1, the constructors of the class, as the chain a declaration of
	// an object chooses from by 13.3.1.3.  It is held on the class because a
	// constructor has no name a lookup reaches, and the chain is in declaration
	// order, with 12.1p5's implicit one the only member when no declaration
	// wrote any.
	SemaEntity* constructor;
	// Class: 12.4p1, the destructor of the class, held here for the same
	// reason: the end of an object's lifetime asks the class for it.
	SemaEntity* destructor;
	// Class: 10p1, the direct base class, which every object of this class
	// holds a subobject of.  It is the one fact the base-clause establishes,
	// and layout, 10.2 lookup, 11.2 access, 12.6.2 construction, 12.4
	// destruction and 4.10p3 conversion each read it rather than the syntax.
	// Null for a class no base-clause was written for.
	SemaEntity* base;
	// Class: 11.2p1, the access the base-specifier gave the base, which is what
	// caps how far a member inherited through it may be named from.
	unsigned char base_access;
	// Class: whether an object of this class holds nothing - no non-static data
	// member, and a base that is itself empty.  1.8p5 still gives it a size of
	// one byte on its own, and the ABI puts such a base subobject at offset
	// zero without giving the derived class any storage for it, so this is what
	// 9.2p13 asks about a base before it lays the members out after it.
	bool empty_class;
	// 12.1 and 12.4: which special member function this declaration declares,
	// as one of the `kOrdinaryFunction` constants.
	unsigned char special;
	// 12.3.1p2: a constructor declared `explicit`, which only
	// direct-initialization may choose.
	bool explicit_function;
	// 12.1/12.4 and the ABI: the ABI gives a constructor and a destructor an
	// entry point for a complete object and one for a base class subobject, and
	// this says whether anything in this translation unit ever ran it on a
	// complete object - which is every object but a base subobject, a member
	// subobject among them.  A special member only a base subobject asked for
	// is the base-object entry alone; one a complete object asked for is the
	// complete-object entry, with the base-object name given to the same body.
	bool complete_object_entry;
	// 8.5.1p1 and 12.1p4: whether the program itself wrote what this function
	// does, which `= default` and `= delete` do not.  It is what stops a class
	// with a constructor from being an aggregate.
	bool user_provided;
	// 8.4.3p1: a function definition written `= delete`, which every use of is
	// ill formed.
	bool deleted;
	// 8.4.2p1: whether the definition of this function is the one the standard
	// gives it rather than one the program wrote, which is true of an implicitly
	// declared special member and of one written `= default`.  It is what says
	// the definition is this translation unit's to generate on demand.
	bool defaulted;
	// 11p1: which of the three access specifiers this member was declared
	// under, which says who may name it.  A declaration that is not a member
	// keeps `kPublicAccess`, which every context may name.
	unsigned char access;
	// 8.5.1p1 and 12.6.2p8: whether the member declaration wrote a
	// brace-or-equal-initializer, which is what a constructor that does not
	// mention the member initializes it with - and what stops the class from
	// being an aggregate.
	bool default_initializer;
	// 8.5.1p1: whether an object of this class is initialized from a
	// braced-init-list by initializing its members from the clauses.
	bool aggregate;
	// 9.2p13: where in its class an object of this non-static data member
	// begins, in bytes.  Layout is settled once, where 9.2p2 completes the
	// class, so every later use of the member is one read rather than a walk
	// of the members declared before it.  9.6p2 gives a bit-field no address of
	// its own, so for one this is where the storage unit holding it begins.
	unsigned long long offset;
	// 7.6.2p1: the alignment an alignment-specifier on this declaration asked
	// for, or zero where it wrote none.  9.2p13 allocates the member at the
	// next address the stricter of this and its type's own alignment allows,
	// and 7.6.2p5 makes the class's alignment at least as strict.
	unsigned long long requested_align;
	// 9.6p1: whether the declaration wrote a width, which makes the member a
	// run of bits inside a storage unit rather than an object with an address.
	// The three facts below say nothing about a member no width was written
	// for, which every other data member is.
	bool bit_field;
	// 9.6p1: the width in bits the declaration wrote.  Zero is the unnamed
	// separator, which allocates nothing and only moves the cursor on.
	unsigned bit_width;
	// 9.6p2: where the field's bits begin inside the storage unit `offset`
	// names.  Layout guarantees the field does not leave that unit, so a read
	// or a write is one load, one shift and one mask.
	unsigned bit_offset;
	// 4.5p3: the type a value read out of the field is promoted to, which is
	// `int` wherever `int` represents every value the width allows.  It is the
	// type the storage unit is loaded and masked with, and it is settled here
	// because only the declaration knows the width.
	TypeId bit_access;
	// 7.1.2p2: whether the definition of this function may appear in more than
	// one translation unit, which 9.3p2 also gives a member function defined in
	// its class body and 12.1p5 an implicitly declared constructor.  It is what
	// says the definition binds weakly and is emitted only where it is used.
	bool inline_function;
	// 12.1p5: whether this constructor does nothing, so that an object it
	// initializes needs no call at all.  A constructor the program wrote, or one
	// with a member to initialize, is not one of these.
	bool trivial;
	// Whether this declaration is reached through an object of the class that
	// declares it: 9.2p1 for a data member, and 9.3.1p3 for a member function,
	// whose type carries that object as the first parameter its declarator did
	// not write.  9.4p1 makes a member declared `static` a member of the class
	// and not of an object, so the region alone does not say.
	bool object_member;
	// 14.1p1: the region this declaration's template parameters were declared
	// in, which is what an instantiation of it substitutes arguments for.  Null
	// for a declaration no template-declaration parameterises, which every
	// declaration of the PA12 subset but a template is.
	Scope* template_parameters;
	// 14.7p1: the template this declaration is a specialization of, and whether
	// the output has already written the declaration it stands for.  A
	// specialization is made by naming it rather than by a declaration the
	// program wrote, so it is bound to no name and is reached only from the
	// template-id or the call that asked for it.
	SemaEntity* primary;
	bool instantiated;
	// This entity among the run's, which is how a fact about it is keyed - the
	// same use `Scope::id` is put to for a region.
	std::uint32_t id;
	// 4.5p3: the type an unscoped enumeration is promoted to, which 7.2p5
	// makes the first of `int`, `unsigned int`, `long` and `unsigned long`
	// that represents every value the enumeration has.  It is known only once
	// the enumerators have been read, so it is held on the declaration rather
	// than in the type the declaration made.
	TypeId promotion;
	// 3.1p2: whether a declaration of an object also defines it.  An `extern`
	// declaration with no initializer declares the object without defining it,
	// which is the one thing that tells a use of a name in another translation
	// unit from the storage this one lays out.
	bool object_definition;
	// 7.5p1: whether the declaration was written inside a `"C"` linkage
	// specification, which is a fact about the declaration and not about its
	// type.  3.5p9 makes the name of an entity with C language linkage and
	// external linkage the name the object file carries, so only the analysis
	// can say what a backend must spell it as.
	bool c_linkage;
	// 3.5p3: whether this namespace-scope name has internal linkage, which a
	// `static` declaration gives it and which a `const` object has unless it
	// was declared `extern`.  It is what tells a symbol another translation
	// unit may reach from one that belongs to this one alone, and only the
	// declaration's own specifiers say it.
	bool internal_linkage;
	// The name the PA12 dump spells this entity with: a namespace-scope
	// declaration is written with the named namespaces around it, and
	// everything else with the name it was declared with.  It is built where
	// the declaration is read, so a use of the name costs no walk.
	std::string dump_name;
};

// One node of the PA12 semantic dump.
//
// PA11 writes a tree of scopes whose lines are all declarations, so a scope
// holds its lines and its child scopes apart.  PA12 writes resolved statements
// and expressions, which are ordered against each other and nest to any depth,
// so its node is the general one: a line, and the nodes written under it.
struct DumpNode
{
	std::string text;
	std::vector<DumpNode*> children;
	// What the line stands for, as typed facts rather than as the text it
	// spells them with.  A later assignment that lowers the resolved program
	// reads this and never parses `text`.
	SemaFact fact;
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
		, base(nullptr)
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
	// The `N::M::` the PA12 dump writes before a declaration of this region.
	// 7.3.1.1p1 leaves an unnamed namespace with no name to write, so its
	// members are spelled as the namespace around it spells its own.
	std::string prefix;
	// Scratch of one walk: the walk that reached this region, so that one with
	// several paths into one namespace holds it once.
	std::uint64_t visit;
	// 10.2p2: the region searched after this one and before the one around it,
	// which for a class with a base-clause is the region its base declares.
	// Every other region leaves it null, so a program with no inheritance pays
	// nothing for the question.
	Scope* base;
	// 11.3p6: the functions a friend declaration in this class first declared.
	// 7.3.1.2p3 makes each a member of the innermost enclosing namespace whose
	// name no ordinary lookup finds, and 3.4.2p2 makes it visible through the
	// class that declared it - so the class holds them and the namespace binds
	// nothing.  Empty for every region but a class one wrote a friend in.
	std::vector<SemaEntity*> friend_functions;
	// 11.3p6 again, read from the namespace: the head of the chain of
	// friend-declared functions of each name this region declares but does not
	// bind.  It is what a later namespace-scope declaration of the same
	// function finds, so the two declare one entity rather than two.
	std::unordered_map<std::string, SemaEntity*> hidden;

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
	// The `translation-unit` node the PA12 dump is rooted at.
	DumpNode& unit() { return *unit_; }
	const DumpNode& unit() const { return *unit_; }

	// A region enclosed by `parent`, writing its lines to `dump`.
	Scope& open(ScopeKind kind, Scope& parent, SemaEntity* owner, DumpScope* dump);
	DumpScope& open_dump(DumpScope& parent, const std::string& header);
	DumpNode& open_node(DumpNode& parent, const std::string& text);
	// Puts what `node` holds under a new line of its own, in place: `node` keeps
	// the place it already has among the lines its parent wrote, and what it
	// said moves into the one node it now holds.  That is what a conversion
	// written around an operand needs, and it costs neither a search of the
	// parent nor a second ordering of what it holds.
	DumpNode& wrap_node(DumpNode& node, const std::string& text);
	SemaEntity& create(SemaKind kind, const std::string& name, TypeId type);

	// 13.1: the declaration of one function name in one region whose parameter
	// type list is `signature`, or nothing when that region has none.  `head` is
	// the entity the name is bound to, so a redeclaration is found rather than
	// searched for and declaring the nth overload of a name costs what declaring
	// the first does.
	SemaEntity* overload_of(const SemaEntity& head, std::uint32_t signature) const;
	void hold_overload(const SemaEntity& head, std::uint32_t signature,
	                   SemaEntity& entity);
	// Forgets that pairing, which 7.3.1.2p3 asks for when a declaration leaves
	// the chain a friend declaration put it in for the one its name binds.
	void drop_overload(const SemaEntity& head, std::uint32_t signature);

	// 14.7.1p1: the specialization of `primary` for the template-argument list
	// `arguments`, or nothing when it has not been made yet.  Naming the same
	// specialization twice names one declaration, so the second naming is a
	// probe rather than a second substitution.
	SemaEntity* specialization_of(const SemaEntity& primary,
	                              std::uint32_t arguments) const;
	void hold_specialization(const SemaEntity& primary, std::uint32_t arguments,
	                         SemaEntity& entity);

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
	//
	// 3.4p2 lets a lookup that finds a function name associate more than one
	// declaration with it, and 7.3.4p3 lets it reach the declarations of several
	// namespaces at once.  `found`, when given, takes the chain each region that
	// contributed heads, in the order the search reached them; the declaration
	// returned is the first of them.  A lookup that asks for no set is one whose
	// caller needs a single declaration, and two regions are the error 3.4p1
	// makes them for it.
	SemaEntity* lookup(Scope& from, const std::string& name, LookupKind filter,
	                   std::vector<SemaEntity*>* found = nullptr);
	// 3.4.3: the declaration of `name` that `in` and everything its
	// declarations appear in have.
	SemaEntity* lookup_in(Scope& in, const std::string& name, LookupKind filter,
	                      std::vector<SemaEntity*>* found = nullptr);
	// A set of declarations for one use of a name to hold.  It belongs to the
	// lookup that found it rather than to any region, because a declaration's own
	// chain is a fact about the region that declared it and no lookup may relink
	// it.
	std::vector<SemaEntity*>& open_overloads();

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

	// 11.3p1: `granting` gave `friendly` - a function or a class - the reach
	// its own members have.  The relation is held here rather than on either
	// entity because a program with no friend declaration should pay nothing
	// for it: `has_friends` is what every access check asks first.
	void befriend(const SemaEntity& granting, const SemaEntity& friendly);
	bool befriended(const SemaEntity& granting,
	                const SemaEntity& friendly) const;
	bool has_friends() const { return !friendships_.empty(); }

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
	                             const std::vector<Scope*>& regions,
	                             std::vector<SemaEntity*>* found);
	// Gathers `declaring.searchers`, following only the using-directives
	// written since the last gathering.
	void gather_searchers(Scope& declaring);
	// 3.4p1 and 3.4p2: the one entity a lookup that reached two declarations
	// found, the set it found when both of them are functions, or the error that
	// they are two entities.
	static SemaEntity* merge_found(SemaEntity* found, SemaEntity* again,
	                               std::vector<SemaEntity*>* set);

	std::deque<Scope> scopes_;
	std::deque<SemaEntity> entities_;
	std::deque<DumpScope> dumps_;
	std::deque<DumpNode> nodes_;
	std::deque<std::vector<SemaEntity*> > overload_sets_;
	Scope* global_;
	DumpScope* root_;
	DumpNode* unit_;
	std::uint32_t type_entities_;
	std::unordered_map<TypeId, SemaEntity*> type_owners_;
	// The declarations of each overloaded name, keyed by the entity the name is
	// bound to and the parameter type list 13.1 tells two declarations apart by.
	std::unordered_map<std::uint64_t, SemaEntity*> overloads_;
	// The specializations made so far, keyed by the template and the
	// template-argument list they were made from.
	std::unordered_map<std::uint64_t, SemaEntity*> specializations_;
	// 11.3p1: the friendships granted so far, as the pair of entity
	// identifiers in one word, so asking whether one class befriended one
	// declaration is a probe.
	std::unordered_set<std::uint64_t> friendships_;
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

// 14.1p1: the region a declarator-id written in `scope` declares its name into.
// The region a template's parameters are declared in encloses only the
// declaration they parameterise, so the name that declaration introduces
// belongs to the region the template-declaration was written in, which is where
// a use of the template looks for it.
Scope& declaring_region(Scope& scope);

// Writes `scope` and everything under it, indenting two spaces per level.
void write_dump(std::ostream& out, const DumpScope& scope, unsigned depth);
void write_nodes(std::ostream& out, const DumpNode& node, unsigned depth);
