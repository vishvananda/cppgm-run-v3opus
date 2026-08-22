#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ast_names.h"

class AstNode;

// The parser state a failed alternative has to undo: the position, and the one
// piece of bracket state 14.2.3 reads back, namely whether the innermost open
// bracket pair is an angle bracket pair, which decides whether the next `>`
// closes it or is an operator.
struct ParseMark
{
	std::size_t pos;
	bool angle;
};

// Whether a member specification is being read, which is what says a class
// body makes the readings put aside inside it or leaves them for the class
// around it.  A put-aside reading is made outside every member specification,
// so a class a body defines is the outermost one for its own readings.
class MemberSpecification
{
public:
	MemberSpecification(bool& reading, bool inside)
		: reading_(reading)
		, saved_(reading)
	{
		reading = inside;
	}

	~MemberSpecification() { reading_ = saved_; }

	// Whether a member specification was already being read, which is what
	// makes this class a nested one.
	bool nested() const { return saved_; }

private:
	MemberSpecification(const MemberSpecification&);
	MemberSpecification& operator=(const MemberSpecification&);
	bool& reading_;
	bool saved_;
};

// 9.2p2: the readings a member specification puts aside, and the regions they
// are put back in.
//
// The clause names four complete-class contexts, and the four are not one
// shape: a function-body ends at the `}` that balances its `{`, and the other
// three end at a delimiter the run around them also writes.  What they share is
// that the reading is *made* at the same place, in the same regions, so one
// stack of entries holds all four and the kind says which rule the reading runs.
//
// A class is regarded as complete within the function bodies of its own member
// specification "including such things in nested classes", so the reading is
// made at the `}` that completes the *outermost* class of a nest and not where
// the specification met the `{`.  A class nested in one therefore reads nothing
// of its own: it hands its entries up and records the region its members stood
// in beside them, because that region is gone by the time the class around it
// reaches its own `}`.  One entry per class body that deferred anything, each
// naming the class it stands in - so a reading opens them outermost first and a
// nested class's own member hides the enclosing class's exactly as it did where
// the body was written.
//
// The entries are a stack: a class takes its own out before the first of them
// is read, so the vector never holds one twice and one body is read exactly
// once however deep the nesting goes.
class DeferredReadings
{
public:
	// No region encloses this reading.
	static std::size_t none() { return static_cast<std::size_t>(-1); }

	// One class body a reading was written inside and that has already been
	// left: what its members were remembered under, what it declared, and the
	// class body around it.
	struct Region
	{
		std::string qualifier;
		DeclaredNames::Names names;
		std::size_t parent;
	};

	// 9.2p2: one reading put aside.
	struct Body
	{
		// Which of the clause's four contexts this entry stands for, which says
		// what rule the reading runs and which node its result is added to.
		enum Kind
		{
			// 8.4p1's function-body: the ctor-initializer and the
			// compound-statement, added to the definition they complete.
			kFunctionBody,
			// 8.3.6p1's `= initializer-clause` on a parameter-declaration.
			kDefaultArgument,
			// 9.2's brace-or-equal-initializer on a member-declarator.
			kMemberInitializer,
			// 15.4p1's `noexcept ( expression )` written as a function suffix.
			kExceptionSpecification
		};

		Body()
			: kind(kFunctionBody)
			, definition(nullptr)
			, target(nullptr)
			, declarator(nullptr)
			, clause(nullptr)
			, region(none())
			, written(0)
			, ends(0)
			, has_initializer(false)
		{
		}

