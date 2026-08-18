#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct AstNode;
class Scope;
struct DumpScope;
struct SemaEntity;
class SemaModel;
class TypeTable;

// 14.5.6.1p5: the stand-ins two template heads are compared through, and the
// signature each declaration was built into.
//
// Two heads declare the same template when their parameters stand in the same
// places and their types agree once each head's parameters stand for the
// other's.  The types themselves differ - each head declared parameters of its
// own - so a comparison substitutes both onto one set of stand-ins: one per
// position, made once here and shared by every signature built from then on.
// 14.1p4's place binds a value rather than a type, and two heads spell one such
// place only where the type it binds a value of agrees too - so that place
// stands for its position and that type together and is keyed by both.
//
// It lives beside the head itself because it is the head's own vocabulary: what
// a signature is built from is what `TemplateInfo` declared.
class TemplateSignatures
{
public:
	// The stand-in for a type place at `index`, and for one declared with
	// `...`, which stands for a run rather than for one argument.
	TypeId place(TypeTable& types, SemaModel& model, std::size_t index,
	             bool pack);
	// The stand-in for a value place at `index` binding a value of `of`, which
	// is the type the places before it have already been canonicalized to.
	TypeId value_place(TypeTable& types, SemaModel& model, std::size_t index,
	                   bool pack, TypeId of);
	// The signature one declaration was built into, or `kNoType` where none has
	// been.  `held` says whether this call is the one that has to build it.
	TypeId& built(std::uint32_t declaration, bool& held);

private:
	std::vector<TypeId> places_;
	std::vector<TypeId> packs_;
	std::unordered_map<std::uint64_t, TypeId> values_;
	std::unordered_map<std::uint32_t, TypeId> built_;
};

// 14p1: what a template-declaration parameterises.
//
// A template is not a declaration but the pattern of one, and 14.7.1p1 makes
// an instantiation the reading of that pattern with every parameter standing
// for the argument bound to it.  So the pattern is kept as the syntax it was
// written from, beside the region it was written in - which is where the names
// its body mentions are looked up from - and the parameters its head declared.
// Nothing is copied and no text is replayed: an instantiation opens a region
// of its own for the bindings and reads the same tree the program wrote.
struct TemplateInfo
{
	TemplateInfo()
		: pattern(nullptr)
		, region(nullptr)
		, reading_region(nullptr)
		, dump(nullptr)
		, parameter_region(nullptr)
		, reading_dump(nullptr)
		, current(nullptr)
		, supported(true)
	{}

	const AstNode* pattern;
	Scope* region;
	// 14.5.1.3p1: where 14.7.1p1 opens its bindings, which parts company with
	// `region` for a definition written outside its class.  Such a definition
	// stands under a head per class it is nested in and then its own, and
	// 14.1p2 lets it spell the enclosing classes' places with names of its own
	// - names bound in the region that outer head opened and nowhere in the
	// class the declaration was made in.  So the pattern is read where the nest
	// stands, while the declaration, its object-file name and the region a use
	// finds it in stay the class's.  Null for every definition written where
	// its declaration is made.
	Scope* reading_region;
	DumpScope* dump;
	// 14.6.1p1: the region binding each parameter to a type standing for
	// itself, and the class the definition read in it makes - the current
	// instantiation.  They are opened once, by the first reading that needs
	// them, and 14.5.1.3p1's out-of-class member definitions are read against
	// the same two however long after the class they were written.  The lines
	// of that reading stand in a dump nothing writes out.
	Scope* parameter_region;
	DumpScope* reading_dump;
	SemaEntity* current;
	// 14.1p9 and 14.1p10: the argument a place takes where the use wrote none.
	// The defaults available to a use are every declaration's merged, and
	// 14.1p2 lets each of those declarations spell the places its own way - so
	// what a default names the places before it by is a fact of the head that
	// wrote it, kept beside the type-id rather than looked for on whichever
	// declaration the merge left standing.
	struct Default
	{
		Default()
			: written(nullptr)
		{}

