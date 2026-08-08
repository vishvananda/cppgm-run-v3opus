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

// 5.3.4p1, 5.3.5p2 and the ABI: the bytes the array form of a new-expression
// asks for in front of the elements, which hold how many elements there are.
// 5.3.5p2's array delete is handed the pointer to the first element and has to
// run one destructor per element, so the count is the one thing about the
// allocation that has to outlive the expression that made it.  Only an array of
// class type carries one: nothing else has a lifetime for the count to bound.
const unsigned long long kArrayCookieBytes = 8;

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
	// 5.3.5: a delete-expression, which is 12.4p3's end of the lifetime of the
	// object its operand points to and 3.7.4.2's return of the storage that
	// object stood in.  Both act on storage the operand already names, so the
	// node holds the destruction as its own action and the deallocation
	// function 5.3.5p9 chose as `entity`: the one argument that call takes is
	// the pointer, which is what the operand under this node is worth.
	DeleteExpression,
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
	// 12.8p15 and p28: one step of such a transfer that carries an *array*
	// member whose elements are carried by a call rather than by their bytes.
	// The member the element's class has takes one element and no call of it
	// takes the array, so the step is written once - for an element - and this
	// node is the walk of the array around it: `type` is the array member's
	// own type, which is what says how many elements the one step is run for,
	// and the one child is that step.  Which element it is at is the walk's,
	// exactly as 12.6p1's construction of an array leaves it: the lines under
	// it name the array, and where they stand is the element the walk reached.
	ArrayTransfer,
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
		, destruction(nullptr)
		, object(nullptr)
		, constant(false)
		, zero_initialized(false)
		, reverse_elements(false)
		, elided_prvalue(false)
		, base_subobject(false)
		, subobject_step(false)
		, array_form(false)
		, object_definition(false)
		, counted(false)
		, may_fail(false)
		, full_expression_end(false)
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
	// 12.4p3 and 15.2p2: the destructor the object whose lifetime this node
	// begins ends in.  A declaration and a prvalue of class type each begin
	// one, and an exception thrown while the object stands has to end it
	// wherever it goes on to - so the call is a fact of the place the lifetime
	// began and not only of the place it ends.  Null where the end of the
	// lifetime comes to nothing, which is what says no handler owes it.
	SemaEntity* destruction;
	// 12.2p1 and 12.2p3: the object a prvalue of class type that stands in
	// storage of its own *is*.  A `temporary-object` names one the analysis
	// wrote; a call and a conditional that hand back a class prvalue name none,
	// and where the place asking for the value needs an object rather than a
	// value the function gives them one - so the object is a fact of the node
	// that produced it and not of any declaration.  It is what the end of the
	// lifetime 12.2p3 gives that object names, and null wherever the prvalue
	// reached a destination something else had already named.
	SemaEntity* object;
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
	// 12.8p15 and 12.6.2p10: whether this `constructor-action` builds a
	// subobject of an object rather than a complete object of its own.  What
	// the two part company over is 8.4.3p2: the class's copy constructor being
	// one no program may name says how the *program's* initialization of an
	// object of it is carried, and says nothing about the constructor 12.8p15
	// itself selected for a subobject one step in.
	bool subobject_step;
	// 5.3.4p1 and 5.3.5p2: whether this `new-expression` or `delete-expression`
	// is the array form, which asks for one object per element rather than one
	// object, keeps the count of them beside the storage, and ends as many
	// lifetimes as it began.  The two forms call different functions and
	// 5.3.5p2 makes writing one for the other undefined, so which one this is,
	// is a fact of the expression and not of the type its operand has.
	bool array_form;
	// 3.1p2: whether *this* declaration of the object is the one that defines
	// it.  Whether the object is defined at all is a fact of the declaration it
	// stands for, and 9.4.2p2's definition written with a nested-name-specifier
	// is a second line describing one object - so which of the lines lays the
	// storage out is a fact of the line and not of the entity under it.
	bool object_definition;
	// 5.3.4p6: whether the number of elements this `new-expression`'s array
	// form creates is one the translation knows.  It is the one array bound the
	// standard does not require to be a constant, so `elements` says how many
	// there are only when this is true - a count of zero is a count like any
	// other, and reading `elements == 0` as "not known" would make the one
	// bound the source wrote plainest the one the program computes at run time.
	bool counted;
	// 5.3.4p15 and 18.6.1.1p3: whether a call of the allocation function this
	// `new-expression` chose says it obtained no storage by handing back a null
	// pointer, which is what makes an address there something to test before
	// anything is initialized at it.
	bool may_fail;
	// 1.9p10 and 12.2p3: whether this `destructor-action` is the end of a
	// temporary of the full-expression that created it, rather than 3.8p1's end
	// of an object a block control leaves declared.  The two stand side by side
	// under one statement - a `return` ends its operand's temporaries and then
	// every block it leaves - and 15.2p2's handler tells them apart: it stands
	// around the full-expression, so what the full-expression ends is destroyed
	// inside it and what the blocks end is destroyed after it comes off.
	bool full_expression_end;
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
