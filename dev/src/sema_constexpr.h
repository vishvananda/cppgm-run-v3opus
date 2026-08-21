#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sema_address.h"
#include "sema_declaration.h"
#include "type_model.h"

class SemaAnalyzer;
class Scope;
struct AnalyzedValue;
struct AstNode;
struct SemaEntity;

// 6.1-6.6: how a statement a fold ran left the walk of the body around it.
// 6.6.1's `break` and 6.6.2's `continue` are answered by the nearest iteration
// statement and 6.6.3's `return` by the call, so each is carried out of the
// statement that wrote it rather than thrown.
enum class ConstexprFlow
{
	Normal,
	Break,
	Continue,
	Returned
};

// The state one folded call carries while the statements of its body run.
//
// The regions the body's blocks open are opened once per fold rather than once
// per entry, so a loop of n iterations costs the regions its body has and not
// n times that, and the objects those blocks declare are created once and
// written again.  What re-entering a block has to undo is therefore the values:
// 3.3.3p2 gives each pass through a block its own objects, so on entry every
// object the block declared holds nothing again and a name read before the
// declaration that sets it reaches no value this pass wrote.
struct ConstexprFrame
{
	ConstexprFrame()
		: returns(kNoType)
		, steps(0)
		, returned(false)
	{}

	// 6.6.3p2: what the return statement the walk reached handed back.
	SemaConstant result;
	// 6.6.3p2: the type of the object the call hands back, which is the place
	// a `return` statement's operand fills - and 8.5.4p1 makes a
	// braced-init-list standing there the initialization of that place, so the
	// walk has to know it before it reads one.
	TypeId returns;
	// How many statements this fold has run, which is what bounds a loop whose
	// condition the body never falsifies.
	unsigned long long steps;
	bool returned;
	// The region each block of the body opened, keyed by the block.
	std::unordered_map<const AstNode*, Scope*> regions;
	// The object each declaration of the body declared, keyed by the
	// init-declarator or condition-declaration that declares it.
	std::unordered_map<const AstNode*, SemaEntity*> objects;
	// What each of those regions declared, which is what re-entry unsets.
	std::unordered_map<const Scope*, std::vector<SemaEntity*> > declared;
	// 7p3: the declarations of the body that introduce a name and declare no
	// object - a typedef, an alias, a using-declaration, a class.  A name is
	// introduced into a region once, and the regions here are opened once per
	// fold, so these are read on the pass that first reaches them and left
	// alone afterwards: a second reading of a class-specifier defines the class
	// twice, and a second reading of any of them costs a declaration per pass
	// through the loop that holds it.
	std::unordered_set<const AstNode*> introduced;
};

// 7.1.5p2 and 5.19p2: the call of a constexpr function a constant expression
// folds, and the object of literal class type such a call may stand on.
//
// 5.19 reads an expression twice in this compiler - once over the tree, where a
// declaration wrote one, and once over the words a template-argument-list left
// it as - and 5.2.2's call is the one operand neither of those readings can
// answer on its own, because what it comes to is a *body*.  So the call is a
// reading of its own that both of them ask, and it is kept here for the same
// reason `sema_deduce.h` keeps 14.8.2's match: what it walks is a function
// definition and its region, which is neither of the two operand walks'
// business.
//
// 7.1.5p3 leaves such a body one `return` statement, with only typedefs,
// alias-declarations, using-declarations, using-directives, static_asserts and
// null statements around it - so the reading is that grammar exactly.  The body
// is re-read rather than recovered from the walk that analysed it: a fold binds
// its own arguments, and the region it binds them in is one of its own opened
// over the region the declarator gave the parameters.
//
// An object of literal class type is what 5.2.3p2's `T()` and p3's `T{...}`
// come to at a value place, and 12.3.2p1's conversion function is how one
// reaches the integral type 14.1p4 gave the place.  Such an object is a
// `TypeKind::Value` entry like any other constant: its type is the class and
// its bits are the identifier of the interned list of what its subobjects hold,
// so `S{5}` written twice is one object and two objects of one class with
// different members are two.

// 12.6.2p10: one entry of that list, which is 10p1's base class subobject or
// 9.2p1's non-static data member.  The two are told apart because a name
// reaches only the second: a base subobject is initialized by a mem-initializer
// that writes its *type*, converted to by 4.10p3 rather than named, and read
// back through the class that declares whatever member 10.2's lookup found in
// it.
struct Subobject
{
	SemaEntity* entity;
	TypeId type;
	bool base;
};

class ConstexprReading
{
public:
	explicit ConstexprReading(SemaAnalyzer& analyzer);

	// 5.2.2p1 with 7.1.5p2: what `callee(arguments)`, called on `object`, comes
	// to.  `object` is null for a call of a function that is not called on one,
	// which 9.4p1's static member and every non-member are.  Throws
	// `NotConstant` where 7.1.5 leaves the call outside a constant expression.
	// The region the body is read in is opened over `callee`'s own, so the
	// region the *call* was written in says nothing about the answer.
	SemaConstant call(SemaEntity& callee, const SemaConstant* object,
	                  const std::vector<SemaConstant>& arguments);
	// 14p1: whether the declaration a call reached is one no argument list has
	// settled - a member of the class a *pattern* being read declares, or a
	// specialization made over an argument that depends on a parameter.  A
	// template declares no function until it is instantiated, so this reading
	// holds the definition of neither and 14.6p8 stands a value in for the
	// call rather than calling the missing body the program's error.
	bool unsettled_callee(const SemaEntity& callee) const;

