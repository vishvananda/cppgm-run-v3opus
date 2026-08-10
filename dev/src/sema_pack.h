#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "type_model.h"

struct AstNode;
class Scope;
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

	// The same question over the tree PA10 handed on rather than over one
	// spelling, which is what an expansion written in an expression is: the
	// names are the ones its nodes wrote, and a function parameter pack is one
	// of the answers here because 8.3.5p10's places are what it stands for.
	Run run_of_node(const AstNode& node, const SemaContext& ctx) const;

	// The region the `element`th reading of a pattern stands in, binding each
	// pack the run named to what it stands for there - one element of a bound
	// run, or the place 14.5.3p4's expansion of a function parameter pack
	// declared for it.
	Scope& element_region(const Run& run, std::size_t element,
	                      const SemaContext& ctx);

	// 14.5.3p4 over a type rather than over a spelling: `pattern...` where the
	// packs are already in the type, which is what a parameter-declaration
	// written `Args... args` and a function type built by substitution hold.
	//
	// False where the type names no pack at all, which is 8.3.5p3's `f(int...)`
	// and not an expansion.  Otherwise one entry is appended per element, or
	// the expansion itself where the run is not settled.
	bool expand_type(TypeId pattern, std::vector<TypeId>& out);

	// 14.5.3p4 inside a list a substitution rebuilds: one written entry becomes
	// however many the run its packs are bound to holds, appended to `out`.
	// Every other entry is the one type the substitution makes of it.
	void substitute_entry(TypeId written,
	                      const std::unordered_map<TypeId, TypeId>& bindings,
	                      std::unordered_map<TypeId, TypeId>& memo,
	                      std::vector<TypeId>& out);

	// The packs `pattern` is written over: the runs an argument list has
	// bound, and the places of the head being read that it still names.
	void packs_in(TypeId pattern, std::vector<TypeId>& runs,
	              std::vector<TypeId>& places) const;

private:
	// One name a pattern wrote, merged into what the run so far says.
	void note_name(const std::string& name, const SemaContext& ctx,
	               Run& run) const;

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

// 14.1p11 and 14.5.3p1: the place a pack was declared at among the parameters a
// function template's head declared, or their number where it declared none.
// That head is read by the ordinary declaration path, so its places are
// declarations and the fact is on the type each of them declared.
std::size_t function_pack_place(const TypeTable& types,
                                const std::vector<SemaEntity*>& parameters);

// 8.3.5p10 and 14.5.3p4: the name the `index`th place of an expanded function
// parameter pack is declared under.  The pack's own name is the first of them,
// so a use of that name reaches the first place and the run is read off it.
std::string pack_element_name(const std::string& name, std::size_t index);
