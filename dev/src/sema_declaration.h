#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sema_scope.h"
#include "type_model.h"

struct AstNode;

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
	{}

	Scope* scope;
	DumpScope* dump;
	// The PA12 node a declaration read here writes under, which at block
	// scope is the `simple-declaration` the statement opened.  Null where
	// the output has no line for what a declaration declares, which is
	// every member of a class.
	DumpNode* node;
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

// One parameter of a parameter-clause, before 8.3.5p4 drops a lone `void`.
struct DeclaredParameter
{
	DeclaredParameter()
		: type(kNoType)
		, initializer(nullptr)
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
		, used(false)
	{}

	const AstNode* written;
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
struct InitializerClauses
{
	InitializerClauses(const AstNode& written)
		: list(&written)
		, at(0)
	{}

	const AstNode* list;
	std::size_t at;

	bool spent() const;
	const AstNode& next() const;
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
struct DependentDecltype
{
	DependentDecltype()
		: written(nullptr)
		, region(nullptr)
	{}

	const AstNode* written;
	Scope* region;
};

// 3.7.2p2: one namespace-scope object with thread storage duration, and the
// line it was declared on.  It is destroyed when its own thread ends rather
// than when the program does, so the action stands under the declaration -
// where the initialization that hands it to the runtime is - and is written
// once that declaration has written everything else it holds.
struct ThreadLifetime
{
	SemaEntity* entity;
	DumpNode* line;
};

// A value of the 5.19 subset: what it is worth, and the type that says how
// wide it is and whether it is signed.
struct SemaConstant
{
	SemaConstant()
		: type(kNoType)
		, bits(0)
	{}

	TypeId type;
	unsigned long long bits;
};
