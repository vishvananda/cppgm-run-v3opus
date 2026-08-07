#pragma once

#include <cstdint>
#include <deque>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "parse_depth.h"
#include "sema_name.h"
#include "sema_scope.h"
#include "type_model.h"

struct AstNode;

// An expression that is not one of the constant expressions 5.19 defines, or
// not one of the subset of them PA11 evaluates.
//
// This is not by itself a program the assignment refuses: 5.19p3 makes a const
// object of integral type a constant only when its initializer is one, and a
// declaration whose initializer is an ordinary expression is well formed and
// declares an object that is not a constant.  It is what separates that from
// the errors an expression can also hold - a name that is declared nowhere, a
// type-id that names no type - which are facts about the program rather than
// about the value, and which every caller lets through.
class NotConstant : public std::runtime_error
{
public:
	explicit NotConstant(const std::string& what)
		: std::runtime_error(what)
	{}
};

// Which dump the walk writes.
//
// PA11 describes what a translation unit declares; PA12 describes what its
// function bodies mean.  Both read the same declarations from the same tree, so
// one walk serves both and the mode says only which tree of lines it fills and
// which of the two assignments' rules it holds the program to.
// PA15 adds a third: the same PA12 walk, holding the program to the same
// rules, but also recording on each line the typed facts a lowering reads.
enum class SemaDialect
{
	Types,
	Semantics,
	Lowering
};

// The PA11 semantic pass: one walk of a PA10 syntax tree that builds the
// scopes, declarations and types of a translation unit, and the dump of them.
//
// The walk is in source order and visits each node once.  A declaration is
// resolved where it is reached, against the scopes already built, so nothing is
// deferred and no construct is read twice.  What a declaration established
// stays in the model; the tree is not consulted again once the walk is past it.
class SemaAnalyzer
{
public:
	explicit SemaAnalyzer(SemaDialect dialect = SemaDialect::Types);

	// Analyses `unit`, a PA10 `translation-unit`.  Throws for a program the
	// assignment gives no meaning to.
	void run(const AstNode& unit);

	void write(std::ostream& out) const;

	// The resolved program of the unit just analysed, for a caller that lowers
	// it.  The tree of lines is the resolved procedural tree, each line
	// carrying the typed facts of `SemaFact`; the table answers what a type is.
	const DumpNode& resolved() const { return model_.unit(); }
	TypeTable& types() { return types_; }

private:
	// Where a declaration is read: the region it declares into, and the dump
	// node its lines are written to.  The two part company where the standard
	// and the output format do: a member function defined outside its class
	// declares into the class and writes its lines there, while an enumeration
	// defined outside its class writes its lines where it is written.
	struct Context
	{
		Context()
			: scope(nullptr)
			, dump(nullptr)
			, node(nullptr)
		{}

		Scope* scope;
		DumpScope* dump;
		// The PA12 node a declaration read here writes under, which at block
		// scope is the `simple-declaration` the statement opened.  Null where
		// the output has no line for what a declaration declares, which is
		// every member of a class.
		DumpNode* node;
	};

	// One analysed expression.
	//
	// `type` is what the operators see, so a reference is already removed from
	// it (5p5).  `spelled` is what the dump writes, which parts company with
	// `type` exactly where the standard makes a reference visible in the
	// result of an expression: a call of a function returning a reference, and
	// a cast to one.
	struct Value
	{
		Value();

		TypeId type;
		TypeId spelled;
		ValueCategory category;
		DumpNode* node;
		// 13.4 and 3.4p2: the declarations an unresolved function name denotes,
		// which a target type or a call's arguments choose between.  One region's
		// declarations of a name are the chain it heads, and 7.3.4p3 lets one
		// lookup reach the chains of several, so the set is a list of heads and
		// belongs to the lookup that found it.
		const std::vector<SemaEntity*>* functions;
		// 5.3.1p3: the line that name wrote, when `&` was written on it, so
		// that the target which chooses a declaration writes both the name and
		// the pointer to it.  Null when the value is the name itself.
		DumpNode* addressed;
		// 3.4 and 13.4: the id-expression a function name was written as.  The
		// line the output writes for it names the declaration the program
		// wrote rather than the one the lookup reached, so a name found
		// through a using-directive stays as it stands and a template-id keeps
		// the arguments it wrote.
		const AstNode* name;
		// The line this value wrote, as the two parts of it that are not the
		// category and the type: the node kind the format writes before them,
		// and the payload it writes after.  Holding them is what lets a
		// conversion that changes what the value denotes - a cast to a
		// reference, a null pointer constant - spell that one line again in the
		// place it stands, rather than the output being built in a second pass.
		const char* what;
		std::string payload;
		// 4.10p1: an integral constant expression prvalue that evaluates to
		// zero, which is the only integral operand a pointer accepts.
		bool null_constant;
		bool constant;
		unsigned long long value;
		// The declaration a name stands for, and the token an operator was
		// written with.  The line spells both; a lowering needs them as the
		// facts they are, so they travel with the value rather than being
		// read back out of the text.
		SemaEntity* entity;
		unsigned op;
		// 5p9: the type a built-in binary operator brings both operands to.
		TypeId operands;
		// 7.3.3p1 and 11.2p5: whether this operand is the object a member
		// function a using-declaration brought into its class is called on.
		// The class the name was written on is the naming class, so 4.10p3's
		// conversion to the base subobject the function belongs to is not one
		// the program wrote and the base-specifier's own access is not asked
		// about - which is what lets a private base's member be named.
		bool through_using;
	};

	// 13.3.3.1: how good the conversion of one argument is.  The ranks of
	// Table 12, plus what 13.3.3.2p3 and p4 need to tell two of one rank apart.
	struct Match
	{
		Match();

		bool viable;
		// 0 exact, 1 promotion, 2 conversion, 3 ellipsis.
		int rank;
		// 13.3.3.2p4: a conversion to `bool` from a pointer loses to one that
		// keeps the pointer.
		bool to_bool;
		// 13.3.3.2p3: how a reference parameter bound its argument.
		bool reference;
		bool binds_rvalue_ref;
		bool binds_lvalue;
		// 13.3.3.2p3: the type the sequence produced where all it did to the
		// argument was qualify it - the pointer a qualification conversion made
		// of it, or the type a reference bound it as.  Two sequences that differ
		// only in this are ordered by whose qualifiers are the fewer, which is
		// what tells `f()` from `f() const` apart on an object of either.
		TypeId qualified;
		// The temporary a reference parameter had to convert its argument into,
		// which the dump writes as a cast.
		TypeId materialized;
		// 4.10p3 and 13.3.3.1.4p1: the base class this sequence converted the
		// argument to, which is what the tree has to name the subobject of and
		// what 13.3.3.2p4 orders two such sequences by.
		SemaEntity* to_base;
		// 13.3.3.1.2p1: the converting constructor this sequence calls, when
		// the argument reaches the parameter's class only through one.  The
		// temporary it makes is what the parameter is given.
		SemaEntity* converting;
	};

	// Where the object an initialization or a destruction acts on stands.
	// 8.5 initializes an object a declaration named; 12.6.2 initializes a
	// non-static data member of the object the constructor is running on, and
	// 12.6.2p5 that object's base class subobject.  The three differ only in how
	// the action names the object, so one path writes all three.
	enum class Placement
	{
		Named,
		Member,
		Base
	};

	// 12.2p1: what asked a conversion for the temporary it made, which is what
	// names the storage the function gives that temporary.  An argument of
	// class type is a copy the call owns (5.2.2p4) and a returned prvalue is
	// the object the caller reads (6.6.3p2), so each names its own storage;
	// every other place reads the object the expression already wrote, which
	// keeps the name it was given where it was written.
	enum class Requested
	{
		Written,
		Argument,
		Returned
	};

	// The prefix `Requested` gives a temporary an argument conversion made.
	// `reference` says the place binds a reference to it rather than holding
	// the object itself, which 8.5.3p5 makes storage of the argument and not an
	// object the call is passed.
	static const char* requested_prefix(Requested by, bool reference);

	// The terminals a declaration was written from, which is what names an
	// unnamed class that no declarator names (9.5p2).
	struct Span
	{
		std::uint32_t begin;
		std::uint32_t end;
	};

	// A `decl-specifier-seq` or `type-specifier-seq` as read.
	struct Specifiers
	{
		Specifiers();

