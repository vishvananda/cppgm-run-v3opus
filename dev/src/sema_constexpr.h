#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sema_declaration.h"
#include "type_model.h"

class SemaAnalyzer;
struct AstNode;
struct SemaEntity;

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
	// heads that takes this many arguments and that this unit has a constexpr
	// definition of, or null where the set holds no such declaration or holds
	// more than one.  A fold has constants and not typed expressions, so the
	// arity is the whole of what it can rank by - and a name whose set leaves
	// the choice open is one it refuses rather than guesses at.
	SemaEntity* chosen(SemaEntity& named, std::size_t arguments) const;

	// 5.2.3p2 and p3: the object `T()` and `T{...}` stand for where `T` names a
	// class, as the constant that holds its subobjects' values.  `written` is
	// what the clauses came to, and 8.5.1p7 value-initializes every member the
	// list did not reach.
	SemaConstant object_of(TypeId type,
	                       const std::vector<SemaConstant>& written);

	// True where the constant stands for such an object rather than for a value
	// of arithmetic type, which is what says its bits are a list identifier.
	bool is_object(const SemaConstant& value) const;

	// 12.3.2p1 with 14.3.2p5: `value`, an object of class type, brought to the
	// type `place` by a conversion function of its class.  Throws `NotConstant`
	// where the class declares no constexpr conversion function that reaches an
	// arithmetic type at all.
	SemaConstant converted(const SemaConstant& value, TypeId place);

private:
	ConstexprReading(const ConstexprReading&);
	ConstexprReading& operator=(const ConstexprReading&);

	// The entry `value` interns as, which is what keys a fold and what a folded
	// answer is read back out of.
	TypeId entry_of(const SemaConstant& value) const;
	static SemaConstant constant_of(TypeId entry, const TypeTable& types);

	// 7.1.5p3's function-body: the one `return` statement's expression, with
	// the declarations written around it read into `region` first.  Null where
	// the body holds something 7.1.5p3 does not allow.
	const AstNode* return_expression(const AstNode& body,
	                                 const SemaContext& inner);

	// The region a fold of `callee` reads its body in: a region of its own over
	// the one the declarator opened, binding each parameter to what its
	// argument came to and, for a call on an object, each non-static data
	// member of that object's class to what the object holds for it.
	void bind_arguments(SemaEntity& callee, const SemaConstant* object,
	                    const std::vector<SemaConstant>& arguments,
	                    const SemaContext& inner);
	void bind_constant(const std::string& name, const SemaConstant& value,
	                   const SemaContext& inner);

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
