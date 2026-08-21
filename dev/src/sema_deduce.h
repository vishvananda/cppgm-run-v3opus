#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "type_model.h"

class SemaAnalyzer;
struct AnalyzedValue;
struct AstNode;
class Scope;
struct SemaContext;
struct SemaEntity;

// 14.8.2p8's other half: a failure that happened while a *template* was being
// instantiated rather than in the immediate context of a substitution.
//
// Substituting an argument list reaches for classes the arguments name, and
// 14.7.1p1's reading of one of those is a translation of its own: what its body
// refuses - a `static_assert` over the arguments, a member whose type is ill
// formed - is refused wherever the instantiation was asked for.  So the error
// carries which side of the line it came from, and a substitution attempt lets
// this one through.
class Instantiated : public std::runtime_error
{
public:
	explicit Instantiated(const std::string& why)
		: std::runtime_error(why)
	{}
};

// 14.8.2p8: one attempt at building the declaration an argument list makes of a
// template, and what a failure of it means.
//
// Substituting an argument list into a template's declaration builds types and
// reads expressions that the pattern could not: `decltype(t.run())` names a
// member of whatever class the arguments put there, `enable_if<B,T>::type` is a
// member only some arguments' class has, and a default template argument may be
// either.  14.8.2p8 says that when such a type or expression is ill formed the
// program is *not* refused - the declaration is simply not one this argument
// list makes, and 13.3 goes on without it.  Only where no candidate is left
// does the use have nothing to name.
//
// So the attempt is a scope and not a query: the substitution is run, and a
// failure inside it is caught here and turned into "this argument list makes no
// declaration".  What it costs is one try-block per candidate a deduction
// reaches, which is nothing until a candidate actually fails.
//
// 14.6p8's stand-in is the one failure that is not this: a reading of a pattern
// that ran out on a value standing in for one an argument list has yet to
// settle said nothing about the substitution, so it is handed back to the
// reading that made the stand-in rather than swallowed here.
class Substitution
{
public:
	explicit Substitution(SemaAnalyzer& analyzer);

	// 14.8.2p8: whether `why` is a failure of the substitution, which discards
	// the candidate, rather than one the program has to be refused for.
	bool discards(const std::runtime_error& why);

	// 14.8.2p8 over 14.8.1p2's written list: the specialization `arguments`
	// makes of `primary`, or null where substituting them is what failed - with
	// the failure's own words left in `refused` for a use that ends with no
	// candidate at all.
	SemaEntity* specialize(SemaEntity& primary,
	                       const std::vector<TypeId>& arguments,
	                       std::string& refused);

	// 14.7.1p1: whether `function` is a specialization no argument list has
	// settled, which is a declaration nothing instantiates and no fold runs -
	// the template it was made of has no pattern recorded, or its own list
	// still names a place.
	bool unsettled(const SemaEntity& function) const;

	// 14.6.2p1 with 14.2p4: what this substitution makes of a name written
	// after a prefix no argument list had settled - the prefix built first, then
	// the member's own list where it wrote one, and then the class the two name
	// asked for the member.
	TypeId member(TypeId naming, TypeId bare, unsigned cv,
	              const std::unordered_map<TypeId, TypeId>& bindings,
	              std::unordered_map<TypeId, TypeId>& memo);

	// 14.4p1 with 14.6.2p2: the argument a qualified-id naming a member of a
	// class no argument list has settled stands for.
	//
	// 14.5.6.1p5 compares two declarations by the types their declarators
	// built, and a value written into one of those types is part of that
	// comparison - so what it is has to be a fact of what the name *reaches*
	// and not of the characters that reached it.  A spelling says neither:
	// `value_type` and `T` name one type through two names, and an alias
	// template's own body writes a pattern over places the naming discarded.
	// So the stand-in is interned by the prefix, the member and 14.3.2p5's
	// place, all three of which every spelling of one name agrees on.
	//
	// `written` is the region the reading stood in, which is no part of that
	// identity and is kept beside the entry for 11.2's question alone - a
	// substitution that settles the prefix asks the access there, where the
	// spelling form asked it of the region it was re-read against.
	TypeId naming_value(TypeId prefix, const std::string& member, TypeId place,
	                    Scope* written = nullptr);

	// 7.1.6.2p4, 14.1p9 and 14.6.2p2: what this substitution makes of a
	// *reading* the definition left standing - a decltype-specifier, a value
	// argument written as a spelling, a head's own default written as a tree.
	// Each is answered by making the reading again in the regions these
	// arguments make of the ones it was written in.  `kNoType` where `bare`
	// names no such reading, which is every other kind of place.
	TypeId reading(TypeId naming, TypeId bare, unsigned cv,
	               const std::unordered_map<TypeId, TypeId>& bindings,
	               std::unordered_map<TypeId, TypeId>& memo);

private:
	Substitution(const Substitution&);
	Substitution& operator=(const Substitution&);

