#pragma once

#include <string>
#include <vector>

#include "sema_scope.h"
#include "type_model.h"

struct AstNode;

// The two typed records the expression layer passes between its steps.
//
// 5p1's expression and 13.3's candidate are what every reading of a body hands
// back and what every overload resolution ranks, so they are the vocabulary of
// that layer rather than of any one of its walks.  They carry facts and never
// text: what the dump would spell is held as the two parts of a line, so a
// conversion written around an operand can spell it again in place.

// One analysed expression.
//
// `type` is what the operators see, so a reference is already removed from
// it (5p5).  `spelled` is what the dump writes, which parts company with
// `type` exactly where the standard makes a reference visible in the
// result of an expression: a call of a function returning a reference, and
// a cast to one.
struct AnalyzedValue
{
	AnalyzedValue();

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
	// 9.3.2p1: a pointer this translation knows holds the address of an
	// object - `this`, which is the object a member function was called on.
	// 4.10p3 has to hand back the null pointer value where the source of a
	// derived-to-base conversion holds one, and where the conversion moves
	// the address at all that test is a branch the program pays for; a
	// pointer that cannot be null needs neither.  False for every pointer a
	// program wrote, which is all but the one the analysis makes.
	bool nonnull;
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
	// 13.3.1p4: the value category the object expression of a member call
	// had.  9.3.1p3 holds the implicit object parameter as a pointer, so
	// the argument this value carries is the object's address and the
	// category it was written with would otherwise be lost - and it is
	// exactly what 8.3.5p1's ref-qualifier binds by.  It says nothing about
	// any value that is not an implied object argument.
	ValueCategory object_category;
	// 7.3.3p1 and 11.2p5: whether this operand is the object a member
	// function a using-declaration brought into its class is called on.
	// The class the name was written on is the naming class, so 4.10p3's
	// conversion to the base subobject the function belongs to is not one
	// the program wrote and the base-specifier's own access is not asked
	// about - which is what lets a private base's member be named.
	bool through_using;
	// 13.3.3.1.5p1: the braced-init-list this argument was written as.  A
	// list is not an expression, so nothing about it can be read before the
	// parameter it reaches is known: the list travels as the value, `node`
	// holds the place it will be written in, and `type` stays unsettled
	// until 8.5.4 has been given a type to read it for.
	const AstNode* braced;
	// 13.3.3.1p4: the class 13.3.1.7's second phase is initializing, where
	// this list is that phase's one element and is itself a list.  A
	// parameter of that class - or of a reference to it - reaches nothing
	// through a user-defined conversion there, which is what stops the
	// class's own copy constructor from competing with the constructor the
	// braces were written for.  `kNoType` everywhere else.
	TypeId listed_class;
};

// 13.3.3.1: how good the conversion of one argument is.  The ranks of
// Table 12, plus what 13.3.3.2p3 and p4 need to tell two of one rank apart.
struct OverloadMatch
{
	OverloadMatch();

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
	// 12.3.2p1 and 13.3.1.5p1: the conversion function this sequence calls,
	// when the argument's class reaches the parameter only through one.
	// What the parameter is given is the value that call handed back.
	SemaEntity* converted;
	// 13.3.3.2p3: the rank of the second standard conversion sequence of a
	// user-defined conversion sequence, which is what orders two of them
	// that call the same conversion function or constructor.  `kExactMatch`
	// for a sequence that is not a user-defined one.
	int second_rank;
	// 13.3.3.1.5p3 and p4: the class a list-initialization sequence
	// initializes, which is the user-defined conversion such a sequence
	// holds - 13.3.3.2p3 orders two of them only where they "initialize the
	// same class", exactly as it orders two ordinary ones only where they
	// hold the same constructor or conversion function.  `kNoType` for a
	// sequence that is not one.
	TypeId list_class;
};