		unsigned counted[kSimpleTypeSpecifierCount];
		unsigned builtins;
		unsigned cv;
		TypeId type_name;
		bool has_type_name;
		bool is_typedef;
		bool is_constexpr;
		// 3.1p2 and 7.1.1: an `extern` declaration with no initializer is not
		// a definition of the object it declares.
		bool is_extern;
		// 9.4p1: a member declared `static` is not a member of an object, so it
		// has no implicit object parameter and is reached without one.
		bool is_static;
		// 7.1.2p2: the definition of this function may be written in more than
		// one translation unit, so no one unit owns the one the program has.
		bool is_inline;
		// 3.7.2p1 and 7.1.1p1: the variable this sequence declares has thread
		// storage duration - one object per thread rather than one per program.
		bool is_thread_local;
		// 11.3p1: the declaration grants this class's access rather than
		// declaring a member of it, so what it declares belongs to the region
		// around the class and not to the class.
		bool is_friend;
		// 7.1.1p10: the non-static data members this sequence declares are not
		// const however const the object holding them is, so a const member
		// function may write one and 5.3.1p3 hands back a pointer to
		// non-const.
		bool is_mutable;
		// 7.1.6.4p1 and 8.3.5p2: the sequence is the one type-specifier `auto`,
		// which names no type of its own and stands for the one a
		// trailing-return-type writes after the declarator-id.
		bool is_auto;
		// 7.6.2p1: the strictest alignment an alignment-specifier of this
		// sequence asked for, or zero where it wrote none.
		unsigned long long alignment;
		// The class or enumeration this sequence declared.
		SemaEntity* introduced;
	};

	// One parameter of a parameter-clause, before 8.3.5p4 drops a lone `void`.
	struct Parameter
	{
		Parameter()
			: type(kNoType)
			, initializer(nullptr)
		{}

		std::string name;
		TypeId type;
		// 8.3.6p1: the default-argument this parameter was written with, which
		// a call that omits the argument uses in its place.
		const AstNode* initializer;
	};

	// A definition the dump writes at the end of the translation unit.
	//
	// 12.1p5 gives a class a constructor no declaration wrote, and 9.2p2 makes
	// a member function's body a complete-class context, which is read after
	// the class it is written in is closed.  Both are definitions the program
	// has that the place they are written cannot hold, so they are held here
	// and written where the output puts them.
	struct Pending
	{
		Pending();

		SemaEntity* function;
		// The implicit object parameter of 9.3.1p3, which the dump writes as
		// the first parameter of a member function.
		SemaEntity* self;
		// A member function's declarator and body, and the region its
		// parameters were declared in.  Null for a constructor no declaration
		// wrote, whose body is empty.
		const AstNode* body;
		Scope* scope;
		std::vector<Parameter> parameters;
		// 12.6.2: the ctor-initializer this constructor was written with, and
		// the class whose members it initializes.  Both are null for a function
		// that is not a constructor; `members` alone is set for the constructor
		// 12.1p5 gives a class, whose initializations are all implied.
		const AstNode* initializers;
		Scope* members;
		// 14.7.1: a specialization, which is a declaration the program did not
		// write rather than a definition it did, and which the output writes as
		// the declaration with the parameters of the template it was made from.
		bool instantiation;
	};

	// 12.6.2: one mem-initializer of a ctor-initializer, indexed by the name
	// its mem-initializer-id ends in.  `used` says a member of that name was
	// reached, which 12.6.2p2 is what makes the mem-initializer-id name
	// something; `spelled` is what the diagnostic names when it does not.
	struct MemInitializer
	{
		MemInitializer()
			: written(nullptr)
			, used(false)
		{}

		const AstNode* written;
		std::string spelled;
		bool used;
	};

	// A value of the 5.19 subset: what it is worth, and the type that says how
	// wide it is and whether it is signed.
	struct Constant
	{
		Constant()
			: type(kNoType)
			, bits(0)
		{}

		TypeId type;
		unsigned long long bits;
	};

