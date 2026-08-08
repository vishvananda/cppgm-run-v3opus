#pragma once

#include <string>
#include <vector>

struct AstNode;
class Scope;
struct DumpScope;

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
		, supported(true)
	{}

	const AstNode* pattern;
	Scope* region;
	DumpScope* dump;
	// 14.1p2: the type parameters the head declared, in order, and 14.1p9's
	// default arguments, null where the head wrote none.  A head this
	// milestone gives no meaning to - a non-type parameter, a template
	// parameter, a pack - leaves `supported` false, so the declaration is
	// still read and only an instantiation of it is refused.
	std::vector<std::string> parameters;
	std::vector<const AstNode*> defaults;
	bool supported;
};
