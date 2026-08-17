#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sema_value.h"
#include "type_model.h"

class Scope;
class SemaAnalyzer;
struct SemaContext;
struct SemaEntity;

// 3.4.2: the declarations a use of a name reaches through the *types* of its
// arguments rather than through the region it was written in.
//
// This is a lookup of its own and not a step of any one expression: 13.3's
// candidate set for a call, 13.3.1.2p3's for an operator expression and the
// set a fold of a constant expression ranks are all the ordinary lookup's
// answer and this one's together.  What it walks is the type of each argument
// and the base chain and enclosing regions of every class it reaches, which is
// neither the expression layer's business nor the overload ranking's - so it
// is kept here, as `sema_deduce.h` keeps 14.8.2's match and `sema_pack.h`
// keeps 14.5.3's run.
//
// Every walk is by *reached region*: `AssociatedRegions` holds what it has
// already visited, so a class met twice costs one probe and a specialization
// whose arguments repeat is followed once - a set of n arguments over a nest
// d deep costs n + d visits and never their product.
class ArgumentLookup
{
public:
	explicit ArgumentLookup(SemaAnalyzer& analyzer);

	// 3.4.2p2: the declarations of `name` the argument types reach, appended to
	// `candidates`.  Returns how many of them are the single friend
	// declarations 11.3p6 made, which stand last.
	std::size_t candidates(const std::string& name,
	                       const std::vector<AnalyzedValue>& arguments,
	                       std::vector<SemaEntity*>& out);

	// 3.4.2p2 with 14.8.1p2: the same, for a callee the program wrote as a
	// template-id.  14.2 writes the argument list inside the name, so the
	// search is made with the name the template-id names and what it reaches
	// are templates the written list has still to be read against - which is
	// what the ordinary lookup's own declarations already had done to them.
	std::size_t call_candidates(const std::string& called,
	                            const std::vector<AnalyzedValue>& arguments,
	                            const SemaContext& ctx,
	                            std::vector<SemaEntity*>& out);

	// 3.4.2p3: whether what the ordinary lookup found leaves the
	// argument-dependent one to be done at all.  A name nothing was found of
	// leaves it to be done, which is what reaches a function declared beside
	// its own argument's class and nowhere else.
	bool allowed(const SemaEntity* named) const;

	// 3.4.2p2: the namespaces and classes one type is associated with.
	void associate_type(TypeId type, AssociatedRegions& out);
	void associate_bases(SemaEntity* owner, AssociatedRegions& out);
	void associate_region(Scope* region, AssociatedRegions& out);

private:
	ArgumentLookup(const ArgumentLookup&);
	ArgumentLookup& operator=(const ArgumentLookup&);

	SemaAnalyzer& analyzer_;
};
