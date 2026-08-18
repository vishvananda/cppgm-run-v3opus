#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sema_pack.h"
#include "sema_scope.h"
#include "type_model.h"

struct AstNode;
class SemaAnalyzer;

// The typed records the declaration layer passes between its steps.
//
// 7p1's decl-specifier-seq, 8.3.5p4's parameter, 3.3's region a declaration is
// read against, 12.6.2p1's mem-initializer and 5.19p3's value are what every
// walk of a declaration hands to the next one, so they are the vocabulary of
// that layer rather than of any one of its walks - the same split
// `sema_value.h` makes for the expression layer.  Each holds facts and never
// text.

// Where a declaration is read: the region it declares into, and the dump
// node its lines are written to.  The two part company where the standard
// and the output format do: a member function defined outside its class
// declares into the class and writes its lines there, while an enumeration
// defined outside its class writes its lines where it is written.
struct SemaContext
{
	SemaContext()
		: scope(nullptr)
		, dump(nullptr)
		, node(nullptr)
		, template_head(nullptr)
		, enclosing_head(nullptr)
		, instantiated_member(false)
	{}

	Scope* scope;
	DumpScope* dump;
	// The PA12 node a declaration read here writes under, which at block
	// scope is the `simple-declaration` the statement opened.  Null where
	// the output has no line for what a declaration declares, which is
	// every member of a class.
	DumpNode* node;
	// 14.1p1: the region the template head standing over *this* declaration
	// declared its parameters in.  It equals `scope` for every declaration a
	// template-declaration parameterises, and 3.4.1p8 is where the two part
	// company: a declarator-id with a nested-name-specifier is read in the
	// region that name reaches, with the head's own names standing over it -
	// so the head is what says the declaration declares a template however far
	// from it that region is.  Null for a declaration no template-declaration
	// parameterises, and for every reading nested inside one.
	Scope* template_head;
	// 14.5.1.3p1: the head standing outside the one above, taken before
	// `StandingIn` moves the nest into the region the declarator-id names.
	// A member template defined outside its class stands under a head per class
	// it is nested in and then its own, and 14.1p2 lets that definition spell
	// the enclosing classes' places with names of its own - so this is the
	// region binding those names, and 14.7.1p1's reading of the pattern is
	// opened there rather than in the class, which binds only the names the
	// class's own head spelled.  Null for every declaration written where its
	// one head stands.
	Scope* enclosing_head;
	// 14.5.1.3p1 with 14.7.1p1: whether this reading is an instantiation of a
	// member definition written outside its class template - which 9.4.2p2
	// makes the definition that lays a static data member's storage out.  What
	// it says is that no unit wrote that definition for these arguments, so
	// 3.2p3 puts the storage in the program where the program reaches it;
	// 14.7.3p1's `template<>` is written out and is read with this false.
	bool instantiated_member;
};

// The terminals a declaration was written from, which is what names an
// unnamed class that no declarator names (9.5p2).
struct SemaSpan
{
	std::uint32_t begin;
	std::uint32_t end;
};

// A `decl-specifier-seq` or `type-specifier-seq` as read.
struct DeclSpecifiers
{
	DeclSpecifiers();

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
	// 10.3p1: the member function this sequence declares is dispatched
	// through the object's own class rather than through the name a call
	// wrote, which 9.2p2 settles for the class as a whole.
	bool is_virtual;
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

// 5.1.1p3: whether the declarators of a declaration written in a class declare
// member functions an object of it is called on, which is what stands `this`
// over the rest of each of them.  9.4p1's `static` member is called on none,
// 11.3p1's friend declares into the region around the class rather than a member
// of it, and 7.1.3's typedef declares no function at all - so each of the three
// writes a declarator in a class body that has no object to name, and no part of
// the declarator says which of them was written.
bool declares_object_member(const DeclSpecifiers& specifiers);

// One parameter of a parameter-clause, before 8.3.5p4 drops a lone `void`.
struct DeclaredParameter
{
	DeclaredParameter()
		: type(kNoType)
		, initializer(nullptr)
		, pack_run(0)
	{}