	// 5.19p2's three operands whose reading is more than the arithmetic: a
	// name, which may be a place an argument list has yet to settle; a unary
	// or binary operator over what its operands came to; and the one shape the
	// grammar hands on as a call, which is 5.2.3p1's cast, 5.2.3p2/p3's object
	// of literal class type or 5.2.2p1's call of a function.
	SemaConstant id_constant(const AstNode& node, const SemaContext& ctx);
	// The same reading of a declaration a lookup has already reached, which is
	// what a door that looked the name up itself asks rather than looking it up
	// again.  `spelling` is the name the program wrote, for the refusal.
	SemaConstant entity_constant(SemaEntity& entity,
	                             const std::string& spelling);
	SemaConstant unary_constant(const AstNode& node, const SemaContext& ctx);
	SemaConstant binary_constant(const AstNode& node, const SemaContext& ctx);
	// 9.3.2p1: the object the innermost folded call was written on, as the
	// prvalue of pointer type the keyword is.  The fold binds it in the region
	// it opened for the call, so the answer is that binding read back and no
	// question about where the body stands.
	SemaConstant this_constant(const SemaContext& ctx);
	// 9.3.2p1: the binding the innermost folded call made for the object it was
	// called on, told from 8.3.5p10's own declaration of the name by the object
	// it names.  Null where this reading has no such object.
	SemaEntity* folded_this(const SemaContext& ctx) const;
	// 8.3.2p1: which object a declaration of reference type bound its name to,
	// recorded on the declaration so every reading of the name answers from it.
	void bind_declared_reference(SemaEntity& entity, const AstNode& wrote,
	                             TypeId type, const SemaContext& ctx);
	SemaConstant call_or_cast(const AstNode& node, const SemaContext& ctx);
	// 5.2.1p1's subscript and 5.4p4/5.2.9's cast, each of which is one operator
	// with a reading per kind of operand or destination - so each is a reading
	// of its own rather than an arm of the dispatch that reaches it.
	SemaConstant subscript_constant(const AstNode& node, const SemaContext& ctx);
	// 5.2.1p1 over two operands already read, which is what a reading holding a
	// spelling rather than a tree has: the array or pointer on the left and the
	// index on the right, with 13.5.5p1's call the third arm.
	SemaConstant element_at(const SemaConstant& held, const SemaConstant& index,
	                        const SemaContext& ctx);
	bool indexable(const SemaConstant& value) const;
	SemaConstant cast_constant(const AstNode& node, const SemaContext& ctx);

	// The arithmetic of one binary operator over what its operands came to,
	// with 5p10's conversions on them.  It is asked by the reading of a
	// binary-expression and again by 5.17p7's compound assignment and 5.3.2p1's
	// increment, which are that same operator written as a write-back - so the
	// operator is one rule here rather than one per spelling that reaches it.
	SemaConstant binary_value(unsigned token, const SemaConstant& left,
	                          const SemaConstant& right);
	// The same operator where 5p10 brought both operands to one of 3.9.1p8's
	// floating types, which is the arm of it no integer of this translation
	// answers.
	SemaConstant real_value(unsigned token, long double left, long double right,
	                        TypeId type);

	// 4p3 and 6.4p4: whether a value taken as a condition is true, which for an
	// object of class type is what 12.3.2p1's conversion to `bool` hands back.
	bool truth(const SemaConstant& value);

	// 6.1-6.6 over the body of one folded call: `node` run in `ctx`, with
	// `frame` holding what the call has settled so far.  Throws `NotConstant`
	// where the statement is one this evaluation does not run or where
	// something it evaluates is not constant.
	ConstexprFlow statement(const AstNode& node, const SemaContext& ctx,
	                        ConstexprFrame& frame);

	// 5.17p1 and 5.17p7: what `E1 = E2` and `E1 op= E2` come to inside an
	// evaluation, which is the value written back to the object `E1` names -
	// and that object is one the fold itself declared, because 5.19p2 leaves an
	// evaluation nothing else it may write.
	SemaConstant assignment_constant(const AstNode& node, const SemaContext& ctx);
	// 5.3.2p1 and 5.2.6p1: `++E`, `--E`, `E++` and `E--`, which is that same
	// write-back with 1 as the other operand and with `prefix` saying whether
	// the value is the one written or the one that stood there.
	SemaConstant increment_constant(const AstNode& node, const SemaContext& ctx,
	                                bool prefix);

	// 5.2.2p1: what a call whose postfix-expression is the id-expression
	// `callee` comes to, which is the arm of 5.2.3's shape whose name reaches a
	// function rather than a type.  The name is looked up here because it is
	// the one lookup that says which of the two readings was written.
	SemaConstant called(const AstNode& callee,
	                    const std::vector<SemaConstant>& arguments,
	                    const SemaContext& ctx);
	// The same call written as a name rather than as a tree, which is where the
	// reading over a template-argument *spelling* arrives - a flattened
	// argument holds the words a call was written with and no node of its own.
	// Which declaration such a call reaches is 3.4, 3.4.2 and 14.2's answer
	// there as it is anywhere else, so both doors ask this one reading: a
	// template-id names its specializations, an unqualified name also names
	// what its arguments' namespaces declare, and 13.3 ranks the set.
	SemaConstant called_name(const std::string& name, const AstNode* callee,
	                         const std::vector<SemaConstant>& arguments,
	                         const SemaContext& ctx);

	// 13.3, asked of the constants a fold holds: the one declaration of
	// `candidates` no other beats over this argument list, with `object` the
	// implicit object argument 13.3.1p3 gives a call written on one.  It is the
	// analysis's own ranking - so 14.8.2's deduction makes a template a
	// candidate here as it does anywhere else, and 13.3.3.2 tells two
	// declarations apart that the count of their places cannot.  Throws
	// `NotConstant` where the set holds nothing viable, where no candidate is
	// best, and where the one chosen is no constexpr function this unit
	// defined.
	// `associated` is how many of the entries 3.4.2 contributed, which
	// 14.6.4.2p1 asks its question of the other side of.
	SemaEntity& selected(const std::string& name,
	                     const std::vector<SemaEntity*>& candidates,
	                     const std::vector<AnalyzedValue>& written,
	                     const AnalyzedValue* object, std::size_t singles,
	                     std::size_t associated);

