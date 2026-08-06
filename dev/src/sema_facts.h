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
	Sizeof,
	BracedInitList,
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
	// 2.14.4: the tokens a floating literal was written from.  Its value is a
	// value of the target's floating type, which no integer holds, so what the
	// program wrote is what a lowering writes again.
	std::string spelling;
};
