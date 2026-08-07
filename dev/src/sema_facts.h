#pragma once

#include <string>

#include "type_model.h"

struct SemaEntity;

// 12.6p1, 8.5.1p7 and 12.4p8: how many elements of an array of class type are
// still described one element at a time.  Every element of such an array is
// created and destroyed by the same one call, and 15.2p2 asks about each of
// them separately - so writing them out is n nodes and n(n+1)/2 instructions
// for a count the source wrote as one number.  Past this the elements stop
// being a description of the array and the bound starts being one: the
// analysis leaves 8.5.1p7's tail as one action, and the lowering writes both
// 12.6p1's construction and 12.4p8's destruction as one loop over an index the
// function holds.  It is one number because the analysis, the lowering and the
// counting of a destructor's suffix all have to agree about which form the
// array was written in.
const unsigned long long kArrayLoopLimit = 16;

// 3.10: what an expression denotes.
enum class ValueCategory
{
	LValue,
	XValue,
	PRValue
};

// What one node of the resolved procedural tree is.
//
// The PA12 dump writes a line per resolved statement, declaration and
// expression, and the tree of those lines is already the shape a lowering
// walks: source order, one node per construct, operands under the operator
// that reads them.  This names the construct each node stands for, so a later
// assignment reads the resolved program from the model rather than from the
// text the model wrote or from the syntax tree it was read out of.
enum class FactKind : unsigned char
{
	None,
	// Expressions.
	Literal,
	Id,
	Member,
	Callee,
	Call,
	Unary,
	Postfix,
	Binary,
	Assignment,
	Conditional,
	Subscript,
	Cast,
	// 4.10p3 and 10p1: the base class subobject of the object the operand
	// denotes, which is that object's storage at the place its class gave the
	// base.  The operand is a pointer where a pointer converted and the object
	// itself where an lvalue did, and this node's own type says which.
	BaseConversion,
	Sizeof,
	// 5.3.4: a new-expression, which is the call of the allocation function
	// 3.7.4.1 gives the storage and, under it, the initialization 8.5p16 gives
	// the object that storage now holds.  What the expression is worth is the
	// pointer the call returned, so the object is built at an address the tree
	// already produced rather than in storage of the function's own.
	NewExpression,
	BracedInitList,
	Label,
	Goto,
	// Statements.
	Compound,
	ExpressionStatement,
	Return,
	If,
	Switch,
	While,
	Do,
	For,
	ForInit,
	Iteration,
	Then,
	Else,
	Condition,
	ConditionDeclaration,
	Case,
	Default,
	Break,
	Continue,
	SimpleDeclaration,
	// 12.1p5 and 8.5p6: the constructor call an object of class type is
	// initialized by, written under the declaration of the object.
	ConstructorAction,
	// 12.2p1 and 5.2.3p2: a prvalue of class type, which is an object no
	// declaration named.  The function it stands in gives it storage, the
	// `constructor-action` under it runs on that storage, and everything that
	// reads the prvalue - a reference bound to it, a member named in it, a base
	// subobject of it - reads that object.  `entity` is the object, so two
	// readers of one temporary reach one piece of storage; `spelling` is the
	// name its storage takes, which says what asked for the object.
	TemporaryObject,
	// 12.4p3 and 3.8p1: the destructor call the end of an object's lifetime is,
	// written where that end is - at the end of the block that declared it, on
	// the return that leaves it, and in the shutdown of the program.
	DestructorAction,
	// 12.6.2: one subobject of the object a constructor is initializing, as the
	// member it names and what initializes it.  The constructor's own body
	// holds one per member in declaration order, whatever order the
	// mem-initializers were written in.
	MemberInitialization,
	// 8.5.1: an aggregate initialized from a braced-init-list, as the
	// subobjects its clauses reached and the value every other one takes.
	AggregateInitialization,
	// One of those subobjects: a member of a class, or an element of an array.
	SubobjectInitialization,
	// 12.8p15 and p28: one step of a value transfer the standard rather than
	// the program defines that carries storage rather than naming a subobject -
	// the leading run of members a copy of the bytes carries exactly, and
	// 9.6p2's storage unit, which the bit-fields sharing it are carried in one
	// piece.  `value` is the byte it begins at, `elements` how many bytes it
	// spans, and `type` the scalar the two ends are read and written with, or
	// no type where the span is copied as storage.  The two children are the
	// object written into and the object read from, in that order.
	StorageTransfer,
	// Declarations.
	Variable,
	Parameter,
	FunctionDefinition,
	FunctionDeclaration,
	Namespace,
	TypeAlias
};