		Kind kind;
		// The definition the compound-statement is the last child of.
		AstNode* definition;
		// The node the reading's result is added to.  It is `definition` for a
		// function-body and the node the syntax left empty for the other three,
		// each of which holds exactly the one child the reading makes - so the
		// child lands where the rule that skipped it would have put it.
		AstNode* target;
		// 8.3.5p10: the declarator whose places the body names.
		const AstNode* declarator;
		// 14.1p2: the head the member template was written under, or null for
		// a member that is not a template.  The clause's own places went out of
		// scope with the declarator; every head above it is still in force,
		// because the class body this reading is made at stands inside them.
		const AstNode* clause;
		// The class bodies this reading stands in that the reading is not made
		// inside, innermost, or `none()` for a member of the class making it.
		std::size_t region;
		// The first terminal this entry owns.  A parse that resets behind it
		// abandoned the alternative the entry was recorded in, which is what
		// `trim` reads: a function-body is recorded where the definition is
		// already settled, but the other three stand inside a declarator, which
		// is read once per alternative the declaration is tried under.
		std::size_t written;
		// One past the last terminal this entry owns, or 0 for a function-body,
		// whose `}` is what says where it ends.  The other three end at a
		// delimiter the run around them writes, so the skip is what found the
		// end and the reading is held to it: a reading that stops short leaves
		// terminals the program wrote and no rule reads.
		std::size_t ends;
		// 12.6.2p1: whether a ctor-initializer stands before the body, and where.
		// 8.4p1 makes a function-body the ctor-initializer and the
		// compound-statement both, so 9.2p2 completes the class for the two of
		// them - the list is read here only to find the `{` the body opens at,
		// and a list naming a member the class declares below it cannot be read
		// here at all.
		bool has_initializer;
		ParseMark initializer;
		// The `{` the body opens at, with the bracket state it was met under.
		ParseMark body;
	};

	std::size_t size() const { return bodies_.size(); }
	Body& at(std::size_t index) { return bodies_[index]; }
	const Region& region(std::size_t index) const { return regions_[index]; }
	void add(const Body& held) { bodies_.push_back(held); }
	void resize(std::size_t size) { bodies_.resize(size); }

	// The entries an abandoned alternative recorded, dropped.  A rule that
	// resets its cursor to `pos` gave up everything it read from there on, so
	// every entry whose own terminals begin at or after it went with it.  The
	// entries stand in the order they were recorded, which is the order of their
	// terminals, so the drop is from the back and costs one comparison per reset
	// plus one per entry ever recorded.
	void trim(std::size_t pos)
	{
		while (bodies_.size() > floor_ && bodies_.back().written >= pos)
		{
			bodies_.pop_back();
		}
	}

	// The entries below `floor` are not this parse's to drop: while a reading is
	// being made the cursor jumps back to where the construct was written, which
	// is behind every entry the classes around it are still holding.
	std::size_t floor() const { return floor_; }
	void set_floor(std::size_t floor) { floor_ = floor; }

	// 9.2p2 in a nested class: the entries this class body deferred, handed to
	// the class around it with the region they stood in recorded.  An entry a
	// class nested in *this* one deferred already names its own region, so what
	// this one becomes is that region's parent.
	void enclose(std::size_t from, const std::string& qualifier,
	             const DeclaredNames::Names& names);

	// The regions a reading stands in, innermost first - which the caller opens
	// in reverse, because the outermost is the one the class making the reading
	// stands closest to.
	void chain(std::size_t region, std::vector<std::size_t>& out) const;

	DeferredReadings()
		: floor_(0)
	{
	}

private:
	std::vector<Body> bodies_;
	std::size_t floor_;
	// One per class body that deferred anything.  They outlive the entries that
	// name them, because an entry is taken out before it is read; the count is
	// one per nested class body the unit writes and nothing walks them.
	std::vector<Region> regions_;
};

inline void DeferredReadings::enclose(std::size_t from,
                                      const std::string& qualifier,
                                      const DeclaredNames::Names& names)
{
	if (from >= bodies_.size())
	{
		return;
	}
	const std::size_t made = regions_.size();
	regions_.resize(made + 1);
	regions_[made].qualifier = qualifier;
	regions_[made].names = names;
	regions_[made].parent = none();
	for (std::size_t index = from; index < bodies_.size(); ++index)
	{
		if (bodies_[index].region == none())
		{
			bodies_[index].region = made;
			continue;
		}
		// The entry already stands in a class this one encloses, so this class
		// is what that nest's outermost region stands in.  Several entries
		// share one region, and the walk stops as soon as one of them has said
		// so.
		std::size_t root = bodies_[index].region;
		while (regions_[root].parent != none())
		{
			root = regions_[root].parent;
		}
		if (root != made)
		{
			regions_[root].parent = made;
		}
	}
}

inline void DeferredReadings::chain(std::size_t region,
                                    std::vector<std::size_t>& out) const
{
	out.clear();
	for (std::size_t at = region; at != none(); at = regions_[at].parent)
	{
		out.push_back(at);
	}
}