	// 13.3.1.2p1: `operands` read as the call of an operator function, which is
	// what an operator written on one of class or enumeration type is.
	//
	// This is the third door 13.3 answers for a fold, beside `selected` for a
	// call and `converted` for a conversion, and it is the same sentence: the
	// set is 13.3.1.2p3's, which `OperatorCall` gathers for the expression
	// layer and for this reading alike, and the ranking is `select_overload`.
	// What a fold adds is only what it always adds - the chosen declaration has
	// to be a constexpr function this unit defined, which `call` asks *after*
	// 13.3 has chosen and never uses to choose.
	//
	// False - and `out` untouched - where 13.3.1.2p2 leaves the built-in
	// operator: no operand of class or enumeration type, no candidate at all,
	// nothing in the set viable, or a built-in candidate that reads these
	// operands better.  Every caller then reads the operands as it always has,
	// through 12.3.2p1's conversion function where one is needed.
	bool operator_constant(unsigned token,
	                       const std::vector<SemaConstant>& operands,
	                       const SemaContext& ctx, SemaConstant& out);
	// 13.3.1.2p2: whether an operand makes the operator a call to look a
	// declaration up for at all, which is the one test every door runs before
	// gathering anything.  The two spellings are the same clause asked of a
	// value and of the type a declaration gave one.
	bool overloadable_operand(const SemaConstant& value) const;
	bool overloadable_place(TypeId type) const;
	// 13.3.1.2p3 over one operand: whether any declaration of the operator is
	// reached from it.  5.14p1's short-circuit belongs to the built-in operator
	// alone, so the door that has to leave an operand unread asks this of the
	// one it has read before deciding not to read the other.
	bool reaches_operator(unsigned token, const SemaConstant& operand,
	                      const SemaContext& ctx);

	// 8.5p14 and 8.5p16: `value` read where a place of type `place` is
	// initialized from it.  A place of class type the value is not already of
	// is 13.3.1.3's constructor called with that one value, which is the
	// conversion 13.3 chose when it ranked the call and this reading performs;
	// a place of any other type is the value as it stands, because `convert`
	// answers the arithmetic ones where they are reached.  It is what 5.2.2p4's
	// argument and 6.6.3p2's returned value are each read through, so a
	// constant crossing into or out of a call is one rule and not two.
	SemaConstant at_class_place(const SemaConstant& value, TypeId place);
	// The same reading where the place may also be 8.3.2p1's reference, which
	// binds to the object the value designates rather than taking a copy of it -
	// so what the expression is worth afterwards is that object read back.
	SemaConstant at_object_place(const SemaConstant& value, TypeId place);

	// 5.2.3p2 and p3: the object `T()` and `T{...}` stand for where `T` names a
	// class, as the constant that holds its subobjects' values.  `written` is
	// what the clauses came to.  8.5.1p1 says which of the two initializations
	// that is: an aggregate takes them as 8.5.1p2's clauses, one per member in
	// declaration order with 8.5.1p7 value-initializing the rest, and every
	// other class takes them as 8.5p16's direct-initialization, whose object is
	// what 12.6.2's mem-initializers of the constructor 13.3.1.3 chose come to.
	SemaConstant object_of(TypeId type,
	                       const std::vector<SemaConstant>& written);

	// 5.19p3 and 7.1.5p9: what the object a declaration declares is *worth*,
	// where its type is const and the initializer it wrote is a constant
	// expression.  It is one reading and not one per kind of object: 3.9p10
	// makes a literal class type and an array of one constants of the same
	// standing as an arithmetic type, and each of the three is folded here.
	// A declaration that folds nothing leaves the object holding no constant
	// and is not itself ill formed, so the one failure is caught - unless
	// `required` says 7.1.5p9 asked for the fold, in which case the refusal is
	// this declaration's own diagnostic and is let through.  It is let through
	// only where the refusal is a *covered* one: `NotConstant::covered` says
	// 5.19 answered about the program, and `ConstexprRequirement::valued_type`
	// says a constant of the object's type is one this build holds at all.
	// Returns false where neither held - the reading ran out - which is what
	// leaves 7.1.5p9's requirement beside it nothing to ask.
	//
	// `before_the_program` is 3.6.2p2's own question beside 5.19p3's: whether
	// the declaration gives the object static storage duration, and so an image
	// this fold's answer is what the program finds in.
	bool fold_declared_object(SemaEntity& entity, const AstNode* initializer,
	                          TypeId type, const SemaContext& ctx,
	                          bool required, bool before_the_program);

	// 8.5: what the initializer `wrote` leaves an object of type `type`
	// holding, which is the one reading both doors a declaration stands at ask
	// - the fold of a declaration the program wrote, and the declaration
	// statement 6.7p1 runs inside an evaluation.  Throws `NotConstant` where
	// the initializer is not one 5.19 reads.
	SemaConstant initialized_value(const AstNode& wrote, TypeId type,
	                               const SemaContext& ctx);

	// 8.5.1p2: what one initializer-clause comes to for the subobject it
	// initializes, which is the reading a *list* of clauses is read through.
	//
	// Which reading answers a clause is the type of that subobject and not the
	// syntax of the clause: `{1,2}` written for an element of class type is
	// 8.5.1p1's initialization of that element and not an expression at all, and
	// `1` written for the same element is 8.5p16's copy-initialization of it,
	// which for a class is a constructor 13.3 chooses.  So the clauses of a
	// nested list are read against the types of the subobjects they reach, one
	// pass down the object, and `object_of` and `array_of` below are handed the
	// values that reading came to.
	SemaConstant clause_of(const AstNode& clause, TypeId target,
	                       const SemaContext& ctx);

	// 8.5.1p2 and 8.5.2p1 over the whole of one list: the two questions only the
	// place the braces stand can answer - whether the one clause in them is the
	// string literal that initializes the whole array, and whether every clause
	// reached a subobject.  Every place a list stands asks it.
	SemaConstant list_constant(TypeId bare, const AstNode& list,
	                           const SemaContext& ctx);
	// 8.5.1p2 over one aggregate, taking what it needs out of a list the caller
	// may still be walking.  8.5.1p11 lets the braces around a subaggregate's
	// clauses be left out, so how many clauses an aggregate takes is what its
	// own walk of its subobjects arrives at rather than a count worked out in
	// front of it - which is why the cursor and not a list is the argument, and
	// why the walk goes one *subobject* per step and not one clause.
	SemaConstant aggregate_constant(TypeId bare, InitializerClauses& clauses,
	                                const SemaContext& ctx);
	// One subobject of that walk: the clause standing here where its braces
	// were written, the run of clauses its own subobjects take where 8.5.1p11
	// left them out, and 8.5.2p1's code units where the clause is a string
	// literal for an array of character type.
	SemaConstant subobject_constant(TypeId type, InitializerClauses& clauses,
	                                const SemaContext& ctx);
	// 8.5.2p1: the array of character type a string literal initializes, whose
	// elements hold the literal's own code units and no clause at all.  False
	// where the initializer is no string literal for this array.
	bool string_constant(TypeId array, const AstNode& written,
	                     const SemaContext& ctx, SemaConstant& out);

