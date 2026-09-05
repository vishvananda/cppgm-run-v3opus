#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sema_scope.h"
#include "type_model.h"

class SemaAnalyzer;

// 6.7p3: one point of a function body a jump leaves from or arrives at, as the
// regions open where it stands - outermost first - with the number of
// declarations each had made by then that a jump may not bypass.
//
// A point costs the block nesting it is written at and never the declarations
// it can see, because the regions are the ones the scope chain already holds
// and each carries its own count.  What makes two points comparable is that
// those chains share a prefix: the regions both stand in are the same objects,
// so the innermost region a jump does not leave is the last of the prefix, and
// everything after it on the arrival's chain is a region the jump enters.
struct JumpPoint
{
	std::vector<std::pair<const Scope*, unsigned> > open;
};

// 6.1p1, 6.6.4p1 and 6.7p3: the jumps of the function being read.
//
// They are one reader because they are one question asked of two points of one
// body: a label may be written after the goto that names it, and a case label
// stands for a jump from the condition of the switch it labels - so neither end
// can answer alone and the body is the whole of what can.  Each point is
// recorded where the walk reads it, a case label is answered where it stands
// because the switch it jumps from is already open, and the goto pairs are
// matched once the body has been read.
class JumpReadings
{
public:
	JumpReadings();

	// 6.4.2p1: the switch statements this walk stands inside, as the region
	// each opened - which is the point a jump to one of its case labels comes
	// from, and is entered before that region exists.
	void open_switch();
	void hold_switch_region(const Scope& region);
	void close_switch();
	bool inside_switch() const { return !switches_.empty(); }

	// 6.1p1: a label of this function, and 6.6.4p1 a goto that names one.
	void write_label(const std::string& name, const Scope& at);
	void write_goto(const std::string& name, const Scope& at);
	// 6.7p3: the jump a case or default label is reached by, whose origin is
	// the condition of the innermost switch the label stands in.
	void require_reached_case(const Scope& at) const;
	// 6.6.4p1 and 6.7p3 asked where the body ends: every goto names a label of
	// this function, and no goto bypasses a declaration.
	void require_labelled_gotos() const;

	void swap(JumpReadings& other);

private:
	// The regions open at `at`, outermost first, with what each had declared.
	static JumpPoint point_at(const Scope& at);
	// 6.7p3: the refusal of a jump from `from` to `to`.
	static void require_no_bypass(const JumpPoint& from, const JumpPoint& to);

	std::vector<const Scope*> switches_;
	std::unordered_map<std::string, JumpPoint> labels_;
	std::vector<std::pair<std::string, JumpPoint> > gotos_;
};

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
// 3.2p2 and 5p8: the statements of a body are potentially evaluated whatever
// the reading that reached the body was, so the unevaluated operand standing
// over it - a `decltype` whose expression completed a class, a `sizeof` whose
// operand asked for an instantiation - is put aside here and given back where
// this reading ends.
unsigned unevaluated_;
unsigned breakable_;
unsigned continuable_;
std::size_t live_destructions_;
std::vector<std::vector<SemaEntity*> > lifetimes_;
std::vector<std::size_t> breakable_frames_;
std::vector<std::size_t> continuable_frames_;
std::vector<SemaEntity*> parameter_objects_;
JumpReadings jumps_;
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

// 11.2p4: the region an access is written in, held while something is read
// against it and put back where that reading ends.  Every expression is read
// against one region, and a conversion the initialization applies *after* the
// expression it was written for - 4.10p3's conversion to a base among them - is
// written in that same region, so the region outlives the reading of the
// operand and is held here rather than there.
struct Written
{
	Written(SemaAnalyzer& owner, Scope* region);
	~Written();

	SemaAnalyzer& owner;
	Scope* held;
};

// A depth held while one reading stands, and put back where it ends - so a
// reading that stands inside another says so and an error thrown out of one
// leaves the walk where it found it.
class ReadingDepth
{
public:
	// `held` false leaves the depth alone, which is what lets one reading take
	// two of them apart - a class body an instantiation read is always one of
	// those, and only sometimes a pattern's.
	explicit ReadingDepth(unsigned& depth, bool held = true)
		: depth_(depth)
		, held_(held)
	{
		depth_ += held_ ? 1u : 0u;
	}

	~ReadingDepth()
	{
		depth_ -= held_ ? 1u : 0u;
	}

private:
	ReadingDepth(const ReadingDepth&);
	ReadingDepth& operator=(const ReadingDepth&);

	unsigned& depth_;
	bool held_;
};

// 3.2p2 and 14.6.4.1p1: a reading that is a potentially-evaluated context of
// its own, however it was reached.
//
// 5p8's unevaluated operand says nothing about what the *definitions* an
// instantiation makes there name: a class completed inside `decltype` folds
// the constant expressions its own members write, and the bodies it holds are
// run wherever the program calls them - the point of instantiation is a
// namespace-scope one and not the operand that asked for the type.  So the
// depth is put aside at each instantiation's door and given back where it ends.
class Evaluated
{
public:
	explicit Evaluated(unsigned& depth)
		: depth_(depth)
		, held_(depth)
	{
		depth_ = 0;
	}

	~Evaluated()
	{
		depth_ = held_;
	}

private:
	Evaluated(const Evaluated&);
	Evaluated& operator=(const Evaluated&);

	unsigned& depth_;
	const unsigned held_;
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