	std::string name;
	TypeId type;
	// 8.3.6p1: the default-argument this parameter was written with, which
	// a call that omits the argument uses in its place.
	const AstNode* initializer;
	// 8.3.5p10: the name the object file spells this place with where this
	// clause wrote none.  It is the function's name for the place rather than
	// this declaration's, so it names the object and binds nothing: 3.3.4 ends
	// a declaration's parameter names at its own declarator.  Empty where this
	// clause named the place itself, or where no declaration has yet.
	std::string object_name;
	// 14.5.3p4: how many places the expansion of a function parameter pack
	// made, carried by the first of them because 8.3.5p10 gives it the pack's
	// own name.  Zero for every place a clause wrote out.
	unsigned pack_run;
};

// An initializer as written, and the region it is read in - which is not the
// region whatever asks for it stands in.  8.3.6p9 reads a default-argument in
// the region the declaration that gave it was written in, and 9.2p2 reads a
// brace-or-equal-initializer in the complete-class context of its class, so the
// region travels beside the syntax in both.
struct HeldInitializer
{
	HeldInitializer()
		: written(nullptr)
		, scope(nullptr)
	{}

	const AstNode* written;
	Scope* scope;
};

// What every declaration of one function has said about one of the parameters
// its type gives a place to, kept per function rather than per declaration.
//
// 8.3.6p4: the default-argument belongs to the declaration that first gave it,
// which a later one does not move.
//
// 8.3.5p10: the name is no part of the type, so no two declarations need agree
// about it and only the object file asks - the first name any declaration gave,
// with a definition's own name beating it.  A declaration may name the place
// after a definition has already made the object for it, so the objects that
// definition left unnamed wait here for that name.
// 14.7.1p1: what the template's *first* declaration spelled this place with,
// which is what a specialization made from it is spelled with.  A
// specialization is a declaration nothing wrote, so it takes no name of its
// own - and every later declaration of the template redeclares the template
// rather than any specialization, so none of them is one of the declarations
// that could have named this place.  A definition still spells the places its
// own declarator wrote, because the body read for a specialization is that
// declarator's.
struct ParameterRecord
{
	ParameterRecord()
		: pattern_frozen(false)
	{}

	HeldInitializer initializer;
	std::string name;
	std::string pattern_name;
	bool pattern_frozen;
	std::vector<SemaEntity*> objects;
};

// A definition the dump writes at the end of the translation unit.
//
// 12.1p5 gives a class a constructor no declaration wrote, and 9.2p2 makes
// a member function's body a complete-class context, which is read after
// the class it is written in is closed.  Both are definitions the program
// has that the place they are written cannot hold, so they are held here
// and written where the output puts them.
struct PendingDefinition
{
	PendingDefinition();