	// 8.5.1p2 and p7 over an array: the same reading where the subobjects the
	// clauses reach are 8.3.4p6's elements rather than 9.2p1's members, so
	// which one a clause initializes is its position and no lookup at all, and
	// the elements past the last clause are value-initialized.
	SemaConstant array_of(TypeId type,
	                      const std::vector<SemaConstant>& written);
	// 5.2.1p1: the element a subscript of such an array names.
	SemaConstant element_value(const SemaConstant& array,
	                           unsigned long long index);

	// 5.19p2's address constant expression, whose whole reading is
	// `sema_address.cpp`'s.  A constant of pointer type carries the identifier
	// `AddressTable` interned the object it designates under, and 3.10p1's
	// glvalue - the object an *expression* designated - is `SemaConstant::object`
	// beside it.
	//
	// `designated` is the lvalue reading beside `evaluate`'s value one: what
	// object an expression names, which `static int n;` has and no value of.
	// `loaded` is the other direction, 5.3.1p1's `*`, and `advanced` 5.7p5's
	// arithmetic.  `at_pointer_place` and `at_reference_place` are 4.2p1's decay
	// and 8.3.2p1's binding, asked wherever an initialization reaches a place of
	// one of those types, and `static_address` is 5.19p2's own requirement on a
	// pointer a declaration keeps.
	std::uint32_t designated(const AstNode& node, const SemaContext& ctx,
	                         bool value_fallback = true);
	std::uint32_t designated_entity(SemaEntity& entity,
	                                const std::string& spelling);
	SemaConstant loaded(std::uint32_t address);
	SemaConstant held_at(std::uint32_t address);
	SemaConstant pointer_constant(std::uint32_t address, TypeId place);
	TypeId address_type(std::uint32_t address) const;
	bool holds_address(const SemaConstant& value) const;
	bool null_pointer_operand(const SemaConstant& value) const;
	bool address_operation(unsigned token, const SemaConstant& left,
	                       const SemaConstant& right, SemaConstant& out);
	bool at_pointer_place(const SemaConstant& value, TypeId place,
	                      SemaConstant& out, bool contextual = false);
	SemaConstant at_reference_place(const SemaConstant& value, TypeId place);
	// 8.5.3p4 with 4.10p3 and 5.2.9p11: which object such a binding names where
	// the reference and the operand are two classes of one derivation - the
	// base class subobject going down, the object it is part of coming back up.
	std::uint32_t bound_object(const SemaConstant& value, TypeId bound);
	bool static_address(const SemaConstant& value) const;
	// 5.19 asked of a refusal about an object this reading holds no value of:
	// the program's error where 5.19 gives the object none, and this build's
	// edge where the object's own fold ran out.
	bool covered_object(std::uint32_t address) const;
	bool literal_object(const std::string& spelling, SemaConstant& out);
	SemaConstant subscripted(const SemaConstant& pointer,
	                         const SemaConstant& index);
	// 8.5.4p1: a braced-init-list is no expression, so what one standing here
	// comes to is the initialization of the place it fills - which is what
	// `place` says, where the reader knows it.
	SemaConstant operand_constant(const AstNode& node, const SemaContext& ctx,
	                              TypeId place = kNoType);
	SemaConstant decayed_operand(const SemaConstant& value);
	SemaEntity* called_through(const SemaConstant& value);
	SemaConstant returned_constant(const SemaConstant& answer);

	// True where the constant stands for such an object rather than for a value
	// of arithmetic type, which is what says its bits are a list identifier.
	// `is_object` is the reading that asks for a *class*, because 5.2.5p1's
	// member access and 12.3.2p1's conversion function are questions only a
	// class answers; `holds_list` is the same fact about the bits.
	bool is_object(const SemaConstant& value) const;
	bool holds_list(const SemaConstant& value) const;
	// 5.19p2 with 3.9.1p8: whether a subobject of `type` is one the constants
	// hold at all, which 8.3.4p6's element, 9.2p13's member and the member a
	// constructor initializes each ask - so the kinds there are are written
	// once and not once per walker.
	bool valued_subobject(TypeId type) const;

	// 5.2.5p1 over an object a constant expression holds: what `E.m` comes to
	// where `m` is 9.2p1's non-static data member, which is the subobject the
	// object's own list holds for it.  A call written on such an object is the
	// same access with 5.2.2p1's arguments after it, and both reach the class's
	// members through the one lookup `member_named` is.  5.2.5p1's `->` is left
	// out of both: its left operand is a pointer, which 5.19p2 leaves a
	// constant expression none of.
	//
	// 5.19 is read twice in this compiler, and an access is written in either -
	// `p.y` and `pt{2,5}.sum()` stand in a template-argument-list as readily as
	// in a declaration.  So the rule is asked of the object and the name alone,
	// which is all the reading over a spelling holds; the two below are the tree
	// reading's own way of getting to those and ask exactly these.
	SemaConstant member_value(const SemaConstant& object,
	                          const std::string& name, const SemaContext& ctx);
	SemaConstant member_call(const SemaConstant& object, const std::string& name,
	                         const std::vector<SemaConstant>& arguments,
	                         const SemaContext& ctx);
	SemaConstant member_constant(const AstNode& node, const SemaContext& ctx);