	// Declarations (sema_analyzer.cpp).
	void declaration(const AstNode& node, const Context& ctx);
	void namespace_definition(const AstNode& node, const Context& ctx);
	void namespace_alias(const AstNode& node, const Context& ctx);
	void using_directive(const AstNode& node, const Context& ctx);
	void using_declaration(const AstNode& node, const Context& ctx);
	// 7.3.3p1: the members a using-declaration written in a class declares -
	// one per declaration `named` heads - and 7.3.3p14's rule that a
	// declaration the class itself made of the same name and parameter list
	// hides the one the base made, so it is not brought in at all.
	void declare_using_members(SemaEntity& named, Scope& where,
	                           const std::string& name);
	// One of them: a declaration of this class naming what the base declared.
	SemaEntity& declare_using_member(SemaEntity& target, Scope& where,
	                                 const std::string& name);
	// 12.9p1: the constructors a using-declaration naming a direct base class's
	// constructors declares in `where`, one per member of the base's candidate
	// set of inherited constructors other than the ones 12.9p3 leaves out.
	void inherit_constructors(SemaEntity& base, Scope& where);
	// One of that set: the first `taken` parameters of `from`, which is the
	// whole of its parameter-type-list when `whole`.
	void inherit_constructor(SemaEntity& from, const SemaEntity& base,
	                         std::size_t taken, bool whole, SemaEntity& derived,
	                         Scope& where, const std::string& spelled);
	// 12.9p1 and 8.3.6p4: how many parameters a call of `function` has to write,
	// which is where its default-arguments begin - the shortest parameter list
	// 12.9p1's candidate set takes from this declaration.
	std::size_t required_parameters(const SemaEntity& function) const;
	// 12.1p5 and 12.9p3: whether this class declares a constructor of its own,
	// which an inherited one is not - so a class that only inherits still has
	// the default constructor 12.1p5 gives it.
	static bool declares_own_constructor(const SemaEntity& entity);
	// 13.1 and 7.3.3p14: what tells two declarations of one name in one class
	// apart - 8.3.5p4's parameter-type-list and 8.3.5p7's cv-qualifier-seq.
	// 9.3.1p3 put an object parameter in a non-static member function's type
	// that no declarator wrote, so it is left out of the list and its
	// qualifiers stand beside it; a static member function wrote no
	// cv-qualifier-seq and declares the list its declarator wrote.  That is
	// what makes a static and a non-static declaration of one class hide each
	// other rather than overload, which 9.4.1p2 does not allow.
	std::uint32_t member_signature(const SemaEntity& function);
	std::uint32_t member_signature(TypeId type, bool object_member);
	// 7.3.3p1: the declaration a name a using-declaration brought into a class
	// reaches, which is the one the base made.  11p1's access and 7.3.3p14's
	// hiding are facts about the declaration the class made; everything a use
	// of the member needs is a fact about the one it names.
	static SemaEntity& declared_member(SemaEntity& entity)
	{
		return entity.shadowed != nullptr ? *entity.shadowed : entity;
	}
	static const SemaEntity& declared_member(const SemaEntity& entity)
	{
		return entity.shadowed != nullptr ? *entity.shadowed : entity;
	}
	void alias_declaration(const AstNode& node, const Context& ctx);
	void static_assert_declaration(const AstNode& node, const Context& ctx);
	void template_declaration(const AstNode& node, const Context& ctx);
	void template_parameter(const AstNode& node, const Context& ctx);
	void simple_declaration(const AstNode& node, const Context& ctx);
	void condition_declaration(const AstNode& node, const Context& ctx);
	// One declarator of a declaration, with the initializer written for it.
	void init_declarator(const AstNode& node, const AstNode* initializer,
	                     const Specifiers& specifiers, const Context& ctx);
	// 8.3.5 and 13.1: one declarator of a declaration that declares a function,
	// from the point its type is known.  `granting` is the class a friend
	// declaration gives this declaration the access of, and null for every
	// other declaration.
	void declare_function_declarator(const AstNode& node,
	                                 const std::string& name, TypeId type,
	                                 const QualifiedName& spelled,
	                                 const Specifiers& specifiers,
	                                 const Context& target,
	                                 SemaEntity* granting,
	                                 std::vector<Parameter>& spelled_parameters);
	void function_definition(const AstNode& node, const Context& ctx);
	void statement(const AstNode& node, const Context& ctx);
	SemaEntity& class_declaration(const AstNode& node, const Context& ctx,
	                              const Span& span, bool define,
	                              const std::string& named_by);
	SemaEntity& enum_declaration(const AstNode& node, const Context& ctx,
	                             bool elaborated, const std::string& named_by);
	void enumerators(const AstNode& node, SemaEntity& entity,
	                 const std::string& spelling, DumpScope& dump);
	// 7.1.3p2: the name the first declarator of a declaration gives a class or
	// enumeration its specifiers left unnamed.
	static std::string name_from_declarators(const AstNode& node);
	// 9.5p1: the members of an anonymous union are declared in the region the
	// union is declared in, and are members of the object the union declared.
	// Anything else `entity` may be declares nothing there, so every
	// declaration that introduces a class asks.
	void inject_union_members(SemaEntity* entity, const Context& ctx,
	                          const Span& span);
	// 12.1 and 12.4: a constructor or destructor a class body declares, which
	// is a member declaration with no decl-specifier-seq whose name is the
	// class's own.  It declares into the class and, where a body is written,
	// leaves the definition for the end of the translation unit as 9.2p2 does
	// for every member function defined in its class.
	void special_member(const AstNode& node, const Context& ctx);
	// 10p1: the base-clause of a class definition, read before its members
	// because they are read against what its base declares.
	void read_base_clause(const AstNode& node, SemaEntity& entity, Scope& scope,
	                      const Context& ctx, const std::string& header);
	// 12.1p5 and 12.4p3: the constructor and the destructor a class with no
	// declared one has, declared into the class where its definition ends.
	// 9.2p2: the special members the class has where its definition ends.
	void declare_special_members(SemaEntity& entity, Scope& scope);
	void declare_constructor(SemaEntity& entity, Scope& scope);
	void declare_destructor(SemaEntity& entity, Scope& scope);
	// 12.1p5 and 8.5p6: the constructors of the class `type`, as the chain
	// 13.3.1.3 chooses from, and the destructor 12.4p3 gives it.  Null for
	// anything that is not a class type this unit completed.
	SemaEntity* class_constructors(TypeId type);
	// 8.5p7: whether the program wrote a constructor of the class whose
	// declarations `head` begins, which is what says a value-initialized object
	// of it is not zero-initialized first.
	static bool user_provided_constructor(const SemaEntity& head);
	// 13.3.3.1.2p1: the converting constructor of `target` that a user-defined
	// conversion sequence for `argument` calls, or null where none does or
	// where two do equally well.
	SemaEntity* converting_constructor(const Value& argument, TypeId target);
	SemaEntity* class_destructor(TypeId type);
	// 8.5.1p1: whether an object of `type` is initialized from a
	// braced-init-list by initializing its members with the clauses.
	bool aggregate_type(TypeId type);
	// 12.1p5: whether default-initializing or destroying an object of the class
	// `scope` declares does nothing, so that no call has to be made for one.
	bool trivial_default_construction(Scope& scope);
	bool trivial_destruction(Scope& scope);
	// 12.6.2: the member initializations of one constructor, in the declaration
	// order 12.6.2p10 gives them whatever order the mem-initializers were
	// written in.  Each is written under the constructor's own definition.
	void write_member_initializations(const Pending& pending, DumpNode& line,
	                                  const Context& inner);
	// 8.5.1p2: what the constructor an aggregate class was given does - each
	// member initialized with the parameter of the same name.
	void write_member_parameters(const Pending& pending, DumpNode& line);
	// 12.1p5: whether the definition the standard would give this class's
	// default constructor is one it cannot write, which deletes the
	// constructor.
	bool undefinable_default(Scope& scope);
	// 12.6.2p2: whether a mem-initializer-id names the base class rather than a
	// non-static data member.
	bool names_the_base(const std::string& written, const SemaEntity& base,
	                    const Context& ctx);
	// 12.4p8: the destructor calls for the members of the class a destructor
	// belongs to, in reverse declaration order, written after its body.
	void write_member_destructions(Scope& members, DumpNode& line);
	// 3.8p1 and 12.4p3: the destructor call that ends the lifetime of the
	// object `entity` names, or nothing where its class has no destructor to
	// run.  `member` says the object is a member of the one being destroyed
	// rather than an object a declaration named.
	void destructor_action(SemaEntity& entity, DumpNode& parent,
	                       Placement where);
	// 9.3.2p1: the type of `this` in the body of the member function `function`.
	TypeId this_type(const SemaEntity& function);
	// 3.7.1: records the object a definition declares with whichever region
	// 3.8p1 makes the end of its lifetime an action of, which its storage
	// duration is what says.
	void record_lifetime(SemaEntity& entity, const Context& target,
	                     DumpNode& line);
	// 3.5, 3.7.1 and 3.7.2: the linkage a variable's name has and the storage
	// duration its object has, settled from this declaration's specifiers and
	// from the declaration of the same variable this region already holds.
	void record_storage(SemaEntity& entity, const SemaEntity* prior,
	                    const Specifiers& specifiers, const Context& target,
	                    TypeId type);
	// The objects an open block has declared whose destructors run when control
	// leaves it, innermost frame last.
	std::vector<std::vector<SemaEntity*> > lifetimes_;
	// Holds one frame while a block is read, and writes its destructor actions
	// where the block ends.
	void open_lifetimes();
	void close_lifetimes(DumpNode& line);
	// 6.6p2 and 3.8p1: a jump leaves every block it jumps out of, so it runs
	// the destructors of the objects all of them declared, innermost first.
	// `depth` is the first frame the jump does not leave: 0 for a return, which
	// leaves the whole function, and the frame the statement itself opened for
	// a break or a continue.
	void leave_lifetimes(std::size_t depth, DumpNode& line);
	// 6.6.3p2: a return leaves every block between it and the function.
	void unwind_lifetimes(DumpNode& line);
	// 3.8p1: whether any block still open holds an object whose lifetime ends
	// with a call, which is what a jump this milestone cannot place those calls
	// for has to be refused for.
	bool lifetimes_pending() const;
	// 12.4p3: whether the end of this object's lifetime is a call.
	bool ends_in_call(const SemaEntity& entity);
	// 12.1p5: whether default-initializing an object of `type` does nothing at
	// all, so that a subobject of it needs no action written.
	bool trivially_constructed(TypeId type);
	// 3.6.3p1: the namespace-scope objects this unit constructed, whose
	// destructors run in reverse order when the program ends.
	std::vector<SemaEntity*> static_lifetimes_;
	// 3.7.2p2: the namespace-scope objects with thread storage duration this
	// unit constructed, and the line each was declared on.  One of these is
	// destroyed when its own thread ends rather than when the program does, so
	// the action stands under the declaration - where the initialization that
	// hands it to the runtime is - and is written once that declaration has
	// written everything else it holds.
	struct ThreadLifetime
	{
		SemaEntity* entity;
		DumpNode* line;
	};
	std::vector<ThreadLifetime> thread_lifetimes_;
	// 8.5.1p1: whether the class `scope` declares is an aggregate.
	static bool aggregate_class(Scope& scope);
	// 8.5.1p2: the initializer-clauses of one braced-init-list, and how many of
	// them the subobjects read so far have taken.  8.5.1p11 lets one list
	// initialize subobjects at several depths, so the cursor is what the walk of
	// the aggregate carries rather than a list per level.
	struct Clauses
	{
		Clauses(const AstNode& written)
			: list(&written)
			, at(0)
		{}

		const AstNode* list;
		std::size_t at;

		bool spent() const;
		const AstNode& next() const;
	};

