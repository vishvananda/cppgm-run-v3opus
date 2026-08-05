#pragma once

#include <cstddef>

// How deep a recursive descent may go.
//
// A grammar recurses once per nesting level of its input, so a file can ask
// for a machine stack as deep as it is long.  Every rule that can re-enter
// itself opens a `Frame`, so this one counter bounds the stack the descent
// uses, and a file that nests deeper is refused rather than crashing.
//
// The refusal is sticky.  A rule that failed only because the limit was
// reached is not a fact about its position, so remembering it would poison a
// memo table for a shallower attempt at the same position.  Marking the whole
// descent instead makes every remaining rule fail at once, which is both
// correct and the quickest way out of a file that nests too deep.
class ParseDepth
{
public:
	// The costliest cycle measured in the PA6 grammar is nested template
	// argument lists, at about 230 bytes of stack per frame, so this keeps the
	// deepest accepted input inside about a quarter of the 8 MB default stack
	// while still admitting thousands of levels of nesting where Annex B
	// recommends supporting 256.
	static const std::size_t kMaxFrames = 10000;

	ParseDepth()
		: open_(0)
		, overflow_(false)
	{}

	bool overflowed() const { return overflow_; }

	// One level of the descent, opened for as long as the rule runs.
	class Frame
	{
	public:
		explicit Frame(ParseDepth& depth)
			: depth_(depth)
		{
			if (++depth_.open_ > kMaxFrames)
			{
				depth_.overflow_ = true;
			}
		}

		~Frame() { --depth_.open_; }

		bool overflowed() const { return depth_.overflow_; }

	private:
		Frame(const Frame&);
		Frame& operator=(const Frame&);

		ParseDepth& depth_;
	};

private:
	std::size_t open_;
	bool overflow_;
};