		const AstNode* written;
		std::vector<std::string> spelled;
	};
	// 14.1p2: the parameters the head declared, in order, and 14.1p9's default
	// arguments beside them, empty where no declaration wrote one.  A head this
	// milestone gives no meaning to - a template parameter, a pack - leaves
	// `supported` false, so the declaration is still read and only an
	// instantiation of it is refused.
	//
	// 14.5.5p1 leaves the same mark: a partial specialization is a *second body*
	// an argument list may be read from, so one this milestone could not read is
	// not a declaration it may quietly ignore - the list it would have been read
	// from would then be read from the primary's body instead, which is a
	// different program.  What cannot be read is therefore what cannot be
	// instantiated, at the one gate 14.3p1's argument list already passes.
	//
	// 14.1p4's non-type parameter is a place of its own: what it binds is a
	// value and not a type, and what type that value has is written by the
	// parameter's own decl-specifier-seq and declarator - which may name the
	// parameters before it, as `template<class T, T v>` does.  So the syntax
	// travels with the place and the type is read in a region that already
	// binds them.
	struct Parameter
	{
		Parameter()
			: written(nullptr)
			, value(false)
			, templated(false)
			, head(nullptr)
			, pack(false)
			, self(0)
			, type(0)
		{}

		std::string name;
		const AstNode* written;
		bool value;
		// 14.1p2: a place declared `template<…> class`, which binds a
		// *template* and neither a type nor a value.  Its own
		// template-parameter-clause is what 14.3.3p1 matches a written argument
		// against, so it is read as a head of its own - once per clause, and
		// null for every place that is not one.
		bool templated;
		TemplateInfo* head;
		// 14.5.3p1: a place declared with `...`, which binds the run of
		// arguments left over rather than one argument.  14.1p11 leaves it
		// last in a primary template's head, so the places before it are the
		// arguments before it one for one.
		bool pack;
		// 14.6.1p1: the type standing for this place, which the current
		// instantiation's argument list is written from and which the type of a
		// later value place is read over.
		TypeId self;
		// 14.1p4: the type a value place names a value of, over the places
		// before it.  A concrete argument list substitutes its own bindings
		// into it.  Zero at a type place and until the head is read.
		TypeId type;
	};
	std::vector<Parameter> parameters;
	std::vector<Default> defaults;
	bool supported;
	// 14.5.1.3p1: the members of the template a definition outside its class
	// wrote, in the order they were written.  They are not members of the
	// pattern's own syntax, so a specialization reads them after the body and
	// one written after a specialization was already made is read for it then.
	// 14.1p2 lets each write parameter names of its own, so the head is kept
	// beside the declaration it parameterises and is what the bindings a
	// reading of it opens are spelled by.
	struct Member
	{
		Member(const AstNode* wrote, const AstNode* declares, Scope* stood)
			: clause(wrote)
			, declaration(declares)
			, carried(stood)
		{}

		const AstNode* clause;
		const AstNode* declaration;
		// 14.5.1.3p1 and 14.1p2: the region the heads standing *outside* this
		// definition's own bound when it was recorded - which is where the
		// names it wrote for the enclosing classes' places stand.  A member of
		// a member template writes one head per class it is nested in, and this
		// entry is made once per enclosing specialization, so the region is
		// that specialization's own.  Null for a definition whose one head is
		// the outermost.
		Scope* carried;
	};
	std::vector<Member> members;
	// 14.7.1p1: the specializations made so far, which is what a member
	// definition read after them is read against.
	std::vector<SemaEntity*> specializations;
	// 14.7.3p1: what an explicit specialization wrote for one argument list -
	// the class body read in place of the pattern, and the function body run in
	// place of the pattern's.  Both are keyed by the interned argument list the
	// specialization is already found by, so an instantiation asks one hash
	// lookup on a number it already has and never scans them.
	std::unordered_map<std::uint32_t, const AstNode*> explicit_classes;
	std::unordered_map<std::uint32_t, const AstNode*> explicit_functions;
	// 14.5.1p1: the same for a variable template, whose specialization is the
	// constant one init-declarator evaluates to rather than a class or a body.
	std::unordered_map<std::uint32_t, const AstNode*> explicit_variables;
	// 14.7.3p1: the static data members of this template that one argument list
	// has a definition of its own for.
	//
	// 9.4.2p2's definition written outside the class is what lays a static data
	// member's storage out and what its initializer initializes it with, and while
	// the template's own is the only one, that initializer is a fact of the
	// *declaration*: 5.19p2 reads it wherever a specialization's name is written.
	// A `template<>` for one list makes it a fact of the definitions instead -
	// which definition an argument list is read from is then what says what the
	// object holds - so the pattern's value is no longer one every specialization
	// was initialized with before the use.  It is keyed by the member's name
	// because that is what a declarator-id in either definition wrote, and asked
	// once per instantiated definition of a member of a template that has any.
	std::unordered_set<std::string> explicit_members;
	// 14.5.5p1: a declaration of this template for a *pattern* of arguments
	// rather than for a list of them.
	//
	// It is a template of its own - its head declares places nothing else
	// declares, and the pattern is written over those - so what it adds to this
	// one is a second body an argument list may be read from.  14.5.5.1p1 is
	// which: the list is matched against each pattern, and the arguments the
	// match deduced for the winner's own head are what its body is read
	// against.  Empty for every template no head partially specialized, which
	// is what makes the choice one test for a program that writes no trait.
	struct Partial
	{
		Partial()
			: head(nullptr)
			, body(nullptr)
			, signature(0)
			, current(nullptr)
		{}