	// 14.6.2p2 at the substitution: what these arguments make of such a naming.
	// A prefix they leave dependent keeps the naming standing - over the class
	// *this* substitution built, exactly as a dependent member type does - and
	// a prefix they settle is asked for the member, whose constant is then
	// 14.3.2p5's converted argument.
	TypeId settled_value(TypeId naming, TypeId bare, TypeId owner,
	                     const std::unordered_map<TypeId, TypeId>& bindings,
	                     std::unordered_map<TypeId, TypeId>& memo);

	SemaAnalyzer& analyzer_;
	unsigned stood_;
};

// 14.8.2: the argument list a use of a function template deduces for it.
//
// A function template is named without its arguments, so what makes a
// specialization of it is a *match*: each parameter type P is paired with the
// type A of what the use put there, and the pairs together say what every
// place of the head is bound to.  Two uses write those pairs - 14.8.2.1's call,
// which has one A per argument, and 14.8.2.2's target type, which is one A for
// the whole function type - and both end at 14.8.2p5, which asks whether the
// pairs, 14.5.3p4's run and 14.1p9's defaults between them left every place
// with an argument.
//
// The match itself is structural and knows nothing about calls: it walks a P
// and an A of the same shape and records what each parameter the P is written
// over stood for.  So it is a reading of its own, kept here rather than beside
// 14.7.1's instantiation, and every use of it costs one walk of one type.
class Deduction
{
public:
	explicit Deduction(SemaAnalyzer& analyzer);

	// 14.8.2.1: the specialization of `primary` that the arguments of a call
	// deduce, or null when they deduce none.
	SemaEntity* from_call(SemaEntity& primary,
	                      const std::vector<AnalyzedValue>& arguments);

	// 14.8.2.2: the specialization of `primary` that a target type of the
	// function type `wanted` deduces, or null when it deduces none.  13.4p1's
	// target is one type rather than a list, so the whole function type - the
	// result and every parameter - is the one pair the deduction is over.
	SemaEntity* from_target(SemaEntity& primary, TypeId wanted);

	// 14.8.2.3: the specialization of the conversion function template
	// `primary` that a conversion to `wanted` deduces, or null when it deduces
	// none.  12.3.2p1 writes the conversion-type-id where every other function
	// writes its result type, so the one P/A pair is that result against the
	// type 13.3.1.5p1 asked the class to reach.
	SemaEntity* from_conversion(SemaEntity& primary, TypeId wanted);

	// 7.1.6.4p6: the type a declarator written `auto` stands for, deduced from
	// the initializer standing beside it.  The clause says the deduction is
	// 14.8.2.1's own - the declarator with its placeholder replaced by an
	// invented parameter is P, and the initializer is the one argument of a
	// call to it - so the pair below is the pair a call already writes, and the
	// type is what substituting what it bound leaves.  `written` is that P and
	// `initializer` the 8.5 initializer of the declaration; `kNoType` where the
	// initializer settles no type, which inside a template pattern leaves the
	// declaration standing as it was written.
	// `place` takes what the invented parameter itself was bound to, which is
	// 7.1.6.4p7's question about a second declarator of the same declaration.
	TypeId from_initializer(TypeId written, const AstNode* initializer,
	                        const SemaContext& ctx, TypeId* place = nullptr);

	// 7.1.6.4p4, p6 and p7 at one declarator: the deduction above with the two
	// requirements the *declaration* has to meet around it - that a placeholder
	// stands in no non-static data member, which `member_variable` says, and
	// that every declarator of one declaration deduce the same type, which
	// `deduced` carries between them.  `written` is returned unchanged where
	// the deduction stood in, which leaves the declaration a pattern.
	TypeId placeholder_declaration(TypeId written, const AstNode* initializer,
	                               const SemaContext& ctx, bool member_variable,
	                               const std::string& name, TypeId* deduced);

	// 14.8.2.1p1's one pair: the parameter type `parameter` as it stands
	// against the type of what a call - or 7.1.6.4p6's initializer - put there.
	bool match_argument(TypeId parameter, const AnalyzedValue& argument,
	                    std::unordered_map<TypeId, TypeId>& bindings);

	// 14.8.2.5: the bindings the argument type `argument` gives the template
	// parameters `pattern` is written over, added to `bindings`.  False when
	// the two do not agree, which is a deduction that failed.
	//
	// 14.8.2.1p4: `relaxed` is the top of a reference parameter, where the
	// deduced argument may be more cv-qualified than what was passed - which is
	// what lets `const T &` take an lvalue of a type nothing wrote `const` on.
	//
	// 14.8.2.1p3: `derived` is a pair the use wrote out, where a class A may be
	// a class *derived* from what the class P names.  It is false everywhere
	// below the top of such a pair, because a base of a member's type is no
	// match for what the member was written as.
	bool match(TypeId pattern, TypeId argument,
	           std::unordered_map<TypeId, TypeId>& bindings,
	           bool relaxed = false, bool derived = false);

	// 14.8.2.5p4 with 14.5.3p1: one list of entries against another - a
	// template-argument-list, or 8.3.5p1's parameter list - where a trailing
	// `P...` in the pattern is one entry standing for every argument the
	// entries before it did not take.
	//
	// 14.5.5.1p1's choice between the partial specializations of one template is
	// this same reading: a pattern is an argument list written over the places
	// its own head declared, so what a use's list matches is asked here.
	bool match_arguments(const std::vector<TypeId>& wanted,
	                     const std::vector<TypeId>& given,
	                     std::unordered_map<TypeId, TypeId>& bindings);

private:
	Deduction(const Deduction&);
	Deduction& operator=(const Deduction&);