	// 5.19p3: `value` read where the place asks for a number.  A constant that
	// stands for an object of class type holds the identifier of the list its
	// subobjects came to and no value of arithmetic type at all, and 5.19p3
	// leaves a converted constant expression its user-defined conversions - so
	// what such a constant is worth at an arithmetic place is what 12.3.2p1's
	// conversion function hands back, and every other constant is itself.
	// `place` is the type the place asked for, or `kNoType` where it asks only
	// that the value be one an arithmetic reading can take.
	//
	// Every reading that takes a constant as a number asks this: an array
	// bound, an enumerator, a case label, an alignment, a bit-field width, a
	// count, and `convert` and `promote` for everything that reaches an
	// operator or an initialization through them.
	//
	// `contextual` is 12.3.2p2's other door: 4p3's contextual conversion is the
	// one place a conversion function declared `explicit` answers, and every
	// place named above is 5.19p3's *implicit* conversion sequence, which it
	// does not.
	SemaConstant at_arithmetic_place(const SemaConstant& value, TypeId place,
	                                 bool contextual = false);

	// The same reading where the place *counts* rather than holds a number: an
	// array bound, a subscript, a shift width.  Each of those is an integral
	// constant expression, so 4.9's conversion of a floating value is no part
	// of what reaches it and a value of floating type is refused there rather
	// than truncated to one.
	unsigned long long counted(const SemaConstant& value);

	// The same reading taken where the expression *stands*, which is what lets
	// 14.6p8 travel with the count: 8.3.4p1's bound, 7.2p1's enumerator,
	// 9.6p1's bit-field width and 7.6.2p1's alignment each count with a
	// constant expression written where a pattern may stand, and none of them
	// is answered by a reading that stood a value in for something an argument
	// list settles.  False says so, on a refusal as much as on a value.
	bool counted_where(const AstNode& node, const SemaContext& ctx,
	                   SemaConstant& value, unsigned long long& count);

	// 8.3.4p1's bound, the one of those places whose count is part of a *type*:
	// how many elements the array has, and - through `place` - the template
	// place the constant-expression named where an argument list has yet to
	// settle one.  `SpelledTypeId` answers the same clause off the text 14.2
	// left a type-id as; the two readings differ in where the expression comes
	// from and in nothing else.
	unsigned long long array_bound(const AstNode& node, const SemaContext& ctx,
	                               TypeId& place);
	// The place a bound named, `kNoType` for a bound that named anything else.
	// 14.6.2p1 leaves a lone identifier the one expression a substitution can
	// put a number back into, which is the same reading a value template
	// argument written as one name already gets.
	TypeId named_place(const AstNode& node, const SemaContext& ctx);
	// 8.3.4p1's bound where the expression is more than one place: the tree,
	// the region and `std::size_t` kept the way 14.1p9's default is, so that
	// 14.3p1 can evaluate it again over the arguments.
	TypeId dependent_bound(const AstNode& node, const SemaContext& ctx);

	// 12.3.2p1 with 14.3.2p5: `value`, an object of class type, brought to the
	// type `place` by a conversion function of its class.
	//
	// Which conversion function is 13.3.3.1.2's user-defined conversion
	// sequence, which the analysis already answers for every other reader of
	// one - so this asks `conversion_match` rather than ranking a set of its
	// own, and asks `builtin_conversion_type` for the type where the place
	// named none.  A fold adds nothing to that question: 12.3.2p2's `explicit`,
	// 8.4.3p2's deleted declaration, 10.2's conversions of a base and the
	// cv-qualification of the object are the same facts here as anywhere else,
	// and whether the declaration chosen is a constexpr one this unit defined
	// is asked *after* 13.3 has chosen and never used to choose.  Throws
	// `NotConstant` where no conversion is viable and where two are.
	SemaConstant converted(const SemaConstant& value, TypeId place,
	                       bool contextual);

	// 5.3.7p1 and p2: `noexcept(E)`, whose result is a constant of type `bool`
	// - which is what puts it here rather than beside the operators that
	// compute one.  Its operand is an unevaluated operand, so it is read once
	// through `probe_expression` and 5.3.7p3 is asked of the resolved tree that
	// reading leaves: 13.3 has chosen every call in it, and what a call is
	// worth to this question is 15.4's exception-specification of the
	// declaration it chose.  `noexcept_value` is the expression layer's line
	// for the operator, spelled as the `bool` literal the value is;
	// `nonthrowing_operand` is the same answer where a constant expression
	// asks for it.
	AnalyzedValue noexcept_value(const AstNode& node, const SemaContext& ctx,
	                             DumpNode& parent);
	bool nonthrowing_operand(const AstNode& node, const SemaContext& ctx);

	// 15.4p1: the condition an exception-specification wrote is a constant
	// expression, so folding it is a reading of this kind and not a reading of
	// the declarator.  9.2p2 regards a class as complete inside one, so a
	// condition written on a member declarator is kept by `defer_specification`
	// where the fold at the declarator could not answer it and folded by
	// `settle_specifications` where the class-specifier closes.
	bool specification_holds(const AstNode& condition, const SemaContext& ctx);
	void defer_specification(SemaEntity& function, const AstNode& declarator,
	                         const SemaContext& ctx);
	void settle_specifications(Scope& scope, const SemaContext& ctx);

private:
	ConstexprReading(const ConstexprReading&);
	ConstexprReading& operator=(const ConstexprReading&);

	// The halves of the address reading the readings above are written from:
	// 2.14.5p8's literal object, 9.2p13's member of an addressed object, 5.7p5's
	// bound and distance, and the array or pointer a subscript walks.
	TypeId literal_type(const std::string& spelling) const;
	unsigned long long address_bound(const ConstantAddress& held) const;
	std::uint32_t subobject_address(std::uint32_t base,
	                                unsigned long long index);
	std::uint32_t member_address(std::uint32_t object, const std::string& name,
	                             const SemaContext& ctx);
	std::uint32_t pointed_object(const SemaConstant& value);
	std::uint32_t array_object(const AstNode& node, const SemaContext& ctx,
	                           bool value_fallback);
	std::uint32_t advanced(std::uint32_t address, long long step);
	long long address_distance(std::uint32_t left, std::uint32_t right);