	SemaEntity* function;
	// The implicit object parameter of 9.3.1p3, which the dump writes as
	// the first parameter of a member function.
	SemaEntity* self;
	// A member function's declarator and body, and the region its
	// parameters were declared in.  Null for a constructor no declaration
	// wrote, whose body is empty.
	const AstNode* body;
	Scope* scope;
	std::vector<DeclaredParameter> parameters;
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
	// 14.7.3p1: whether the body held here is one 14.7.1p1's reading of a pattern
	// made.  Such a body is the definition of the specialization only until the
	// program writes one out for exactly these arguments - so the entry is dropped
	// where `SemaEntity::instantiated_definition` says the declaration no longer
	// holds it, which is one flag test per definition written at the end of the
	// unit and no walk of what was queued.
	bool from_pattern;
	// 14.5.1.3p1 and 14.1p2: the class an out-of-class member definition of a
	// specialization stands inside while it is read, and the region its own
	// head declared its places in.  14.7.1p1 leaves the body to the use that
	// names the member, so the two are recorded here and `EnclosedBy` puts the
	// link back where the body is finally read.  Null for every definition read
	// where it stands.
	Scope* stands_in;
	Scope* head;
	// 6.6.3p2 and 12.1: what the reading that made this definition was made
	// *for*.  A specialization named inside a body that returns an object of
	// class type is one the caller's own storage holds, and the chain of such
	// readings back to the program's own code is what the references write both
	// of the ABI's entry points for - so a base subobject created here asks for
	// the whole of the function and not for the one entry it names.  The chain
	// is carried on the entry because a queued body never stands inside
	// another: the list is walked flat, so the provenance a reading had is a
	// fact of the entry and not of where it is read.
	bool returned_object_chain;
	bool from_instantiated_body;
	// 14.6.4.2p1: what 3.3.6's namespace regions had declared where this body
	// was written.  A body 14.7.1p1's reading put aside is read at the end of
	// the unit, by which time the namespaces its names are looked up in hold
	// every declaration the program made - so 3.4.1's half of what it finds is
	// this number and not what stands at the reading that drains the list.
	// Zero for a body read where it stands, which is under no bound at all.
	std::uint32_t visible;
	// 14.7.1p1: whether the class's own instantiation is what made this body and
	// put it aside, rather than the use that later asked for it.  Such a body is
	// no *start* of 6.6.3p2's chain: the reading that returned the object is the
	// one the entry points are owed for, and a member the class made was made
	// before any of them was read.  It says nothing about a body queued while a
	// chain is already standing, which carries the chain it was queued under.
	bool held_by_class;
};

// 9.6p2: the storage unit a run of bit-fields is being placed in, which one
// member-declaration hands to the next: whether one is open, the type it was
// declared with, the byte it begins at, and how much of it is spent.
struct BitFieldUnit
{
	BitFieldUnit()
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

// 9.2p2 and 14.6p8: a member function body a reading of a template's own
// definition has yet to read.  A body written inside a class body is a
// complete-class context, so the reading holds each until the class-specifier
// closes and reads them all there - which is what lets one name a member the
// class declares below it.
struct HeldTemplateBody
{
	const AstNode* node;
	SemaContext inner;
	std::vector<DeclaredParameter> parameters;
	TypeId type;
};

// 12.6.2: one mem-initializer of a ctor-initializer, indexed by the name
// its mem-initializer-id ends in.  `used` says a member of that name was
// reached, which 12.6.2p2 is what makes the mem-initializer-id name
// something; `spelled` is what the diagnostic names when it does not.
struct WrittenMemInitializer
{
	WrittenMemInitializer()
		: written(nullptr)
		, region(nullptr)
		, used(false)
	{}

	const AstNode* written;
	// 14.5.3p4: the region this entry is read in, which for one element of an
	// expanded mem-initializer binds the packs its pattern names to that
	// element.  Null for every mem-initializer written without a `...`, which
	// is read where the ctor-initializer stands.
	Scope* region;
	std::string spelled;
	bool used;
};

// 8.5p16 and 8.5.4: the form the initializer of an object of class type was
// written with, read once from what the program wrote and before anything is
// written for the initialization.
struct WrittenInitializer
{
	WrittenInitializer()
		: list(nullptr)
		, converting(false)
		, elided_prvalue(false)
		, value_init(false)
	{}

	const AstNode* list;
	bool converting;
	bool elided_prvalue;
	bool value_init;
};

// 8.5.1p2: the initializer-clauses of one braced-init-list, and how many of
// them the subobjects read so far have taken.  8.5.1p11 lets one list
// initialize subobjects at several depths, so the cursor is what the walk of
// the aggregate carries rather than a list per level.
//
// What the clauses *are* is 14.5.3p4's question, so the list is the one
// `sema_pack.h` reads an expansion out of: a clause the program wrote
// `pattern...` is as many clauses as its run holds, each read in a region of
// its own.
struct InitializerClauses
{
	InitializerClauses(const AstNode& written)
		: list(written)
		, at(0)
	{}

	InitializerClauses(const AstNode* written, SemaAnalyzer& analyzer,
	                   const SemaContext& ctx)
		: list(written, analyzer, ctx)
		, at(0)
	{}

	WrittenList list;
	std::size_t at;

