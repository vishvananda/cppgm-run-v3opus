#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "type_model.h"

struct AstNode;
struct DeclaredParameter;
class Scope;
class SemaAnalyzer;
struct SemaContext;
struct SemaEntity;

// 14.5.3p4 in a list the program wrote: the entries one such list comes to.
//
// 8.5.1's initializer-clauses, 5.2.2's arguments, 5.2.3's conversion and
// 5.3.4's placement and initializer are all lists an expansion may stand in,
// and each entry of one is one argument - except where the program wrote
// `pattern...`, which stands for as many as the run its packs are bound to
// holds.  Each of those is that *same* pattern read again in a region of its
// own, so what a walk holds is the node and where it is read rather than a
// list anything rewrote.
//
// A list that wrote no expansion is its own children read where it stands,
// which is every list PA15-PA19 lower: asking costs one node-kind test per
// entry and nothing is allocated or copied.
class WrittenList
{
public:
	WrittenList()
		: written_(nullptr)
		, unsettled_(false)
	{}

	explicit WrittenList(const AstNode& list)
		: written_(&list)
		, unsettled_(false)
	{}

	// The same list with every expansion in it read out, which is what a walk
	// of an instantiated body builds.  Null is the list a call that wrote no
	// arguments at all has, which comes to nothing.
	WrittenList(const AstNode* list, SemaAnalyzer& analyzer,
	            const SemaContext& ctx);

	std::size_t size() const;
	const AstNode& node(std::size_t index) const;
	// The region entry `index` is read in, or null where it is read where the
	// list itself stands - which is every entry of a list holding no expansion.
	Scope* region(std::size_t index) const;
	// 14.6.2p1: whether an entry names a pack no argument list has settled,
	// which stands as one entry until one does.  What the list comes to is not
	// known while it does, so 8.3.4p3's bound is not settled here either.
	bool unsettled() const
	{ return unsettled_; }

private:
	// The list as written, and null once an expansion in it was read out.
	const AstNode* written_;
	std::vector<std::pair<const AstNode*, Scope*> > entries_;
	bool unsettled_;
};

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

	// 14.5.3p5: the places a reading names, recorded on the entry `reading`
	// stands for.  A reading is read again rather than rebuilt - a
	// decltype-specifier, a value argument, a default a head wrote - so its
	// type is built over nothing the packs it names appear in, and 14.5.3p4's
	// expansion of a pattern it stands in would find none.
	//
	// The names it wrote are what it names them by, and one of them may stand
	// for a reading of its own: `enable_if_t<bool(Bn::value)>` is read as `B`
	// against a region binding `B` to the reading that wrote `Bn`, so a place
	// this one names may be a place *that* one recorded.
	void note_places(TypeId reading, const std::string& text,
	                 const SemaContext& ctx) const;
	void note_places(TypeId reading, const AstNode& node,
	                 const SemaContext& ctx) const;
	void note_places(TypeId reading, const std::vector<std::string>& names,
	                 const SemaContext& ctx) const;

	// The same question over the tree PA10 handed on rather than over one
	// spelling, which is what an expansion written in an expression is: the
	// names are the ones its nodes wrote, and a function parameter pack is one
	// of the answers here because 8.3.5p10's places are what it stands for.
	Run run_of_node(const AstNode& node, const SemaContext& ctx) const;

	// The same question asked of more than one tree and merged into one run,
	// which is what a parameter-declaration needs: 8.3.5p1 writes its pattern as
	// a decl-specifier-seq beside a declarator, and 14.5.3p6 makes the packs the
	// two of them name together one run.
	void note_node(const AstNode& node, const SemaContext& ctx, Run& run) const;

	// The region the `element`th reading of a pattern stands in, binding each
	// pack the run named to what it stands for there - one element of a bound
	// run, or the place 14.5.3p4's expansion of a function parameter pack
	// declared for it.
	Scope& element_region(const Run& run, std::size_t element,
	                      const SemaContext& ctx);

	// 14.5.3p4 in 8.3.5p1's parameter list: the places a parameter-declaration
	// written `pattern... name` comes to, appended to `out`.
	//
	// This is the reading over a *declaration* rather than over a spelling or a
	// tree, and it is by element like both of them: the decl-specifier-seq and
	// the declarator are read again for each element of the run, in a region
	// binding the packs they name to that element.  Reading the type once and
	// expanding *that* would put the whole run where each element belongs, which
	// a pattern that is a specialization (`wrap<A>...`) cannot be read back out
	// of.  8.3.5p10 names the places after the pack and 14.5.3p4 declares the
	// pack itself where the run holds no element at all.
	//
	// `reading` is the region the next place of the clause is read against,
	// which a place that binds a name opens; `binds` is whether this clause's
	// names are declared at all.
	void read_places(const AstNode& declaration, const AstNode& specifiers,
	                 const AstNode& declarator, const Run& over,
	                 SemaContext& reading, const SemaContext& ctx, bool binds,
	                 std::vector<DeclaredParameter>& out);

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

// 14.1p11 and 14.4p1: which run of a head an argument list writes out flat.
//
// 14.1p11 leaves a class template's pack last, but 14.8.2 deduces a function
// template's head place by place, so `template<class... U, class... T>` is two
// places each binding a run of its own.  One flat list cannot say where the
// first run ends, and 14.4p1 keys the whole tier by that list - so a run stands
// as *one* entry of it, except the run bound to the last place, which is what
// every argument the places before it did not take comes to.
//
// This is the place whose run is written flat, or the number of places where
// the last place binds no run.  Every list PA19 and the class tier build has
// its pack there or nowhere, so they are read exactly as they were.
std::size_t trailing_pack_place(const TypeTable& types,
                                const std::vector<SemaEntity*>& parameters);

// The argument the `index`th of `places` places takes from such a list: the run
// itself where the place binds one, and `kNoType` where the list stopped short
// of the place - which 14.1p9's default is what fills.
TypeId place_argument(TypeTable& types, const std::vector<TypeId>& arguments,
                      std::size_t index, std::size_t places, bool pack);

// 8.3.5p10 and 14.5.3p4: the name the `index`th place of an expanded function
// parameter pack is declared under.  The pack's own name is the first of them,
// so a use of that name reaches the first place and the run is read off it.
std::string pack_element_name(const std::string& name, std::size_t index);
