#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "type_model.h"

class SemaAnalyzer;
struct SemaContext;
struct SemaEntity;

// 14.5.3: a parameter pack and what an expansion of one comes to.
//
// A pack place binds a *run* of arguments rather than one, and 14.5.3p4 writes
// a use of it as a pattern followed by `...` - so every list a program can
// write an expansion into holds one entry that stands for however many
// arguments the run holds.  Turning that entry into the run is one reading,
// asked from the three places a list is built: a template-argument-list, a
// base-clause and a call's argument list.
//
// The reading is by *element*: the pattern is read again for each argument in
// the run, in a region binding the names of the packs it mentions to that
// element.  Nothing rewrites the pattern's syntax and nothing is scanned twice,
// so a run of n elements costs n readings of one pattern.
class PackReading
{
public:
	explicit PackReading(SemaAnalyzer& analyzer);

	// 14.5.3p4: the arguments `pattern...` comes to where it is written,
	// appended to `out`.
	//
	// Where the packs the pattern names are bound to a run, that is one
	// argument per element; where they are still the parameters of the head
	// being read, 14.6.2p1 leaves the expansion itself standing as one
	// argument, which an argument list settles later.  `place` is the type a
	// non-type place converts each argument to, and `kNoType` at a type place.
	void expand(const std::string& pattern, const SemaContext& ctx,
	            TypeId place, std::vector<TypeId>& out);

	// 5.3.3p5: the number of elements in the run `name` is bound to, and -1
	// where it names a pack no argument list has settled - which is what makes
	// `sizeof...` a value only the instantiation knows.
	long long length(const std::string& name, const SemaContext& ctx) const;

	// 14.5.3p4: whether the run the packs in `pattern` are bound to is
	// settled, and how long it is.  `settled` is false where the pattern names
	// a pack the head being read declared; where it is true and no pack was
	// found at all, the pattern is not an expansion of anything.
	struct Run
	{
		Run()
			: settled(true)
			, found(false)
			, length(0)
		{}

		bool settled;
		bool found;
		std::size_t length;
		// The declarations the pattern named, which are what each element is
		// bound over.
		std::vector<SemaEntity*> packs;
	};
	Run run_of(const std::string& pattern, const SemaContext& ctx) const;

private:
	PackReading(const PackReading&);
	PackReading& operator=(const PackReading&);

	SemaAnalyzer& analyzer_;
};

// 14.5.3p4: the pattern a spelling written with a trailing `...` expands, and
// whether it wrote one at all.  The spelling is what PA10 handed on, so the
// `...` is the last three characters of it and belongs to no name.
bool written_pack_expansion(const std::string& spelling, std::string& pattern);

// 14.5.3p1: what a region binds a pack place's name to, given the argument list
// the specialization was made from and the place the run begins at.
//
// It is the run of the arguments from there on - except where that run is the
// one expansion 14.6.1p1's current instantiation stands for, which is the place
// itself and not a run of one.
TypeId bound_run(TypeTable& types, const std::vector<TypeId>& arguments,
                 std::size_t from);
