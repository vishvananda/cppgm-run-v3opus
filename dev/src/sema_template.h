#pragma once

#include <string>
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
	// 14.1p2: the type parameters the head declared, in order, and 14.1p9's
	// default arguments, empty where no declaration wrote one.  A head this
	// milestone gives no meaning to - a non-type parameter, a template
	// parameter, a pack - leaves `supported` false, so the declaration is
	// still read and only an instantiation of it is refused.
	std::vector<std::string> parameters;
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
};

// 14.7.1p1 and 14.2: whether the definition a unit holds of `function` is one
// an instantiation made rather than one the program wrote out - which a
// specialization of a function template is, and so is every member of a class a
// template-id named, however deeply the classes it belongs to nest.
bool instantiated_declaration(const SemaEntity& function, TypeTable& types);