	// The entry `value` interns as, which is what keys a fold and what a folded
	// answer is read back out of.
	TypeId entry_of(const SemaConstant& value) const;
	static SemaConstant constant_of(TypeId entry, const TypeTable& types);

	// 13.3.3.1 and 13.3.1p3: the constants a fold holds, as the analysed
	// argument list a ranking of candidates reads - one prvalue per written
	// argument, and 9.3.1p3's pointer for the object a call was written on.
	void argument_values(const std::vector<SemaConstant>& arguments,
	                     std::vector<AnalyzedValue>& out) const;
	AnalyzedValue argument_value(const SemaConstant& value) const;
	AnalyzedValue object_value(const SemaConstant& object) const;
	// 3.4, 3.4.2 and 14.2: the declarations `name` reaches, appended to
	// `candidates`, with `singles` taking how many of them are the single
	// friend declarations 11.3p6 made.  Null where nothing of the name is
	// declared, and the declaration itself where the name reaches something
	// that is no function - 13.5.4p1's object of class type is the one such
	// name a call may still be written on.  `callee` is the node the name was
	// written as where there is one, which 7.1.6.2p1's decltype-specifier is
	// the whole of the use for; a flattened spelling has none.
	SemaEntity* callee_candidates(const std::string& name,
	                              const AstNode* callee,
	                              const SemaContext& ctx,
	                              const std::vector<AnalyzedValue>& written,
	                              std::vector<SemaEntity*>& candidates,
	                              std::size_t& singles,
	                              std::size_t& associated);

	// 6.3p1, 6.4p3 and 6.5.3p1: the region `node` opens, which is opened the
	// first time the fold reaches it and reused afterwards.  Every object the
	// last pass through it declared is unset here, so 3.3.3p2's fresh objects
	// cost no region and no entity of their own.
	SemaContext block_region(const AstNode& node, const SemaContext& ctx,
	                         ConstexprFrame& frame);
	// The object `key` declares, created in `ctx`'s region on the first pass
	// through the block and written again on every later one, and null before
	// the pass that creates it.
	SemaEntity* declared_object(const AstNode& key, ConstexprFrame& frame) const;
	SemaEntity& local(const AstNode& key, const std::string& name, TypeId type,
	                  const SemaContext& ctx, ConstexprFrame& frame);
	// 7p3: a declaration of the body that introduces a name and declares no
	// object, read on the pass that first reaches it.
	void introduce(const AstNode& node, const SemaContext& ctx,
	               ConstexprFrame& frame);
	// 7p3 with 8.3: the type and the name one declarator of `specifiers`
	// declares, read without declaring anything - an evaluation's object is a
	// binding of the fold and not a declaration of the program.
	TypeId declared_type(const AstNode& specifiers, const AstNode& declarator,
	                     const SemaContext& ctx, std::string& name);
	// 6.7p1: a declaration-statement of the body, run where it stands.  8.5p11
	// leaves an object with no initializer holding no value, which is what
	// makes reading one before it is written not a constant expression.
	void declared(const AstNode& node, const SemaContext& ctx,
	              ConstexprFrame& frame);
	// 6.4p1 and 6.4p4: what the condition of a selection or iteration statement
	// comes to, including the arm that declares an object and takes its value.
	bool condition_value(const AstNode& node, const SemaContext& ctx,
	                     ConstexprFrame& frame);
	ConstexprFlow selection(const AstNode& node, const SemaContext& ctx,
	                        ConstexprFrame& frame);
	ConstexprFlow iteration(const AstNode& node, const SemaContext& ctx,
	                        ConstexprFrame& frame);
	ConstexprFlow counted_loop(const AstNode& node, const SemaContext& ctx,
	                           ConstexprFrame& frame);
	// 3.4.1: the declaration the operand of an assignment or an increment
	// names, or null where what stands there is no name at all.  It is asked
	// before either reading of the operator, because a name is where an
	// operand's type is read from without evaluating it.
	SemaEntity* named_object(const AstNode& node, const SemaContext& ctx);
	// 5.17p1 and 5.19p2: the object the *built-in* assignment and increment
	// write, which is a name this fold bound and nothing else.  Asked only
	// after 13.3.1.2 has named no operator function, because that is the one
	// reading the object belongs to.
	SemaEntity& written_object(SemaEntity* named);
	// The constant an object a fold declared is holding.
	SemaConstant held_constant(const SemaEntity& object) const;
	// 13.5p1: the assignment or increment read as the call 13.3.1.2 names, over
	// the operand `named` declares or the one evaluating it comes to.  `right`
	// is the second operand where the program wrote one, and `postfix` asks for
	// 13.5.7p1's zero where it did not.
	bool written_operator(const AstNode& node, const SemaContext& ctx,
	                      SemaEntity* named, bool postfix,
	                      const SemaConstant* right, SemaConstant& out);
	// What an evaluation has run so far, refused past the bound below.
	void step(ConstexprFrame& frame);

	// The region a fold of `callee` reads its body in: a region of its own over
	// the one the declarator opened, binding each parameter to what its
	// argument came to and, for a call on an object, each non-static data
	// member of that object's class to what the object holds for it.
	void bind_arguments(SemaEntity& callee, const SemaConstant* object,
	                    const std::vector<SemaConstant>& arguments,
	                    const SemaContext& inner);
	// `written` is 5.19p2's leave to change the binding: a place the call
	// filled is an object whose lifetime began inside the evaluation, and a
	// subobject of the object the call was written on is one whose lifetime
	// began before it.
	SemaEntity& bind_constant(const std::string& name, const SemaConstant& value,
	                          const SemaContext& inner, bool written);
	// 8.3.5p10 with 14.5.3p4: the name of each place `callee`'s declarator
	// opened, one per place of its type - so an entry written `pattern...` is
	// as many of them as the run 14.8.2 bound it to holds, spelled the way
	// 14.5.3p4's own reading looks them up.  `runs` takes that length where a
	// run begins.
	// `empty` takes the pack's own name where the run it was bound to holds no
	// element, which is a declaration the body may still name and no place.
	void declared_places(SemaEntity& callee, std::vector<std::string>& out,
	                     std::vector<unsigned>& runs, std::string& empty) const;