	// 8.5.1: `type` initialized from what is left of `clauses`, writing one
	// `subobject-initialization` per member or element under `parent`.  A
	// subobject no clause reaches is value-initialized, which 8.5p7 makes the
	// zero of its type and which the node with no clause under it says.
	void aggregate_subobject(TypeId type, Clauses& clauses, const Context& ctx,
	                         DumpNode& node);
	void aggregate_members(TypeId type, Clauses& clauses, const Context& ctx,
	                       DumpNode& parent);
	void aggregate_elements(TypeId array, Clauses& clauses, const Context& ctx,
	                        DumpNode& parent);
	// 8.5.1p2 and 8.5.1p7: a subobject of class type is copy-initialized from
	// the clause that reached it, and value-initialized where none did.  Either
	// is one object built where it stands, so what the node carries is the
	// `constructor-action` a declaration of the same object would carry - or,
	// where 12.8p31 elides it, the value it holds a copy of.
	void construct_subobject(TypeId type, const AstNode* written,
	                         const Context& ctx, DumpNode& node,
	                         bool value_init);
	// 8.5.1p2 and 13.3.1.7: the constructor an object of the aggregate class
	// `type` is built by where it is an object of its own, declared once and
	// held on the class.  Null where the class is no aggregate, and where a
	// member of it is no object a by-value parameter can hold - which leaves
	// the initialization to the clauses themselves.
	SemaEntity* member_constructor(TypeId type);
	// 8.5.1p2: one call of that constructor, whose arguments are what the
	// clauses of `list` initialize the members with.
	void construct_from_members(SemaEntity& constructor, const AstNode& list,
	                            const Context& ctx, DumpNode& parent);
	// 8.5.1p11: whether the clause initializes the whole subaggregate rather
	// than the first of its members, which is what says the braces around it
	// were left out.  Only a value of the subaggregate's own class type - or of
	// one derived from it - initializes it on its own.
	bool clause_initializes_class(TypeId type, const AstNode& clause,
	                              const Context& ctx);
	// 8.5.2p1: an array of character type initialized by a string literal,
	// whose elements are the code units the literal holds.
	bool string_initialized(TypeId array, Clauses& clauses, const Context& ctx,
	                        DumpNode& parent);
	// 8.5.1p2: the clauses of a nested braced-init-list, which initialize this
	// subobject alone and must all be taken by it.
	void aggregate_from_list(TypeId type, const AstNode& list,
	                         const Context& ctx, DumpNode& node);
	// The `subobject-initialization` node of one member or one element.
	DumpNode& open_subobject(DumpNode& parent, TypeId type,
	                         const SemaEntity* member,
	                         unsigned long long index);
	// 11.2: whether a context in `from` may name `member`, and the error that
	// it may not.
	// `naming_class` is 11.2p5's class the name was written on, whose own
	// bases may grant what the declaring class does not; null where the name
	// was not written on an object.
	bool accessible(const SemaEntity& member, const Scope* from,
	                const Scope* naming_class = nullptr) const;
	void require_access(const SemaEntity& member, const Scope* from,
	                    const Scope* naming_class = nullptr);
	// 11p6: the class a declaration written outside it names a member of, whose
	// access every name of that declaration is checked with - the leading return
	// type of an out-of-class member function and the initializer of a static
	// data member alike.  Null for a declaration that names no member.
	Scope* naming_context(const std::string& written, const Context& ctx);
	// Holds one while a declaration is read, and puts back the one that was
	// there, because a declaration may be read while another is.
	struct Naming
	{
		Naming(SemaAnalyzer& owner, Scope* region);
		~Naming();

		SemaAnalyzer& owner;
		Scope* held;
	};
	// 5.2.5p1: whether evaluating an analysed expression does nothing a program
	// can observe, so that a member access which turns out to name no subobject
	// may leave the object expression out of the resolved tree, and the error
	// that it may not.
	bool observable(const DumpNode& node) const;
	void require_droppable(const DumpNode& object, const std::string& member);
	// 8.3.4p1: the type of one element of an array, however many dimensions it
	// has, and the type itself for anything else.
	TypeId element_of(TypeId type);
	// 12.6p1: whether a declaration of an array of class type creates its
	// elements by constructing each of them, which every form but one that
	// wrote a clause for an element does.
	bool element_constructed(TypeId type, const AstNode* written);
	// 8.5 and 13.3.1.3: the `constructor-action` an object of class type is
	// initialized by, and the definition of the constructor it calls.
	// `written` is the initializer the program wrote, or null for an object
	// with none; `member` says the object is a non-static data member of the
	// one the constructor being written is initializing, so that the action
	// names it through `this` rather than by a name of its own.
	// `copied` says the initializer was written with `=`, which 8.5.4p3 makes
	// an initialization no `explicit` constructor may answer.
	// `given` is an initializer already analysed where it was written, which
	// 13.3.3.1.2's conversion has and no source form does: the value is taken
	// as it stands and its line moves into the place the call gives it.
	// `value_init` says the initializer was an empty list the grammar wrote no
	// node for, which is what 5.2.3p2's `T()` is.
	// `forwarded`, where given, are the parameters whose values 12.9p8 passes
	// to the constructor of the base subobject an inheriting constructor
	// initializes, in place of an initializer the program wrote.
	void construct_object(SemaEntity& variable, DumpNode& line,
	                      const AstNode* written, const Context& ctx,
	                      Placement where = Placement::Named,
	                      bool copied = false, const Value* given = nullptr,
	                      bool value_init = false,
	                      const std::vector<SemaEntity*>* forwarded = nullptr);
	// 5.1.1p1: a parameter named as the program would name it, written into a
	// node of its own under `parent`.
	Value parameter_value(SemaEntity& parameter, DumpNode& parent);
	// 12.1p5, 12.9p6 and 3.2p3: the definition a use of a constructor the
	// standard rather than the program gives a class asks this unit for, which
	// is written once however many uses ask.
	void demand_constructor_definition(SemaEntity& constructor);
	// The object a constructor-action runs on, as the address of it: an object
	// a declaration named, a member of the object being constructed, or that
	// object's base class subobject.
	void write_constructed_object(SemaEntity& variable, DumpNode& call,
	                              Placement where, Value& object,
	                              TypeId object_type);
	// 12.2p1 and 5.2.3p2: a prvalue of class type is an object, so the function
	// it is written in has to hold one.  This declares that object - which no
	// name reaches - runs the constructor `written` chooses on it, and hands
	// back the prvalue as the object it now stands for.  `prefix` names the
	// storage it is given, which is what says whether an argument asked for it.
	Value materialize_temporary(TypeId type, const AstNode* written,
	                            const Context& ctx, DumpNode& parent,
	                            const char* prefix, bool value_init = false);
	// The same, written into a line that already stands where the prvalue does,
	// which is what an argument conversion needs: the argument keeps the place
	// it had among the operands of the call and the temporary is written around
	// it rather than beside it.
	Value build_temporary(TypeId type, DumpNode& line, const AstNode* written,
	                      const Value* given, const Context& ctx,
	                      const char* prefix, bool value_init);
	// 8.5.3p5 and 13.3.3.1.2: a temporary an argument conversion made is named
	// after the argument it was made for, unless something already read it as
	// the object it is.
	void name_argument_temporary(const Value& value, const char* prefix);
	// The typed facts of a node the analysis builds rather than reads out of an
	// expression the program wrote.
	static void set_fact(DumpNode& node, FactKind kind, TypeId type,
	                     ValueCategory category);
	// 9.3.1p3: the type of a member function, which is called on an object that
	// its declarator does not write and that the dump writes as its first
	// parameter.  Any other function is its own type.
	// 9.4.1p2: `static` stands on the declaration written in the class and is
	// not repeated on a definition written outside it, so a qualified
	// declarator is told which kind of member it declares by the declaration it
	// redeclares.
	TypeId with_object_parameter(TypeId type, const AstNode& declarator,
	                             const Context& target, bool is_static,
	                             const std::string& name, bool qualified);
	// 9.4p1: whether `where` declares `name` as a static member function whose
	// declarator wrote `type`.
	bool declares_static_member(Scope& where, const std::string& name,
	                            TypeId type);
	// 12.8p1: whether the program wrote a copy constructor of the class
	// `entity` whose members `scope` declares, which is what says a copy of an
	// object of it is not the copy of its bytes.
	bool declares_copy_constructor(const SemaEntity& entity, Scope& scope);
	// 12.8p2: the class one member is copied as, which for an array member is
	// its element type.
	TypeId member_copy_type(TypeId type);
	// The definitions the output writes at the end of the translation unit,
	// which are read in the order they were asked for.  Reading one may ask for
	// another, so the list is walked rather than iterated.
	void write_pending_definitions();
	void write_definition(Pending& pending);
	// 9.2p2 and the course ABI: the size and alignment of a completed class,
	// with `requested` the alignment 7.6.2 asked for, or zero for none.
	void lay_out_class(SemaEntity& entity, Scope& scope, bool is_union,
	                   unsigned long long requested);
	// 9.6p2: the storage unit the bit-fields of a class are being allocated
	// into while it is laid out.  A unit is opened by the first field that
	// cannot share the one before it, and the fields declared with its own type
	// share it while their bits fit; anything else closes it.
	struct BitUnit
	{
		BitUnit()
			: open(false)
			, type(kNoType)
			, at(0)
			, used(0)
		{}

