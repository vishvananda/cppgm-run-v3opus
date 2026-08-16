#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sema_declaration.h"
#include "type_model.h"

class SemaAnalyzer;
class Scope;
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
		: steps(0)
		, returned(false)
	{}

	// 6.6.3p2: what the return statement the walk reached handed back.
	SemaConstant result;
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

	// 5.19p2's three operands whose reading is more than the arithmetic: a
	// name, which may be a place an argument list has yet to settle; a unary
	// or binary operator over what its operands came to; and the one shape the
	// grammar hands on as a call, which is 5.2.3p1's cast, 5.2.3p2/p3's object
	// of literal class type or 5.2.2p1's call of a function.
	SemaConstant id_constant(const AstNode& node, const SemaContext& ctx);
	SemaConstant unary_constant(const AstNode& node, const SemaContext& ctx);
	SemaConstant binary_constant(const AstNode& node, const SemaContext& ctx);
	SemaConstant call_or_cast(const AstNode& node, const SemaContext& ctx);

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
	// The same call where the name has already been looked up, which is where
	// the reading over a template-argument spelling arrives: it has resolved
	// the word to tell 5.2.3's cast from 5.2.2's call in the first place.
	SemaConstant called_entity(SemaEntity& named,
	                           const std::vector<SemaConstant>& arguments);

	// 13.3 as far as a fold has to ask: the declaration of the set `named`
	// heads that this many arguments reach and that this unit has a constexpr
	// definition of, or null where the set holds no such declaration or holds
	// more than one.  A fold has constants and not typed expressions, so the
	// arity is the whole of what it can rank by - and a name whose set leaves
	// the choice open is one it refuses rather than guesses at.  8.3.6p1 makes
	// that arity a range: a declaration whose later places all carry a
	// default-argument is reached by a call that stops short of them.
	SemaEntity* chosen(SemaEntity& named, std::size_t arguments) const;

	// 5.2.3p2 and p3: the object `T()` and `T{...}` stand for where `T` names a
	// class, as the constant that holds its subobjects' values.  `written` is
	// what the clauses came to.  8.5.1p1 says which of the two initializations
	// that is: an aggregate takes them as 8.5.1p2's clauses, one per member in
	// declaration order with 8.5.1p7 value-initializing the rest, and every
	// other class takes them as 8.5p16's direct-initialization, whose object is
	// what 12.6.2's mem-initializers of the constructor 13.3.1.3 chose come to.
	SemaConstant object_of(TypeId type,
	                       const std::vector<SemaConstant>& written);

	// 8.5.1p2 and p7 over an array: the same reading where the subobjects the
	// clauses reach are 8.3.4p6's elements rather than 9.2p1's members, so
	// which one a clause initializes is its position and no lookup at all, and
	// the elements past the last clause are value-initialized.
	SemaConstant array_of(TypeId type,
	                      const std::vector<SemaConstant>& written);
	// 5.2.1p1: the element a subscript of such an array names.
	SemaConstant element_value(const SemaConstant& array,
	                           unsigned long long index);

	// True where the constant stands for such an object rather than for a value
	// of arithmetic type, which is what says its bits are a list identifier.
	// `is_object` is the reading that asks for a *class*, because 5.2.5p1's
	// member access and 12.3.2p1's conversion function are questions only a
	// class answers; `holds_list` is the same fact about the bits.
	bool is_object(const SemaConstant& value) const;
	bool holds_list(const SemaConstant& value) const;

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
	SemaConstant at_arithmetic_place(const SemaConstant& value, TypeId place);

	// The same reading where the place *counts* rather than holds a number: an
	// array bound, a subscript, a shift width.  Each of those is an integral
	// constant expression, so 4.9's conversion of a floating value is no part
	// of what reaches it and a value of floating type is refused there rather
	// than truncated to one.
	unsigned long long counted(const SemaConstant& value);

	// 12.3.2p1 with 14.3.2p5: `value`, an object of class type, brought to the
	// type `place` by a conversion function of its class.  13.3.3p1 leaves the
	// choice to a ranking of the conversion each answer then takes, which a
	// fold cannot make: one that reaches `place` itself is the best there is,
	// and a class offering two that reach it only through a further conversion
	// is one this reading refuses rather than picks from.  Throws `NotConstant`
	// there and where the class declares no constexpr conversion function that
	// reaches an arithmetic type at all.
	SemaConstant converted(const SemaConstant& value, TypeId place);

private:
	ConstexprReading(const ConstexprReading&);
	ConstexprReading& operator=(const ConstexprReading&);

	// The entry `value` interns as, which is what keys a fold and what a folded
	// answer is read back out of.
	TypeId entry_of(const SemaConstant& value) const;
	static SemaConstant constant_of(TypeId entry, const TypeTable& types);

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
	// 5.17p1: the object an assignment writes, which is one this fold bound.
	SemaEntity& assigned(const AstNode& node, const SemaContext& ctx);
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
	void bind_constant(const std::string& name, const SemaConstant& value,
	                   const SemaContext& inner, bool written);

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
	// each member, indexed by the name its mem-initializer-id spelled.  The
	// members are initialized in declaration order and the mem-initializers may
	// be written in any, so which one names each member is asked once per
	// member rather than by a scan of the list per member - which is what keeps
	// a constructor of n of them n readings and not n^2.  A mem-initializer-id
	// that names a base is one entry too, and 10p1's base subobject is one this
	// reading's object does not hold.
	static void mem_initializers(
		const AstNode* initializers,
		std::unordered_map<std::string, const AstNode*>& out);

	// 5.2.5p1: the object a member access is written on, which a constant
	// expression holds only where it is one of literal class type, and what the
	// name after the `.` denotes in that object's class.
	SemaConstant accessed_object(const AstNode& node, const SemaContext& ctx);
	SemaEntity* accessed_member(const SemaConstant& object,
	                            const std::string& name, const SemaContext& ctx);
	// 5.2.2p1 with 9.3.1p3: a call whose postfix-expression is that access,
	// which runs on the object where 9.2p1's member function is not static.
	SemaConstant member_called(const AstNode& callee,
	                           const std::vector<SemaConstant>& arguments,
	                           const SemaContext& ctx);

	// 9.2p13: the non-static data members of `type`, in the order they were
	// declared, which is the order 8.5.1p2's clauses reach them in.
	void data_members(TypeId type, std::vector<SemaEntity*>& out) const;

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