	bool spent() const;
	const AstNode& next() const;
	// The context the clause standing here is read in, which is the one the
	// list stands in except where 14.5.3p4 put this clause in a region of its
	// own.
	SemaContext in(const SemaContext& outer) const;
};

// 8.5 initializes an object a declaration named; 12.6.2 initializes a
// non-static data member of the object the constructor is running on, and
// 12.6.2p5 that object's base class subobject.  The three differ only in how
// the action names the object, so one path writes all three.
// 5.2.2p4's parameter is a fourth: an object the *caller* built, which the
// function ends because the boundary says so - so it names the object the
// way a declaration does and 12.4p8's reading of an empty body, which is an
// answer about an object this translation created, does not reach it.
enum class ObjectPlacement
{
	Named,
	Member,
	Base,
	Parameter,
	// 12.6.2p6: the object the constructor being written is already running
	// on, which its ctor-initializer delegates the whole of to another
	// constructor of the same class.  It is no subobject of anything, so
	// what the call is passed is `this` as it stands.
	Delegate
};

// 12.2p1: what asked a conversion for the temporary it made, which is what
// names the storage the function gives that temporary.  An argument of class
// type is a copy the call owns (5.2.2p4) and a returned prvalue is the object
// the caller reads (6.6.3p2), so each names its own storage; every other place
// reads the object the expression already wrote, which keeps the name it was
// given where it was written.
enum class TemporaryRequest
{
	Written,
	Argument,
	Returned
};

// 7.1.6.2p1 and 14.6.2.2p1: a decltype-specifier whose expression names a type
// an argument list has yet to say, kept as the two facts 14.7.1p1's
// instantiation reads it from again - the specifier as it was written, and the
// region it was written in, whose names stand for what the arguments make of
// them.  Nothing is substituted into the expression: it is read a second time.
// 14.3.2p1's value argument an argument list has yet to settle is kept the same
// way and for the same reason: `X<A + 1>` written in a function template's own
// declarator is a type built once, and what it comes to is the expression read
// again against the arguments.  So `written` null with a `spelling` is that
// second form - 5.19 over the text 14.2 left the argument as, converted to the
// type `place` names once the arguments have settled it.
//
// 14.1p9's default at a value place is the third: the head wrote a tree there,
// which the list that stops short of the place would evaluate, and a list that
// left an earlier place dependent has nothing to evaluate it against yet.  So
// the tree is kept as `written` and `evaluated` says 5.19 rather than 7.1.6.2p4
// is what reads it again, converted to `place` as the spelling form is.
struct DependentDecltype
{
	DependentDecltype()
		: written(nullptr)
		, region(nullptr)
		, place(kNoType)
		, evaluated(false)
		, visible(0)
	{}

	const AstNode* written;
	Scope* region;
	std::string spelling;
	TypeId place;
	bool evaluated;
	// 3.3.7p1: how many declarations each of those regions had made when the
	// specifier was read, innermost first.  A place a clause declares *after* a
	// specifier is one that specifier could not name - its potential scope
	// begins at its own declarator-id - and its type may be this very specifier,
	// so a second reading that rebuilt the whole region would have to know what
	// it is about to say to say it.
	std::vector<std::size_t> reach;
	// 14.6.4.2p1: how many declarations the *unit* had made then, which is the
	// same clause asked of 3.3.6's namespace regions.  A second reading is made
	// where the arguments arrive, and the namespaces it looks names up in have
	// gone on being declared into since - so 3.4.1's lookup from the template
	// definition context is this number, and a declaration made after it is one
	// the reading may not find.  The regions above are rebuilt, so they need no
	// such bound; a namespace is reached as it stands and does.
	std::uint32_t visible;
};

// 14.1p9's default at a place a *function* template's head declared, whose
// places are declarations of their own rather than entries of a `TemplateInfo`.
// The two facts are the same two the class tier keeps: what the head wrote,
// and what 14.6.4.2p1 says its names could reach where it wrote it.
struct PlaceDefault
{
	PlaceDefault()
		: written(nullptr)
		, visible(0)
	{}

	PlaceDefault(const AstNode* node, std::uint32_t bound)
		: written(node)
		, visible(bound)
	{}

