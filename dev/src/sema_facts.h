#pragma once

#include <string>

#include "type_model.h"

struct SemaEntity;

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
	unsigned long long value;
	// The spelling this node carries that no other fact holds: the tokens a
	// floating literal was written from, whose value is a value of the
	// target's floating type; the code units a string literal holds, which
	// 2.14.5 makes an array object rather than a value; and the identifier
	// 6.1 gives a label, which names a place in the function and no entity.
	std::string spelling;
};
