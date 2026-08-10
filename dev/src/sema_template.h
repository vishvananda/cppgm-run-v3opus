#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct AstNode;
class Scope;
struct DumpScope;
struct SemaEntity;
class TypeTable;

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
		, dump(nullptr)
		, parameter_region(nullptr)
		, reading_dump(nullptr)
		, current(nullptr)
		, supported(true)
	{}

	const AstNode* pattern;
	Scope* region;
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
			, pack(false)
			, self(0)
			, type(0)
		{}

		std::string name;
		const AstNode* written;
		bool value;
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
		Member(const AstNode* wrote, const AstNode* declares)
			: clause(wrote)
			, declaration(declares)
		{}

		const AstNode* clause;
		const AstNode* declaration;
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