	// 5.2.2p4 with 8.3.6p1: what the places of `callee` are filled with, which
	// is each written argument converted to the type of the place it reached
	// and, for every place the written list stopped short of, the
	// default-argument the declaration wrote - read in the region 8.3.6p9
	// leaves it to rather than in the one the call stands in.  The answer is
	// `TypeTable::type_list` of the object and those values, which is the key
	// the fold is held under: a call that writes an argument and one that lets
	// the default stand for it are the same fold.
	std::uint32_t passed_arguments(SemaEntity& callee,
	                               const SemaConstant* object,
	                               const std::vector<SemaConstant>& arguments,
	                               std::vector<SemaConstant>& passed);

	// 8.5p16 with 12.6.2: the object `T(x)` and `T{x}` come to where 8.5.1p1
	// leaves the class no aggregate, which is what each mem-initializer of the
	// chosen constructor comes to in a region binding its places - and, as
	// 12.6.2p10 initializes in declaration order, the members already settled.
	SemaConstant object_from_constructor(TypeId bare, SemaEntity& owner,
	                                     const std::vector<SemaConstant>& written);
	// 12.6.2p2 and p10: the initializer-clause the ctor-initializer wrote for
	// each subobject, indexed the way `SemaAnalyzer::base_key` indexes it - the
	// whole name of a class a mem-initializer-id names a direct base of, the
	// last component of every other, which is what tells `Payload(p)` naming a
	// base from a member of that name.  The subobjects are initialized in
	// declaration order and the mem-initializers may be written in any, so which
	// one names each is asked once per subobject rather than by a scan of the
	// list per subobject - which is what keeps a constructor of n of them n
	// readings and not n^2.
	void mem_initializers(
		const AstNode* initializers, SemaEntity& owner, const SemaContext& inner,
		std::unordered_map<std::string, WrittenMemInitializer>& out);
	// 12.6.2p10 and 9.5p2: the entries an object of the class holds, one per
	// subobject in layout order, taken from that index.  9.5p1's own object is
	// the entry no mem-initializer-id names, so the walk descends through it and
	// reads the mem-initializers that named the members declared inside it.
	void subobject_entries(
		TypeId bare,
		std::unordered_map<std::string, WrittenMemInitializer>& written_for,
		const SemaContext& inner, std::vector<TypeId>& holds);
	// 12.6.2p10 over one entry of that walk: what the base class subobject or
	// the non-static data member `held` is initialized with - the
	// mem-initializer that named it, else 12.6.2p8's brace-or-equal-initializer,
	// else 8.5p6's default-initialization, which for a class is its own default
	// constructor and for anything else is no value at all.
	SemaConstant subobject_initialized(TypeId bare, const Subobject& held,
	                                   const WrittenMemInitializer* wrote,
	                                   const SemaContext& inner);
	// 14.6p8 over 14.5.3p4's expansion in a mem-initializer's expression-list:
	// the run its packs are bound to has to be one an argument list settled,
	// because until it is there is no saying how many clauses were written.
	void settled_clauses(const InitializerClauses& clauses, TypeId bare);
	// 9.3.1p3 and 9.2p1: the subobjects of the object a call was written on,
	// bound in the fold's region under the names their own declarations gave
	// them, so a member named with no object expression reads the one this
	// object holds.  10.2 finds a base's member from the derived class too, and
	// a member of the derived class hides one of that name in a base - so the
	// class's own members are bound first and each base is walked after them.
	void bind_subobjects(const SemaConstant& object, const SemaContext& inner);

	// 5.2.5p1: the object a member access is written on, which a constant
	// expression holds only where it is one of literal class type, and what the
	// name after the `.` denotes in that object's class.
	SemaConstant accessed_object(const AstNode& node, const SemaContext& ctx);
	// `found`, when given, takes the declarations that lookup associated with
	// the name, which for a member function is the set 13.3 chooses from.
	SemaEntity* accessed_member(const SemaConstant& object,
	                            const std::string& name, const SemaContext& ctx,
	                            std::vector<SemaEntity*>* found = nullptr);
	// 5.2.2p1 with 9.3.1p3: a call whose postfix-expression is that access,
	// which runs on the object where 9.2p1's member function is not static.
	SemaConstant member_called(const AstNode& callee,
	                           const std::vector<SemaConstant>& arguments,
	                           const SemaContext& ctx);

	// 14.7.1p1: the definition of the class `bare` names, asked for where a
	// reading is about to want a fact only the definition settles - 8.5.1p1's
	// aggregate, 9.2p13's members, 12.1's constructors.
	void ask_for_definition(TypeId bare);

	// 12.6.2p10: the subobjects of `type`, in the order an object of it is
	// initialized and laid out - 10p1's base class subobjects first, then
	// 9.2p13's non-static data members - which is the order 8.5.1p2's clauses
	// reach them in and the order the interned list is indexed in.
	void subobjects(TypeId type, std::vector<Subobject>& out) const;

	// 10.2 and 9.2p13: the entries of an object of `type` that reach the
	// subobject `named` declares, appended to `path` from the object down.  A
	// member the class itself declares is one step; one 10.2 found in a base is
	// the step to that base class subobject and this walk repeated inside it.
	// False where no subobject of the object declares it, which 9.4p2's static
	// member and a member of an unrelated class both are.
	bool member_path(TypeId type, SemaEntity& named,
	                 std::vector<unsigned long long>& path);
	// 9.2p13 over one step of such a path: the entry the object's own interned
	// list holds at `index`, carrying 3.10p1's subobject where the whole object
	// was one an expression designated.
	SemaConstant subobject_value(const SemaConstant& object,
	                             unsigned long long index);
	// 4.10p3: the base class subobject of `value` that an object of the class
	// `base` names, which is that walk taken down the derivation rather than to
	// a member.  `value` shall be of a class derived from it, which is what
	// `Derivation::base_in` answers before this is asked.
	SemaConstant base_subobject(const SemaConstant& value, SemaEntity& base);
	// 5.2.9p11 read the other way: the object a base class subobject is part
	// of, where `derived` names the class the cast asked for.  Zero where the
	// address designates no base subobject of such an object, which is
	// 5.2.9p11's undefined case rather than a step this reading may take.
	std::uint32_t containing_object(std::uint32_t address, TypeId derived);

