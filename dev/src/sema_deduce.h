#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "type_model.h"

class SemaAnalyzer;
struct AnalyzedValue;
struct SemaEntity;

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

	// 14.8.2.5p4 at a template place: `L<A…>` against a class an argument list
	// already made, which deduces `L` from the template that class was made of
	// and the rest from the arguments that made it.  `place` is the place `L`
	// stands for.
	bool match_template_id(TypeId pattern, TypeId argument, TypeId place,
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

	// 14.8.2.1p1's one pair: the parameter type `parameter` as it stands
	// against the type of what a call put there.
	bool match_argument(TypeId parameter, const AnalyzedValue& argument,
	                    std::unordered_map<TypeId, TypeId>& bindings);

	// 14.8.2.1p1 over 14.5.3p4's pattern: what the one pack `expansion` names
	// is deduced to when the pattern is matched against each of `given` in
	// turn, bound into `bindings` as the run of what its place took.
	bool match_run(TypeId expansion, const std::vector<TypeId>& given,
	               std::size_t from,
	               std::unordered_map<TypeId, TypeId>& bindings);

	// 14.8.2.1p3: the base of `argument` that is a specialization of the same
	// template as `pattern`, with the argument's own qualifiers, and `kNoType`
	// where no base of it is one.
	TypeId derived_from(TypeId pattern, TypeId argument) const;
	TypeId named_below(TypeId pattern, const SemaEntity& at) const;

	// 14.8.1p2: the type a template-id that wrote a leading part of `primary`'s
	// argument list leaves to be deduced, with those arguments already bound
	// into `bindings`.
	TypeId written_part(SemaEntity& primary, SemaEntity& made_of,
	                    std::unordered_map<TypeId, TypeId>& bindings);

	SemaAnalyzer& analyzer_;
};