	const AstNode* written;
	std::uint32_t visible;
};

// The readings a pattern left standing, which 14.7.1p1 makes again once an
// argument list has arrived.
//
// They are one owner rather than five members of the analysis because they are
// one fact read one way: a construct a template definition could not settle is
// interned by what it was written as and by the region it was written in, and
// what the substitution finds under that key is the reading to make again.  A
// pattern that writes one spelling n times leaves one entry here and names one
// type, which is what makes a naming inside a template cost the same as a
// naming outside one.
struct DependentReadings
{
	// 14.6.2p1: the declarations made for names written through a prefix no
	// argument list has settled, keyed by that prefix and the spelling.
	std::unordered_map<std::string, SemaEntity*> names;
	// 7.1.6.2p1: the same, for a decltype-specifier over an expression the
	// reading does not type, keyed by the specifier and the region it stands
	// in and read back by the substitution that has the arguments.
	std::unordered_map<std::string, TypeId> expressions;
	std::unordered_map<TypeId, DependentDecltype> written;
	// 14.6.2p1: the declaration one `C<A…>` written over a template place
	// stands for, keyed by the place and the interned argument list.
	std::unordered_map<std::uint64_t, SemaEntity*> templates;
	// 14.6.2p2's argument a spelling no argument list has settled stands for,
	// which is a fact of the text alone.
	std::unordered_map<std::string, TypeId> values;
};

// One object whose destruction is an action of its own declaration, and the
// line that declaration wrote.  3.7.2p2's object of a thread and 3.7.1p3's
// block-scope `static` are both of them: neither ends where the region that
// names it ends, and both are handed to the runtime at the one initialization
// they get - so the action stands under the declaration, where that
// initialization is, and is written once the declaration has written
// everything else it holds.
struct DeclaredLifetime
{
	SemaEntity* entity;
	DumpNode* line;
};

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
//
// The two halves of that first sentence are told apart by `covered`.  A refusal
// 5.19 itself makes - a call of a function no declaration made constexpr, an
// overflow, a write to an object the evaluation does not own - is a fact about
// the program, and `covered` is true.  A refusal this reading makes because it
// has no value of the kind the expression asked for - 5.19p2's address, an
// operator this milestone does not evaluate, a class 10p1's base subobject
// leaves it holding no object of - is a fact about the build, and `covered` is
// false.  7.1.5p9's requirement is the one reader: a declaration that asked for
// a constant initializer is refused where a covered refusal says the program is
// wrong, and left alone where the reading merely ran out.
class NotConstant : public std::runtime_error
{
public:
	explicit NotConstant(const std::string& what, bool covered = true)
		: std::runtime_error(what), covered(covered)
	{}

	// Whether this refusal is 5.19's answer about the program rather than the
	// edge of what this build reads.
	bool covered;
};

// A value of the 5.19 subset: what it is worth, and the type that says how
// wide it is and whether it is signed.
//
// 3.9.1p8 leaves two kinds of arithmetic value, and no integer of this
// translation holds one of the floating ones - so which of the two fields says
// what the constant is worth is what the type says, and every reading that
// takes a value asks the type first.  `bits` carries an integral value, and for
// a constant that stands for an object of class type it carries the identifier
// of the interned list its subobjects hold; `real` carries a floating one,
// already rounded to the width its type gives it.
// A constant of pointer type carries neither: 3.9.2p1's value is an address,
// which `bits` holds as the identifier `AddressTable` interned the object it
// designates under.  `object` is the other half of the same fact read one step
// earlier - 3.10p1's glvalue, the object the *expression* designated, which is
// what `&` hands back, what a reference binds to and what 4.2p1 decays.
struct SemaConstant
{
	SemaConstant()
		: type(kNoType)
		, bits(0)
		, real(0)
		, object(0)
		, valued(true)
	{}

	TypeId type;
	unsigned long long bits;
	long double real;
	// 3.10p1: the object this value was read out of, where the expression that
	// reached it designated one, and zero where it is a prvalue of its own.
	std::uint32_t object;
	// 8.5p11: whether the fields above say what that object holds.  An object
	// whose address is a constant expression need not have a value 5.19 reads -
	// `static int n;` is one, and `&n` is a constant while `n` is not - so an
	// operand read for its address alone is this, and every reading that asks
	// for a value refuses it.
	bool valued;
};