		TemplateInfo* head;
		std::vector<TypeId> pattern;
		const AstNode* body;
		// 14.5.6.1p5: the pattern with each place standing for its position,
		// which is what tells a redeclaration of one pattern from a second
		// pattern - two heads spell one pattern over places of their own.
		std::uint32_t signature;
		// 14.6.1p1: the current instantiation of *this* body - the class its
		// own definition declares, which is the specialization its pattern
		// names.  A pattern is a template of its own, so it has one for the
		// same reason the primary does: its body writes its own class's name
		// and 14.5.1.3p1's out-of-class member definitions of it declare into
		// that class.  Null until the first reading that needs one.
		SemaEntity* current;
		// 14.5.1.3p1: the members of *this* body a definition outside its class
		// wrote, in the order they were written.  They are members of the class
		// this pattern declares and of no other, so a specialization read from
		// the primary or from another pattern holds none of them.
		std::vector<Member> members;
	};
	std::vector<Partial> partials;
	// 14.6.1p1: which pattern's current instantiation one class is, keyed by
	// the interned argument list that class was made from - which is its
	// pattern, and which the class already carries.  It is what tells the
	// reading that completes a specialization to read this pattern's body in
	// this pattern's own region rather than the primary's, and it costs one
	// hash lookup on a number every specialization holds.
	std::unordered_map<std::uint32_t, std::size_t> patterns;
	// 14.5.5.1p1: which pattern one argument list chose, and the arguments the
	// match deduced for that pattern's head - as the interned list every other
	// fact about an argument list is keyed by.  The choice is a fact of the
	// template rather than of any one naming, so it is made once and dropped
	// whole wherever a later declaration adds a pattern it never saw.
	struct Chosen
	{
		std::size_t at;
		std::uint32_t arguments;
	};
	std::unordered_map<std::uint32_t, Chosen> chosen;
	// 14.5.1p1 with 3.2p1: the argument lists whose reading is under way.  A
	// class is held before its body is read, so a naming inside that body finds
	// the declaration already made; a variable template's specialization *is*
	// the constant its initializer evaluates to, so there is nothing to hold
	// until the reading is over - and a naming of the same list reached from
	// that reading is the circle 5.19p2 makes ill-formed rather than a second
	// reading of it.  One entry per reading under way, which is a depth.
	std::vector<std::uint32_t> reading;
};

// 14.1p11 and 14.5.3p1: the place `info` declared a pack at, or the number of
// places where it declared none.  It is what tells a written argument the place
// it fills: the ones before it one for one, and every one after it the pack's.
std::size_t pack_place(const TemplateInfo& info);

// 14.7.1p1 and 14.2: whether the definition a unit holds of `function` is one
// an instantiation made rather than one the program wrote out - which a
// specialization of a function template is, and so is every member of a class a
// template-id named, however deeply the classes it belongs to nest.
bool instantiated_declaration(const SemaEntity& function, TypeTable& types);
// 6.6.3p2: whether what this function hands back is an object of class type,
// which stands in storage the caller named.
bool returns_class_object(const SemaEntity& function, TypeTable& types);

// 3.2p3 with 14.7.1p1: laying out an object of `type` is what asks this unit
// for the storage the static data members of the classes in it stand in.
//
// 14.7.1p1 instantiates a specialization's member *declarations* and leaves
// their definitions to whatever reaches them, and an object is what reaches
// every one of them: the bases and members it holds are objects of their own,
// so the walk is over the tree the object is and not over the one class the
// declaration named.  One visit per class - `SemaEntity::storage_demanded` -
// so a unit laying out n objects of one class pays the walk once.
void demand_object_storage(TypeId type, TypeTable& types, SemaModel& model);