// The typed facts one node of that tree carries.
//
// Everything here was established by the PA11/PA12 analysis: the type an
// expression has once 5p5 removed a reference from it, the type its
// declaration wrote, what it denotes, the declaration a name resolved to, the
// operator a node was written with, and the value the constant layer knows.  A
// consumer never re-resolves a name or re-reads a type from syntax.
struct SemaFact
{
	SemaFact()
		: kind(FactKind::None)
		, type(kNoType)
		, spelled(kNoType)
		, category(ValueCategory::PRValue)
		, op(0)
		, operands(kNoType)
		, entity(nullptr)
		, constant(false)
		, zero_initialized(false)
		, reverse_elements(false)
		, elided_prvalue(false)
		, base_subobject(false)
		, elements(0)
		, value(0)
	{}

	FactKind kind;
	// The type the operators see, with a reference already removed.
	TypeId type;
	// The type as written, which keeps the reference a call or a cast makes
	// visible in its result, and for a declaration is what it declared.
	TypeId spelled;
	ValueCategory category;
	// The token the node was written with, for the operator forms.
	unsigned op;
	// 5p9 and 5.9p2: the type a built-in binary operator brings both operands
	// to before it acts on them, which its own result type does not always
	// say - a comparison yields `bool` however its operands were compared.
	TypeId operands;
	// The declaration a name, a callee, a variable or a parameter stands for.
	SemaEntity* entity;
	// 5.19: the value the translation knows, for a literal, an enumerator and
	// a constant the constant layer folded.
	bool constant;
	// 8.5p8: whether this `constructor-action` value-initializes an object of a
	// class with no user-provided constructor, which zero-initializes its
	// storage before the constructor is run on it.  That zero is what the
	// object holds wherever the constructor leaves a member alone, and it is
	// written even where the constructor itself does nothing.
	// 8.5p7: on a `literal`, whether the value is the zero an object of the type
	// is value-initialized with rather than a constant the program wrote.  Which
	// of the two it is is what says a pointer takes 4.10p1's null pointer value
	// where a written null pointer constant is the integer it was spelled as.
	bool zero_initialized;
	// 12.6p1 and 12.4p8: whether this `constructor-action` or
	// `destructor-action` acts on the elements of an array from the last one
	// back to the first.  The action names the array rather than one element,
	// so how many objects it is, is what the array type says; this is the one
	// thing left to say about them, and 12.6.2p10's member is the subobject
	// destroyed in the reverse of the order it was created in.
	bool reverse_elements;
	// 12.8p31 and 5.2.3p1: whether this `constructor-action` is what a prvalue
	// written `T(...)` or `T{...}` was elided into - the object and the prvalue
	// are one, so what was written reached the object directly and no copy
	// stands between them.  5.2.3p2's `T()` is not one of them: it names no
	// argument, so it is the value-initialization written where it stands.
	// It is the one initialization of a subobject of a class that holds nothing
	// whose call the checked-in LowIR does not write, because the copy 8.5.1p2
	// asks for copies nothing and the reference elides the construction of the
	// prvalue with it.
	bool elided_prvalue;
	// 12.6.2p5 and 12.4p8: whether this `constructor-action` or
	// `destructor-action` acts on the base class subobject of the object the
	// function it stands in was called on.  The ABI gives a constructor and a
	// destructor one entry point for a complete object and one for a base
	// subobject, and this is what says which of them this call is of.
	bool base_subobject;
	// 8.5.1p7: how many consecutive elements of the array it stands in this
	// `constructor-action` builds.  An element a clause reached is one object
	// and this is zero; the elements no clause reached are all value-
	// initialized by the same call, so past `kArrayLoopLimit` of them they are
	// one action and this says how many - which is what keeps a bound the
	// source wrote as a number from being described one element at a time.
	unsigned long long elements;
	unsigned long long value;
	// The spelling this node carries that no other fact holds: the tokens a
	// floating literal was written from, whose value is a value of the
	// target's floating type; the code units a string literal holds, which
	// 2.14.5 makes an array object rather than a value; and the identifier
	// 6.1 gives a label, which names a place in the function and no entity.
	std::string spelling;
};