	// 5.3.7p3 over the resolved tree one reading of an unevaluated operand
	// left: false where a line of it is a call the specification of whose
	// callee allows an exception, and true where none is.
	bool nonthrowing_tree(const DumpNode& node) const;

	SemaAnalyzer& analyzer_;
};

// 7.1.5: what a declaration that wrote `constexpr` shall be, asked where the
// declaration stands rather than where a use of it does.
//
// The fold beside this answers what an expression *comes to*; this answers
// whether the program was allowed to write the declaration at all.  They are
// two questions and not one: 7.1.5p3's return type, p4's initialized members
// and p9's constant initializer are requirements on the declaration, and a
// compiler that asks only whether some later fold happened to succeed accepts
// every one of them silently and lowers a dynamic initialization for a
// `constexpr` object 3.6.2 gives no constant one.  7.1.5p5 says such a program
// is ill-formed with no diagnostic required; this is that diagnostic.
//
// 3.9p10's literal type is the fact all of them read, and it belongs to the
// class rather than to any declaration that names one - so it is settled where
// 9.2p2 completes the class, beside 12.1p5's triviality and 8.5.1p1's
// aggregate, and every requirement here is one read of it.
class ConstexprRequirement
{
public:
	explicit ConstexprRequirement(SemaAnalyzer& analyzer);

	// 3.9p10: whether an object of `type` may stand where a constant expression
	// builds one.  A reference and a scalar are literal types outright, an
	// array is one where its element type is, and a class carries its own
	// answer.  True for every type the class walk has no answer for yet -
	// 14.6.2p1's dependent type among them - because a requirement refuses a
	// declaration only where the answer is known to be no.
	bool literal_type(TypeId type) const;
	// Whether 5.19's constant values cover an object of `type` completely,
	// which is what says a fold that came to nothing found the program's error
	// rather than this build's gap.  Pointers, references and pointers to
	// members are the gap: no `SemaConstant` holds an address.
	bool valued_type(TypeId type) const;
	// Whether 7.1.5p9's constant initializer is a requirement this reading may
	// hold a declaration to: the object's type is one the values cover, and the
	// declaration is one the program wrote rather than one a template pattern
	// or an instantiation of it did.  It is the one guard both halves ask - the
	// fold, which lets its own refusal through where this says yes, and the
	// requirement below, which asks for a constant where it does.
	bool demanded(TypeId type, const SemaContext& ctx) const;

	// 3.9p10 and 12.1p5 over the class `scope` declares, settled where the
	// class-specifier closes and read from `SemaEntity::literal_class` and
	// `::valued_class` afterwards.
	void settle_class(SemaEntity& entity, Scope& scope) const;
	// 12.1p5: whether the default constructor the standard defines for this
	// class is a constexpr constructor - which it is exactly where a written
	// `constexpr X() {}` would satisfy 7.1.5p4, so every member is reached by a
	// brace-or-equal-initializer or is itself built by one of these.  `holds`
	// asks that last bullet: with it the answer says whether running the
	// constructor leaves a constant, and without it the answer is the narrower
	// one 3.9p10's third bullet reads about the class.
	bool constexpr_default_construction(Scope& scope, bool holds) const;
	// The same question over one base or one member of class type, which is
	// where the two readings differ: `holds` wants the constructor this fold
	// walks, and 3.9p10 wants only a class that has one and is literal itself.
	bool subobject_default_construction(TypeId type, const SemaEntity& owner,
	                                    bool holds) const;

	// 7.1.5p9: `entity`, declared `constexpr` with `type`, shall have literal
	// type and shall be initialized by a constant expression.  The second half
	// is asked only where `valued_type` says this build could have held the
	// answer and `covered` says the fold beside it read the initializer to an
	// end rather than running out of values partway.
	void require_object(const SemaEntity& entity, TypeId type,
	                    const SemaContext& ctx, const AstNode* initializer,
	                    bool covered) const;
	// 7.1.5p9 with 8.5p6: whether the declaration wrote no initializer and
	// default-initialization of its type performs none either, which is a
	// refusal about the declaration and not about the values - so it is asked
	// where `valued_type` says nothing.
	bool uninitialized(TypeId type, const AstNode* initializer) const;
	// 7.1.5p3: the return type and each parameter type of a constexpr function
	// shall be a literal type, and 7.1.5p3's first bullet leaves it non-virtual.
	// `type` is the function type, whose first parameter is the object one
	// where 9.3.1p3 gave the declaration one.
	void require_function(const SemaEntity& entity, TypeId type,
	                      const std::string& name) const;
	// 7.1.5p4: every non-static data member and base class subobject shall be
	// initialized.  It is asked from the walk 12.6.2p10 already makes down the
	// members of a constructor's class, at the two places that walk finds
	// nothing initializing one - so the requirement is one read per member the
	// definition leaves alone and no walk of its own.
	void require_initialized(const SemaEntity& function,
	                         const std::string& subobject) const;

private:
	ConstexprRequirement(const ConstexprRequirement&);
	ConstexprRequirement& operator=(const ConstexprRequirement&);

	// 14.6p8 and 7.1.5p6: whether this reading is of a template's pattern or of
	// a declaration an instantiation made from one, neither of which is a
	// declaration these requirements are asked of.
	bool instantiated() const;

	SemaAnalyzer& analyzer_;
};

// 7.1.5p5 and Annex B: how deep a chain of folded calls may stand.  A constexpr
// function that calls itself recurses once per level in this walk as well as in
// the program, so the depth the machine stack allows is what bounds it; the
// standard's own recommended minimum is 512.
const unsigned kMaxConstexprDepth = 2048;

// How many statements one folded call may run.  A loop whose condition its body
// never falsifies would otherwise not end, and no bound on the *depth* of the
// walk catches it, because such a loop stands at one depth.  The standard's own
// recommended minimum for the full-expressions an evaluation reads is 1048576.
const unsigned long long kMaxConstexprSteps = 1048576;
