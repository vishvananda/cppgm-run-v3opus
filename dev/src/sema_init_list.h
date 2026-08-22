#pragma once

#include <cstddef>

#include "sema_value.h"
#include "type_model.h"

class SemaAnalyzer;
class WrittenList;
struct SemaContext;

// 13.3.3.1.5 and 8.5.1p6: a braced-init-list read as one *argument* of a call.
//
// Every other initializer 8.5 describes is read for a type the declaration
// already wrote, and this one is not: 13.3 has to rank the list against each
// candidate's parameter before any of them is chosen, so what a list is worth
// to overload resolution is asked here and what it initializes is written
// elsewhere.  Three facts answer the whole clause, and each is settled once:
// how many clauses the list wrote, how many an object of a type can take, and -
// for the one list 13.3.3.1.5p6 ranks by something other than its length - what
// its single clause is worth.
//
// The capacity walk is held per type on the analyzer, so a class is walked once
// however many lists ask about it; the clause is read once per argument, where
// the region it was written in still stands, rather than once per candidate.
class ListArgument
{
public:
	explicit ListArgument(SemaAnalyzer& analyzer);

	// 13.3.3.1.5: the implicit conversion sequence of an argument written as a
	// braced-init-list, which is a fact of the parameter type and of the two
	// the argument carries - how many clauses it wrote and, where it wrote one,
	// what that clause is worth.
	OverloadMatch sequence(const AnalyzedValue& argument, TypeId parameter);

	// 13.3.3.1.5p6's clause, read where the argument is met and carried on the
	// value: the sequence a list of one reaches a place that is no class
	// through is the clause's own, which no reading of the list can answer
	// after 13.3 has chosen.
	void element_facts(AnalyzedValue& value, const WrittenList& written,
	                   const SemaContext& ctx);

	// 13.3.1.7 and 8.5.1: whether a braced-init-list of this many clauses
	// initializes an object of `type` at all, which is what 13.3.3.1.5 asks of
	// every candidate's parameter.  8.5.1 gives an aggregate's subobjects the
	// clauses; every other class is initialized by one of its constructors, so
	// what the list needs is one that accepts that many arguments.  The clauses
	// themselves are read once, where the initialization is written, rather
	// than once per candidate.
	bool initializes(TypeId type, std::size_t clauses);

	// 8.5.1p6 and 8.5.1p11: the most initializer-clauses an object of `type`
	// can take, which is the number of leaves its subobject tree has - a nested
	// aggregate contributes its own, because the braces around it may be left
	// out, and a union contributes one because 8.5.1p15 initializes it by its
	// first member alone.  A list with more clauses than that initializes no
	// object of the type, which is what keeps `f(One)` out of the candidates of
	// `f({1,2})`.
	unsigned long long capacity(TypeId type);

	// 8.5.1p11: what one subobject of that type takes out of the enclosing
	// list, which is its own capacity or the one clause its written braces are
	// - never nothing, however little the subobject holds.
	unsigned long long subobject_clauses(TypeId type);

private:
	ListArgument(const ListArgument&);
	ListArgument& operator=(const ListArgument&);

	SemaAnalyzer& analyzer_;
};
