#pragma once

#include <cstddef>
#include <vector>

#include "sema_declaration.h"

class SemaAnalyzer;

// 9.2p2 at the reading tier, which is what `parse_deferred.h` is at the parse's.
//
// A class is a complete type at the closing `}` of its class-specifier, and
// within the member-specification it is regarded as complete inside a function
// body, a default argument, an exception-specification and a
// brace-or-equal-initializer - "including such things in nested classes".  The
// parse answers the first half of that by putting the terminals of each such
// construct aside; this is the second half, which is that the *readings* of
// them wait too.  The three the readings tier holds are on one list, and one
// entry can hold another, because a body may declare a class of its own whose
// members write bodies the clause leaves until that class closes.

// One held reading.  `initializer` and `definition` say which of the three an
// entry is: a member function body is read where both are null, the one
// expression a brace-or-equal-initializer writes where `initializer` stands,
// and 14.6p8's whole reading of a member *template's* definition - the region
// its declarator opened and the statements under it - where `definition` is
// set.  A member template is a definition this unit writes nothing for, so what
// it owes is that reading and not a body walked against types no argument list
// has settled; `implicit` travels with it because 9.3.1p3's object parameter is
// the type's and not the declarator's.
struct HeldTemplateBody
{
	HeldTemplateBody()
		: node(nullptr)
		, initializer(nullptr)
		, type(0)
		, implicit(0)
		, definition(false)
	{
	}

	const AstNode* node;
	const AstNode* initializer;
	SemaContext inner;
	std::vector<DeclaredParameter> parameters;
	TypeId type;
	std::size_t implicit;
	bool definition;
};

// The entries one class-specifier's member-specification holds, and the `}`
// that reads them.
//
// The mark is what every reading standing inside another already uses; the
// depth is what says which `}` an entry belongs to.  A class nested in another
// takes no mark of its own, because the clause regards the enclosing class as
// complete within such things in nested classes too - so a nest holds one list
// and the outermost class-specifier is the one that reads it.  `read` is that
// `}`: it steps out of the nest first, so a reading it makes stands in no class
// body and holds nothing here that it will not itself read.
class ClassBodyReadings
{
public:
	explicit ClassBodyReadings(SemaAnalyzer& analyzer);
	~ClassBodyReadings();

	void read();

private:
	ClassBodyReadings(const ClassBodyReadings&);
	ClassBodyReadings& operator=(const ClassBodyReadings&);

	SemaAnalyzer& analyzer_;
	std::size_t mark_;
	bool outermost_;
	bool read_;
};