		bool open;
		TypeId type;
		unsigned long long at;
		unsigned long long used;
	};
	// 9.6p2: the share of a storage unit one bit-field takes, from the byte the
	// layout has reached and the unit it is filling.
	void lay_out_bit_field(SemaEntity& member, unsigned long long& at,
	                       BitUnit& unit);
	// 9.6p1: the members one member-declaration that wrote widths declares.
	void bit_field_declaration(const AstNode& node, const Context& ctx);
	// 9.6p2: the type the storage unit of a bit-field of this type is read and
	// written at, which is the signed integer of the unit's own width.
	TypeId bit_field_access_type(TypeId declared);
	// 7.6.2p1: the strictest alignment the class-head's alignment-specifiers
	// asked for.
	unsigned long long requested_alignment(const AstNode& node,
	                                       const Context& ctx);
	// 13.1 and 3.5: the declaration a function declarator makes, which is a
	// redeclaration of an earlier one in the same region exactly when their
	// parameter type lists agree.
	SemaEntity& declare_function(const std::string& name, TypeId type,
	                             const Context& target, bool define,
	                             bool hidden = false,
	                             bool object_member = false);
	// 13.1 and 9.3.1p3: the key the chain a name heads is indexed by, which is
	// the parameter-type-list the declarator wrote wherever the region is a
	// class - so a static and a non-static member function whose types agree
	// only because one carries the object parameter are two declarations.
	std::uint32_t declaration_signature(const Scope& where, TypeId type,
	                                    bool object_member);
	// 7.3.3p14: a member function this class declares hides the one a
	// using-declaration brought in from a base with the same name and
	// parameter-type-list rather than conflicting with it, so what was brought
	// in leaves the chain the name heads.  It is asked where 9.2p2 completes
	// the class, because it is a question about what the complete class
	// declares and not about which of the two the body wrote first - one pass
	// over the declarations each brought-in name has, and nothing at all for a
	// class that wrote no using-declaration.
	void hide_using_members(Scope& where);
	// 11.3p6: a friend declaration declares its function in the innermost
	// enclosing namespace, so a declarator written after `friend` is read
	// against that region rather than against the class it stands in.  Returns
	// the class the declaration grants access to, and leaves `target` naming
	// the region the function is declared in.
	SemaEntity* friend_target(const Context& ctx, const QualifiedName& spelled,
	                          Context& target);
	// 11.3p6 and 7.3.1.2p3: the declaration a friend declaration made visible,
	// which a later namespace-scope declaration of the same function is.  The
	// entity moves from the region's hidden chain into the one its name binds,
	// so the program has one function rather than two.
	void reveal_friend(Scope& where, const std::string& name,
	                   SemaEntity& entity, std::uint32_t signature);
	// 11.3p2: `friend C;` and `friend class C;` grant this class's access to
	// the class the specifiers named, which declares nothing.
	void grant_class_friendship(const Context& ctx,
	                            const Specifiers& specifiers);
	// 3.3.6 and 7.3.1.2p3: the innermost namespace a region is written in,
	// which is what a friend declaration declares into however deeply the class
	// it is written in is nested.
	static Scope& friend_namespace(Scope& scope);
	// 11.3p1: the innermost class around a friend declaration, which is the one
	// that grants what the declaration grants.
	SemaEntity* granting_class(const Context& ctx) const;
	// 11.3p1: whether a name read in `from` reaches what `granting` declared
	// because `granting` befriended what owns `from`.
	bool befriended(const Scope& granting, const Scope& from) const;
	// The `parameter` lines of a function, and the declarations of them in the
	// region the definition opened.  `written` is how many parameters of the
	// function type the declarator did not write, which is the implicit object
	// parameter of a member function.
	void declare_parameters(const std::vector<Parameter>& parameters,
	                        TypeId type, const Context& inner, DumpNode* node,
	                        std::size_t implicit = 0);
	// The entity the declaration of a name reuses, or nothing when the name is
	// new to the region.
	SemaEntity* redeclared(const Context& ctx, const std::string& name,
	                       SemaKind kind);
	// A dump node that also says which construct it stands for, for the
	// statement and declaration lines whose kind is fixed where they are
	// written rather than carried up in a `Value`.
	DumpNode& open_fact(DumpNode& parent, const std::string& text,
	                    FactKind kind);
	void write_line(DumpScope& dump, const char* what, const std::string& name,
	                TypeId type);
	void write_entity_line(DumpScope& dump, const SemaEntity& entity);

	// Specifiers, declarators and names (sema_declarator.cpp).
	Specifiers read_specifiers(const AstNode& seq, const Context& ctx,
	                           const Span& span, bool declaration,
	                           const std::string& named_by);
	void read_type_specifier(const AstNode& node, Specifiers& out,
	                         const Context& ctx, const Span& span,
	                         const std::string& named_by);
	TypeId specifier_type(const Specifiers& specifiers);
	TypeId type_id_type(const AstNode& node, const Context& ctx);
	// 8.3: the type `node` derives from `base`, the name it declares, and - for
	// a caller that goes on to open the region of a function definition - the
	// parameters of its outermost parameter-clause, which 8.4.1p1 makes the one
	// that clause declares.  Reading them here is what keeps a parameter
	// clause read once rather than once for the type and again for the names.
	TypeId declarator_type(const AstNode& node, TypeId base, const Context& ctx,
	                       std::string* name,
	                       std::vector<Parameter>* declared = nullptr);
	TypeId apply_pointer(const AstNode& node, TypeId type,
	                     const Context& ctx);
	TypeId apply_suffix(const AstNode& node, TypeId type, const Context& ctx,
	                    std::vector<Parameter>* declared);
	void read_parameters(const AstNode& clause, const Context& ctx,
	                     std::vector<Parameter>& out, bool& variadic);
	// The declarator-id of a declarator, which a nested declarator holds.
	static const AstNode* declarator_id(const AstNode& node);
	// 3.4.3: `name`, which may be written with a nested-name-specifier,
	// resolved from `ctx`.  Null when nothing of the kind is declared.  `found`,
	// when given, takes the declarations the lookup associated with the name,
	// which 3.4p2 lets be more than one where they are all functions.
	SemaEntity* resolve(const std::string& name, const Context& ctx,
	                    LookupKind filter,
	                    std::vector<SemaEntity*>* found = nullptr);
	// The region the nested-name-specifier of `name` reaches, for a declaration
	// that names an entity of another region.
	Scope* resolve_prefix(const QualifiedName& name, const Context& ctx);
	SemaEntity& require(SemaEntity* entity, const std::string& name);

	// Constant expressions and decltype (sema_constant.cpp).
	Constant evaluate(const AstNode& node, const Context& ctx);
	Constant literal_constant(const std::string& spelling);
	Constant id_constant(const AstNode& node, const Context& ctx);
	Constant unary_constant(const AstNode& node, const Context& ctx);
	Constant binary_constant(const AstNode& node, const Context& ctx);
	Constant convert(const Constant& value, TypeId type) const;
	Constant promote(const Constant& value);
	// 5p10: the type the usual arithmetic conversions bring two operands to.
	TypeId common_type(TypeId left, TypeId right);
	unsigned long long array_bound(const AstNode& node, const Context& ctx);
	TypeId decltype_type(const AstNode& node, const Context& ctx);
	// 5.3.3 and 5.3.6 over a type-id, which is the whole of what PA11 needs.
	unsigned long long size_of(TypeId type) const;
	bool is_signed(TypeId type) const;
	unsigned width_of(TypeId type) const;
	// The integral type an enumeration's values are read as.
	TypeId arithmetic_type(TypeId type) const;
	// A value as the dump writes it, signed when its type is.
	std::string spell_value(TypeId type, unsigned long long bits) const;