	// The three entry points above with 14.8.2p8's attempt taken off: each
	// deduces its pairs and substitutes what they settled, and each may refuse
	// where the declaration that builds is ill formed.  The public door is what
	// turns such a refusal into "this template makes no candidate".
	SemaEntity* deduced_call(SemaEntity& primary,
	                         const std::vector<AnalyzedValue>& arguments);
	SemaEntity* deduced_target(SemaEntity& primary, TypeId wanted);
	SemaEntity* deduced_conversion(SemaEntity& primary, TypeId wanted);

	// 14.8.2.5p4 at a template place: `L<A…>` against a class an argument list
	// already made, which deduces `L` from the template that class was made of
	// and the rest from the arguments that made it.  `place` is the place `L`
	// stands for, and `relaxed` and `derived` are 14.8.2.1p3 and p4's two
	// allowances, carried here as they are carried into every other pair.
	bool match_template_id(TypeId pattern, TypeId argument, TypeId place,
	                       std::unordered_map<TypeId, TypeId>& bindings,
	                       bool relaxed, bool derived);
	// 14.8.2.1p3 at a template place, read as it is at a named template: the
	// naming the whole pair deduces from rather than the first one written,
	// committed to `bindings` only where both halves of it agreed.  What a
	// naming is here is 14.3.3p1's question and not a template-name, because
	// `L<A…>` names no one template to look for.
	bool names_a_template(TypeId bare) const;
	bool naming_below(TypeId pattern, const SemaEntity& at, TypeId place,
	                  std::unordered_map<TypeId, TypeId>& bindings);
	bool match_naming(TypeId pattern, TypeId bare, TypeId place,
	                  std::unordered_map<TypeId, TypeId>& bindings);

	// 14.8.2p5 and 14.1p9: the argument each parameter of `primary` was deduced
	// or, where the deduction reached none, the one its head wrote a default
	// for.  False where a place is left with neither.
	bool arguments_of(const SemaEntity& primary,
	                  const std::unordered_map<TypeId, TypeId>& bindings,
	                  std::vector<TypeId>& out);

	// 14.8.2.1p6: the deduction a parameter gets from an argument that is an
	// unresolved overload set, which stands where exactly one declaration in
	// the set deduces it.
	bool overload_set(const AnalyzedValue& argument, TypeId expected,
	                  std::unordered_map<TypeId, TypeId>& bindings,
	                  bool reference);

	// 14.8.2.5p13 over 8.3.4p1's bound: the pair the brackets of two array
	// types write.  A bound the pattern named a place with is a deduced context
	// of its own - `T[N]` against `const int[5]` binds `N` to 5 as a value of
	// the type the place declared - and a bound both sides settled agrees
	// exactly, which is the one reading this was before there were places.
	bool match_bound(TypeId pattern, TypeId argument,
	                 std::unordered_map<TypeId, TypeId>& bindings);

	// 14.8.2.1p4's second allowance: the argument type this pair is read
	// against, where 4.4's qualification conversion is what reaches the type P
	// is written over.  The deduction looks for a deduced A *identical* to A,
	// and the clause lets the two differ by qualifiers a pointer's pointee
	// gains - so `probe(const T *)` takes an `int *`, deducing `T` as `int`
	// rather than refusing the pair.  The qualifiers are added to the argument
	// before the pair is read, because they are the ones the conversion at the
	// call would add and P is what says where they stand.
	TypeId qualification_converted(TypeId pattern, TypeId argument);

	// 14.8.2.1p1 over 14.5.3p4's pattern: what the one pack `expansion` names
	// is deduced to when the pattern is matched against each of `given` in
	// turn, bound into `bindings` as the run of what its place took.
	bool match_run(TypeId expansion, const std::vector<TypeId>& given,
	               std::size_t from,
	               std::unordered_map<TypeId, TypeId>& bindings);

	// 14.8.2.5p4 over one naming of `pattern`'s template, committed to
	// `bindings` only where every argument pair of it agreed.
	bool match_specialization(TypeId pattern, TypeId argument,
	                          std::unordered_map<TypeId, TypeId>& bindings);

	// 14.8.2.1p3: whether some base of `argument` is a naming of `pattern`'s
	// template that the pattern deduces from, binding what it deduced.
	bool derived_from(TypeId pattern, TypeId argument,
	                  std::unordered_map<TypeId, TypeId>& bindings);
	bool named_below(TypeId pattern, const SemaEntity& at,
	                 std::unordered_map<TypeId, TypeId>& bindings);

	// 14.8.1p2: the type a template-id that wrote a leading part of `primary`'s
	// argument list leaves to be deduced, with those arguments already bound
	// into `bindings`.
	TypeId written_part(SemaEntity& primary, SemaEntity& made_of,
	                    std::unordered_map<TypeId, TypeId>& bindings);

	SemaAnalyzer& analyzer_;
};
