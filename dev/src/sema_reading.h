#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "sema_scope.h"
#include "type_model.h"

class SemaAnalyzer;

// What one reading puts aside while it stands, and gives back however it ends.
//
// A body is read wherever its definition can be, a pattern is read once at its
// own point and again for each specialization, and a declaration is read while
// another is - so every one of these readings can stand inside another, and
// what the enclosing one knew has to survive an error thrown out of the inner.
// Each record below is that: the state one reading takes over, held by an
// object whose destructor puts it back.  They are declarations only; what each
// saves is the analyzer's, so the definitions live with the walk that owns it.

// 6.6.1, 6.6.3 and 12.2p3: what the walk knows about the function whose
// body it is reading.
//
// A body is read wherever its definition can be: where the definition was
// written, at the end of the unit for a member read once its class is
// complete, and - 14.6p8 - at the definition of the template whose pattern
// it is.  Each of those readings can stand inside another, because naming a
// specialization in an expression is what asks for one, so what the
// enclosing reading knew is put aside here and given back when this body is
// done however it ends.
class FunctionReading
{
public:
FunctionReading(SemaAnalyzer& analyzer, SemaEntity* self, TypeId returns);
~FunctionReading();

private:
FunctionReading(const FunctionReading&);
FunctionReading& operator=(const FunctionReading&);

SemaAnalyzer& analyzer_;
SemaEntity* self_;
TypeId returns_;
unsigned breakable_;
unsigned continuable_;
unsigned switches_;
std::size_t live_destructions_;
std::vector<std::vector<SemaEntity*> > lifetimes_;
std::vector<std::size_t> breakable_frames_;
std::vector<std::size_t> continuable_frames_;
std::vector<SemaEntity*> parameter_objects_;
std::unordered_set<std::string> labels_;
std::vector<std::string> gotos_;
};

// 14.6p8: the reading of a template's pattern rather than of a declaration
// this unit has.
//
// What the pattern can be asked is what its declarations *say*, which is
// the PA11 dialect: a type that depends on a template parameter has no
// layout, no conversion and no overload set until an argument arrives, so
// the reading describes the declarations the body makes and translates
// none of them.  It stands inside a lowering, so the dialect is put aside
// here and given back however the reading ends.
class DialectReading
{
public:
explicit DialectReading(SemaAnalyzer& analyzer);
~DialectReading();

private:
DialectReading(const DialectReading&);
DialectReading& operator=(const DialectReading&);

SemaAnalyzer& analyzer_;
SemaDialect dialect_;
};

// 11p6: the class a declaration written outside it names a member of, held
// while that declaration is read and put back where it ends - because a
// declaration may be read while another is.
struct Naming
{
	Naming(SemaAnalyzer& owner, Scope* region);
	~Naming();

	SemaAnalyzer& owner;
	Scope* held;
};

// A depth held while one reading stands, and put back where it ends - so a
// reading that stands inside another says so and an error thrown out of one
// leaves the walk where it found it.
class ReadingDepth
{
public:
	explicit ReadingDepth(unsigned& depth)
		: depth_(depth)
	{
		++depth_;
	}

	~ReadingDepth()
	{
		--depth_;
	}

private:
	ReadingDepth(const ReadingDepth&);
	ReadingDepth& operator=(const ReadingDepth&);

	unsigned& depth_;
};

// 14.5.1.3p1: the region standing between a class and the one around it while
// one out-of-class member definition of it is read, which is that definition's
// own head's rather than the class's.  The class keeps the region it was
// completed against, because every other reading of its members - its own body,
// the next definition written outside it - looks names up through that one.
//
// It is the other half of `StandingIn`, which puts a head *inside* the region a
// qualified declarator-id names: there the region is the program's own and the
// head is what moves, and here the region is the head and the class is what it
// stands over.  A body 14.7.1p1 put aside is read after the reading that made
// it has returned, so the link is put back for it from what `PendingDefinition`
// recorded.
// Either scope may be null, which is a definition read where it stands and no
// link to make.
class EnclosedBy
{
public:
	EnclosedBy(Scope* scope, Scope* region)
		: scope_(region != nullptr ? scope : nullptr)
		, parent_(scope_ != nullptr ? scope_->parent : nullptr)
		, head_(scope_ != nullptr ? scope_->template_head : nullptr)
	{
		if (scope_ != nullptr)
		{
			scope_->parent = region;
			scope_->template_head = region;
		}
	}

	~EnclosedBy()
	{
		if (scope_ != nullptr)
		{
			scope_->parent = parent_;
			scope_->template_head = head_;
		}
	}

private:
	EnclosedBy(const EnclosedBy&);
	EnclosedBy& operator=(const EnclosedBy&);

	Scope* const scope_;
	Scope* const parent_;
	Scope* const head_;
};