	// Statements (sema_statement.cpp).
	//
	// A statement writes its node under `parent` and declares into `ctx`.  The
	// two are separate because 3.3.3 gives a substatement a region of its own
	// while the dump writes it under the statement that holds it.
	void semantic_statement(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	void block_statement(const AstNode& node, const Context& ctx,
	                     DumpNode& parent);
	void selection_statement(const AstNode& node, const Context& ctx,
	                         DumpNode& parent, const char* what);
	void loop_statement(const AstNode& node, const Context& ctx,
	                    DumpNode& parent, const char* what);
	void for_statement(const AstNode& node, const Context& ctx, DumpNode& parent);
	void case_statement(const AstNode& node, const Context& ctx,
	                    DumpNode& parent, bool is_default);
	void return_statement(const AstNode& node, const Context& ctx,
	                      DumpNode& parent);
	// 6.4p3 and 6.5p2: the condition of a selection or iteration statement,
	// which is either an expression or a declaration whose name the statement's
	// substatements can see.
	// `integral` for the condition of a switch, which 6.4.2p2 converts to an
	// integral or enumeration type rather than to bool.
	void condition(const AstNode& node, const Context& ctx, DumpNode& parent,
	               bool integral);
	// The region a substatement that is not a compound-statement opens (6.4p1).
	Context substatement_scope(const Context& ctx);

	// Expressions (sema_expression.cpp).
	Value expression(const AstNode& node, const Context& ctx, DumpNode& parent);
	// The one form `node` is, read by the layer that reads that form.  It is
	// split from `expression` so that the depth guard and the recording of
	// what a line stands for are each written once.
	Value dispatch_expression(const AstNode& node, const Context& ctx,
	                          DumpNode& parent);
	Value id_expression(const AstNode& node, const Context& ctx, DumpNode& parent);
	// What an id-expression already resolved to denotes, which is where a
	// caller that had to look the name up to know what it was written for -
	// a call, which 5.2.3 lets name a type - spends the one lookup it takes.
	// `found` is the set that lookup found, which for a function name is what
	// 13.3 or 13.4 chooses from.
	Value named_value(const AstNode& node, SemaEntity& entity, DumpNode& parent,
	                  const std::vector<SemaEntity*>* found = nullptr);
	Value literal_expression(const AstNode& node, DumpNode& parent);
	// 5.2.5: `E1.E2` and `E1->E2`, which name a member of the class the object
	// expression has.
	Value member_expression(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	// 5.2.5p2: the region a member written after `.` or `->` is looked up in,
	// with `object` left denoting the object rather than a pointer to it.
	Scope& object_region(const AstNode& node, Value& object);
	// 5.2.5 and 13.3.1.1.1: a call whose callee names a member.  `line` is the
	// call's own node; the object the member is named on is written as the
	// call's first argument, because 9.3.1p3 already made the object parameter
	// the first parameter of a non-static member function's type.  A member that
	// is not a function leaves `object` denoting nothing and `target` denoting
	// whatever the member holds, which the call then reads as an ordinary one.
	void member_callee(const AstNode& callee, const Context& ctx, DumpNode& line,
	                   Value& target, Value& object);
	// 9.3.2p1: the `this` a member function named with no object expression is
	// called on, written as the call's first argument.  `object` denotes nothing
	// outside a member function, where 13.3.1p4 leaves a non-static member no
	// candidate at all.
	void implicit_object_argument(const std::vector<SemaEntity*>& candidates,
	                              DumpNode& line, Value& object);
	// 5.3.1p3: the address of the object a member call is made on, written into
	// `node` in place of the object expression already under it.
	void address_of_object(Value& object, DumpNode& node, bool through_pointer);
	// 9.3.1p3 and 9.5p1: a member named with no object expression, which is a
	// member of the object `this` points to or of the one an anonymous union
	// declared.  `payload` is what the dump writes after the type, which is the
	// member's name alone when no `.` or `->` was written.
	// `checked_base` is false where 11.2p5's naming class is the one the name
	// was written on rather than the one that declared the member, which is
	// what a using-declaration makes it: the subobject the member belongs to is
	// still named, and the base-specifier's own access is not asked about.
	Value member_value(SemaEntity& member, const Value& object_written,
	                   const std::string& payload, DumpNode& node,
	                   bool checked_base = true);
	// 5.16p3: an operand of a conditional whose result is an lvalue of a base
	// class of that operand's own class.
	void convert_arm_to_base(Value& arm, TypeId result);
	// 5.9p2: an operand of a built-in binary operator whose composite pointer
	// type is a pointer to a base of its own class.
	void convert_operand_to_base(Value& operand, TypeId operands);
	// 4.10p3 and 10p1: the base class subobject of the object an operand
	// denotes, written as one node holding the operand's own line.
	Value base_value(const Value& object, SemaEntity& base,
	                 bool checked = true);
	// 11.2p4: a conversion from a derived class to a base class of it is well
	// formed only where the base-specifier's access reaches, which is what
	// makes a private base unreachable from outside the class that declared it.
	// A conversion spanning a chain passes through every base-specifier between
	// the two classes, so each one is asked in turn.
	void require_base_access(const SemaEntity* derived, const SemaEntity& base);
	// 11.2p1: the one base-specifier a class wrote, asked of where it is read.
	void require_base_link(const SemaEntity& derived);
	// 11.4p1: the additional check a protected non-static member named on an
	// object asks, which is that the object is of the class the access occurs
	// in rather than of the base that declared the member.
	void require_protected_object(const std::vector<SemaEntity*>& found,
	                              const SemaEntity& member, const Scope* from,
	                              const Scope* object_class);
	// 10p1: whether one class region is another or derives from it.
	bool derives_from(const Scope& derived, const Scope& base) const;
	// 12.4p11: the destructor an object's lifetime ends in a call of, which is
	// named where the object is declared and has to be accessible there.
	void require_destruction_access(const SemaEntity& entity, const Scope* from);
	// 11.2: the region the expression being read was written in.
	Scope* reading_;
	// 10.2: the object a member found through a base class is a member of,
	// which is the base subobject of the class that declared it.
	Value object_in_declaring_class(const Value& object,
	                                const SemaEntity& member,
	                                bool checked = true);
	// The object a member named with no object expression is a member of.
	Value implied_object(const SemaEntity& member, DumpNode& line);
	// 5.3.1p3: `&C::x`, where `entity` is the member of a class the qualified-id
	// named, which the caller resolved to learn that it is one.
	Value member_address(const AstNode& node, const SemaEntity& entity,
	                     DumpNode& parent);
	// 5.1.1p3: the object the member function being read was called on.
	Value this_value(DumpNode& parent);
	Value call_expression(const AstNode& node, const Context& ctx,
	                      DumpNode& parent);
	Value functional_cast(const AstNode& node, const Context& ctx,
	                      DumpNode& parent, TypeId target);
	// 8.5: the initializer written for a declarator, in each of the three forms
	// 8.5p1 gives it.  `image` says 3.6.2 gives the object its value rather than
	// a function building it, which is what an object with static storage
	// duration asks for.
	void write_initializer(const AstNode& initializer, TypeId type,
	                       const Context& ctx, DumpNode& line,
	                       bool image = false);
	// 7.1.6.2 Table 10: the type a one-token simple-type-specifier names, which
	// is how a functional cast written with a keyword finds its type.
	TypeId keyword_type(const std::string& spelling) const;
	Value unary_expression(const AstNode& node, const Context& ctx,
	                       DumpNode& parent);
	Value increment_expression(const AstNode& node, const Context& ctx,
	                           DumpNode& parent, bool postfix);
	Value binary_expression(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	Value assignment_expression(const AstNode& node, const Context& ctx,
	                            DumpNode& parent);
	Value conditional_expression(const AstNode& node, const Context& ctx,
	                             DumpNode& parent);
	Value subscript_expression(const AstNode& node, const Context& ctx,
	                           DumpNode& parent);
	// 5.6 to 5.15: the type a built-in binary operator gives its operands.
	TypeId binary_result(unsigned op, const Value& left, const Value& right);
	// 5p9, 5.7p1 and 5.9p2: the one type both operands of a built-in binary
	// operator are converted to.  It is the operator's result type wherever the
	// two coincide, and is asked for separately because a comparison and a
	// pointer subtraction have result types their operands do not share.
	TypeId binary_operand_type(unsigned op, const Value& left, const Value& right);
	// 5.17p7: the built-in operator a compound assignment is written from.
	static unsigned compound_operator(unsigned op);
	Value cast_expression(const AstNode& node, const Context& ctx,
	                      DumpNode& parent);
	// 5.2.9p1 and 5.4p4: what a cast to a reference type makes of its operand,
	// which is the same whether the cast named its type with a type-id or with
	// the name of the type.
	Value cast_to_reference(TypeId target, Value& source, DumpNode& parent,
	                        DumpNode& line, Value value);
	// Puts what `line` holds in the place `line` itself has under `parent`, for
	// the casts 5.2.9 gives the operand's own line to.
	static void lift_operand(DumpNode& parent, DumpNode& line);
	Value sizeof_expression(const AstNode& node, const Context& ctx,
	                        DumpNode& parent);
	// 5.3.6p1: the alignment an object of the operand's type requires.
	Value alignof_expression(const AstNode& node, const Context& ctx,
	                         DumpNode& parent);
	// 5.19 and the course builtins: a call the translation answers itself.
	bool builtin_call(const std::string& name, const AstNode& node,
	                  const Context& ctx, DumpNode& parent, Value& out);
	// 1.4p8 and 17.6.4.3.2p1: the declaration of a reserved function the
	// implementation provides, made where a call names one no declaration of the
	// program reached.  It is declared in the global namespace, so the first
	// call to name it declares it and every later one finds it by ordinary
	// lookup; a name the implementation reserves nothing for leaves it null.
	SemaEntity* reserved_function(const std::string& written,
	                              std::vector<SemaEntity*>* found);

	// Templates (sema_template.cpp).
	//
	// 14.2 and 14.8.1: the declarations a template-id names, which are the
	// specializations its argument list makes of each template of that name,
	// chained as one overload set for 13.3 or 13.4 to choose from.
	// The specializations are appended to `found`, each a declaration of its own
	// that no chain holds, and the first of them is returned.
	SemaEntity* template_specializations(const std::string& spelling,
	                                     const Context& ctx,
	                                     std::vector<SemaEntity*>& found);
	// 14.8.2.1: the specialization of `primary` that the arguments of a call
	// deduce, or null when they deduce none.
	SemaEntity* deduce_specialization(SemaEntity& primary,
	                                  const std::vector<Value>& arguments);
	// 14.8.2.5: the bindings the argument type `argument` gives the template
	// parameters `pattern` is written over, added to `bindings`.  False when
	// the two do not agree, which is a deduction that failed.
	bool deduce(TypeId pattern, TypeId argument,
	            std::unordered_map<TypeId, TypeId>& bindings);
	// 14.7.1: the declaration `arguments` makes of `primary`, made once
	// however many times it is named.
	SemaEntity& specialize(SemaEntity& primary,
	                       const std::vector<TypeId>& arguments);
	// The type a template-argument names, over the PA12 subset: a fundamental
	// type or a type name, with cv-qualifiers, pointers and references written
	// around it.
	TypeId template_argument_type(const std::string& spelling,
	                              const Context& ctx);
	// 14.7.1p1: the declaration a specialization stands for, held for the end
	// of the translation unit, which is where the output writes it.  Naming
	// the same specialization again writes nothing more.
	void instantiate(SemaEntity& function);
	void write_instantiation(const Pending& pending);

	// 8.5: initialising an object of `target` from `node`, which is what a
	// variable, a condition, a return statement and an argument all do.
	// `listed` says the initializer is a clause of a braced-init-list, which
	// 8.5.4p7 will not let narrow the value it holds.
	Value initialize(const AstNode& node, TypeId target, const Context& ctx,
	                 DumpNode& parent, bool listed = false,
	                 Requested by = Requested::Written);
	// 8.5.4p7: the error that a clause narrows to the type it initialises.
	void require_no_narrowing(const AstNode& written, const Value& value,
	                          TypeId target, const Context& ctx);
	// 8.5.4: a braced-init-list written where an expression initializes an
	// object.  Over the PA12 scalar subset it holds at most one clause, whose
	// value the object takes, and an empty one value-initializes it.
	Value list_initialize(const AstNode& node, TypeId target, const Context& ctx,
	                      DumpNode& parent, bool image = false);
	// 13.3.3.1 over the PA12 conversion subset.
	Match match_argument(const Value& argument, TypeId parameter);
	Match match_by_value(const Value& argument, TypeId parameter);
	Match match_reference(const Value& argument, TypeId parameter);
	// 8.5.3p5: whether a reference of `parameter` binds `argument` itself rather
	// than a temporary a conversion made, which is what 5.16p3 asks of the two
	// operands of a conditional expression about each other.
	bool binds_reference(const Value& argument, TypeId parameter);
	// 4.4 and 4.10: whether a pointer of `from` converts to one of `to`.
	// 10p1: the base class of `derived` that `base` names, or null where
	// `derived` derives from no such class.
	SemaEntity* derived_from(TypeId derived, TypeId base);
	bool pointer_convertible(TypeId from, TypeId to, int& rank, bool& exact);
	bool qualification_convertible(TypeId from, TypeId to);
	// 3.9.3p5: `type` with every top level cv-qualifier removed, reaching
	// through an array to its elements.
	TypeId bare_type(TypeId type);
	// 8.3.6p4: whether a call that gives `given` arguments can reach `function`,
	// which it can when every parameter beyond them was written with a default.
	bool accepts_arity(const SemaEntity& function, std::size_t given) const;
	// 8.3.6p9: the default-argument of one parameter, analysed in the region
	// the declaration that introduced it was written in.
	void write_default_argument(const SemaEntity& function, std::size_t index,
	                            DumpNode& parent);
	// 8.3.6p4: the default-arguments `declared` writes are the function's from
	// this declaration on, whether or not this one is the definition.  A
	// parameter already given one keeps the region that introduced it.
	void record_default_arguments(const SemaEntity& function,
	                              const std::vector<Parameter>& declared,
	                              Scope* region);
	// 13.3.3: the one candidate no other beats, or the error that there is not
	// one.  `candidates` are the declaration chains the lookup found, walked in
	// declaration order within each; `arguments` are the analysed argument
	// expressions in order.
	// `object`, when given, is 13.3.1p3's implicit object argument: it is matched
	// against the first parameter of every non-static member candidate and left
	// out of every other, and 13.3.1p4 makes a non-static member no candidate at
	// all where there is none.
	// `converting` leaves out every candidate declared `explicit`, which
	// 13.3.1.4 does for copy-initialization.
	// 5.2.2p4 and 5.2.2p10: the tail of a call once its declaration is known -
	// the conversions its arguments take, the default-arguments 8.3.6 supplies,
	// and the value the call is.  A call the program wrote and the call
	// 13.3.1.2p1 makes of an operator expression end the same way.
	Value finish_call(DumpNode& line, TypeId function,
	                  std::vector<Value>& arguments, const SemaEntity* chosen,
	                  const Context& ctx);
	SemaEntity* select_overload(const std::vector<SemaEntity*>& candidates,
	                            const std::vector<Value>& arguments,
	                            const std::string& name,
	                            const Value* object = nullptr,
	                            bool converting = false,
	                            std::size_t singles = 0,
	                            const Value* operand = nullptr,
	                            bool* unviable = nullptr);

	// 13.5 and 3.4.2 (sema_operator.cpp).
	// 13.3.1.2p1: the operator expression `line` holds the operands of, read as
	// the call of an operator function it stands for.  False - and nothing
	// written - where no operand has class or enumeration type, where the
	// candidate set is empty, or where 13.3 finds nothing in it viable, all of
	// which leave the built-in operator the caller's own to describe.
	bool operator_expression(unsigned token, const Context& ctx, DumpNode& line,
	                         std::vector<Value>& operands, bool member_only,
	                         Value& value);
	// 3.4.2p2: the namespaces and classes the argument types of one call are
	// associated with, each once and in the order they were reached.
	struct Associated
	{
		std::vector<Scope*> spaces;
		std::vector<Scope*> classes;
		// Which regions the two lists already hold.  A namespace and a class
		// are never one region, so one probe answers for both, and gathering
		// costs the regions the types reach rather than their square.
		std::unordered_set<const Scope*> held;
		// The classes whose own base chain the walk has already followed, which
		// is what lets a second argument of one type stop at once.  It is not
		// the same question as `held`: 3.4.2p2 also associates the class a
		// nested type is a member of, and associates none of its bases.
		std::unordered_set<const SemaEntity*> walked;
	};
	// 3.4.2p2: the namespaces and classes a type is associated with.
	void associate_type(TypeId type, Associated& out);
	void associate_region(Scope* region, Associated& out);
	// 3.4.2p2: the declarations of `name` the argument types reach, appended to
	// `candidates`.  Returns how many of them are the single friend
	// declarations 11.3p6 made, which stand last.
	std::size_t argument_candidates(const std::string& name,
	                                const std::vector<Value>& arguments,
	                                std::vector<SemaEntity*>& candidates);
	// 3.4.2p3: whether what the ordinary lookup found leaves the
	// argument-dependent one to be done at all.
	bool allows_adl(const SemaEntity* named) const;
	// 13.5p6: an operator function is a non-static member function, or else a
	// non-member one taking at least one operand of class or enumeration type.
	// `member` is whether the declaration is written in a class and has no
	// object parameter, which is the static member neither half allows.
	void require_operator_operand(const std::string& name, TypeId type,
	                              bool member);
	// 13.3.3.2: which of two conversions of one argument is better, as 1, 0
	// or -1.
	int compare_matches(const Match& left, const Match& right);
	// 13.3.3p1: whether one candidate beats another, which is its conversions
	// and, where those tie, whether it is a declaration the program wrote and
	// the other a specialization a deduction made.
	bool better_candidate(const Match* left, const Match* right,
	                      std::size_t count, bool left_written = false,
	                      bool right_deduced = false);
	// Rewrites what the dump wrote for `value` where a conversion is visible in
	// it: a null pointer constant, a resolved function name, and the temporary
	// a reference binds to.  Each rewrites the line the operand already wrote,
	// in the place it wrote it.
	void apply_conversion(Value& value, TypeId target, const Match& match,
	                      const Context& ctx,
	                      Requested by = Requested::Written);
	// 13.4: the declaration of an overloaded name a target type asks for.
	SemaEntity* resolve_target(const Value& value, TypeId target);
	// 5.3.1p3: the type `&C::f` has, which is a pointer to member of the class
	// that declared `f`, over the type its declarator wrote: 9.3.1p3 put the
	// object it is called on in the function's type, and the cv-qualifiers that
	// parameter carries are the ones written after its parameter-clause.
	// kNoType for anything but a non-static member function.
	TypeId member_pointer_of(const SemaEntity& function);
	// Writes the line of an id-expression once its overload set is resolved.
	void name_function(Value& value, SemaEntity& function, const char* what);
	// 4.1, 4.2 and 4.3: the type the value of `value` has.
	TypeId decayed(const Value& value);
	void require_complete_value(const Value& value);
	// 4.5: the type one operand of an integral operation is promoted to.
	TypeId promoted(TypeId type);
	// 5p9: the type the usual arithmetic conversions bring two operands to.
	TypeId arithmetic_result(TypeId left, TypeId right);
	// 5.9p2 and 5.10p1: the composite pointer type two pointer operands are
	// compared as, or kNoType when they have none.
	TypeId composite_pointer(const Value& left, const Value& right);

	// The dump.
	//
	// One line of a resolved expression: the node kind, what the expression
	// denotes, its type, and the payload the format writes after it.  Every
	// line of the PA12 expression output is spelled here, so a conversion that
	// rewrites one writes the same shape the operand wrote.
	std::string spell(const char* what, ValueCategory category, TypeId type,
	                  const std::string& payload) const;
	std::string spell(const char* what, ValueCategory category, TypeId type,
	                  const AstNode* payload) const;
	// Spells `value`'s line again, from the category and type it now has.
	void respell(const Value& value) const;
	// Writes what `value` is onto the line it wrote, as typed facts.  Every
	// path that spells a line runs through `respell` or through the one place
	// that first spells it, so recording there is what keeps the facts and the
	// text one description of the same value.
	void record(const Value& value) const;
	// The node kind the resolved tree names a line of `what` by.
	static FactKind fact_kind(const char* what);
	static std::string payload_of(const AstNode& node);
	static const char* category_name(ValueCategory category);
	// The name the PA12 dump gives a declaration of `scope`.
	std::string dump_name(const Scope& scope, const std::string& name) const;
	bool semantics() const { return dialect_ != SemaDialect::Types; }
	bool lowering() const { return dialect_ == SemaDialect::Lowering; }

	SemaDialect dialect_;
	TypeTable types_;
	SemaModel model_;
	// The unnamed enumerations declared so far, which are numbered rather than
	// named after a token span because that is the convention the refs use.
	unsigned anonymous_enums_;
	// The unnamed classes defined in a function so far, which 9p1 leaves with
	// no name of their own and which the convention numbers.
	unsigned local_types_;
	// The definitions the end of the translation unit writes, in the order they
	// were asked for.  Writing one may ask for another, so the list grows while
	// it is being walked and what is being written has to stay where it is.
	std::deque<Pending> pending_;
	// 14.1: the parameters each template's declarator wrote, which an
	// instantiation writes again with their types substituted.  It is keyed by
	// the entity the template-declaration declared because it is a fact about
	// how that one declaration was spelled rather than about the type it made,
	// and only a template is ever asked for, so an ordinary declaration adds
	// nothing to it.
	std::unordered_map<std::uint32_t, std::vector<Parameter> > templates_;
	// 12.9p8: the parameters each constructor was declared with, which the
	// inheriting constructor a using-declaration declares takes as its own -
	// their names as much as their types, because the definition this unit
	// generates for it writes them.  Only a constructor is ever inherited, so
	// only a constructor's declaration adds to this.
	std::unordered_map<std::uint32_t, std::vector<Parameter> >
		constructor_parameters_;
	// 8.3.6p4 and 8.3.6p9: the default-arguments each function has been
	// declared with so far, and for each the region its own declaration was
	// written in.  Several declarations of one function each add the defaults
	// they wrote, and p9 reads one in the region that introduced it rather than
	// in the one the call is written in, so the region travels per parameter.
	struct Default
	{
		Default()
			: written(nullptr)
			, scope(nullptr)
		{}

		const AstNode* written;
		Scope* scope;
	};

	std::unordered_map<std::uint32_t, std::vector<Default> > defaults_;
	// 12.6.2p8: the brace-or-equal-initializer each non-static data member was
	// declared with, and the region it was written in, which 9.2p2 makes the
	// complete-class context it is read in.  It is keyed by the member because
	// it is a fact about that one declaration, and it is read once by every
	// constructor whose mem-initializers do not name the member.
	std::unordered_map<std::uint32_t, Default> member_initializers_;
	// 9.3.2p1: the implicit object parameter of the function whose body is
	// being read, which is what `this` and a member named with no object
	// expression denote.  Null outside a member function.
	SemaEntity* self_;
	// 11p6: the class the declaration being read names a member of, which is
	// the context its names are access-checked in.  Null while the declaration
	// names no member of a class.
	Scope* naming_;
	// 6.6.1 and 6.6.2: the statements a `break` or a `continue` may leave, and
	// 6.4.2 the switch a `case` may label, as the counts of the enclosing ones.
	unsigned breakable_;
	unsigned continuable_;
	unsigned switches_;
	// 3.8p1 with 6.6.1p1 and 6.6.2p1: how deep the lifetime frames were when
	// each of those statements was entered, so that a jump out of one knows
	// which blocks it leaves.  The statement's own frame is not one of them:
	// control lands where that frame is closed.
	std::vector<std::size_t> breakable_frames_;
	std::vector<std::size_t> continuable_frames_;
	// 3.8p1: how many objects the open blocks hold whose lifetimes end in a
	// call, carried so that a jump asking whether any does costs nothing per
	// block around it.
	std::size_t live_destructions_;
	// 6.1p1 and 6.6.4p1: the labels the function being read has written, and
	// the ones its goto statements name.  A label may be written after the
	// goto that names it, so the two are gathered and matched once the body
	// has been read.
	std::unordered_set<std::string> labels_;
	std::vector<std::string> gotos_;
	// 6.6.3: the return type of the function whose body is being read.
	TypeId returns_;
	// 7.5p1 and 7.5p4: the language linkage of the linkage-specification the
	// declaration being read is written inside, which is the innermost one.
	bool c_linkage_;
	// How deep the walk of one expression is.
	//
	// 5.6 makes `a + b + c + ...` a tree as deep as it is long while nesting no
	// rule, so the PA10 parse guard does not bound it: a file that writes a long
	// enough chain asks this walk for a machine stack as deep as the file is.
	// The same counter bounds it here, and a file that goes deeper is refused
	// rather than overrunning the stack.  Every other shape the walk recurses on
	// nests the parse as well, and so is already bounded where it is read.
	ParseDepth depth_;
};
