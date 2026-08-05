#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// What the recognizer remembers: one result per (rule, position, angle).
//
// The keys are dense and the table is written about three times as often as it
// is read, so an entry has to be cheap to add.  It is an open addressed array
// rather than a node based map: an entry is one slot and no allocation, and a
// probe is a multiply and a mask.  A value is the position the rule ended at,
// or `kFailed`.
class MemoTable
{
public:
	static const std::uint32_t kFailed = 0xFFFFFFFFu;

	MemoTable()
		: slots_(kInitialSlots)
		, count_(0)
	{}

	std::size_t size() const { return count_; }

	// True when `key` is remembered, in which case `value` is its result.
	bool find(std::uint64_t key, std::uint32_t& value) const
	{
		const std::size_t mask = slots_.size() - 1;
		for (std::size_t i = hash(key) & mask;; i = (i + 1) & mask)
		{
			if (slots_[i].key == kEmptyKey)
			{
				return false;
			}
			if (slots_[i].key == key + 1)
			{
				value = slots_[i].value;
				return true;
			}
		}
	}

	// Remembers `key`, which `find` has just reported absent.
	void insert(std::uint64_t key, std::uint32_t value);

	// Forgets everything and gives the memory back.
	void clear();

private:
	struct Slot
	{
		Slot() : key(kEmptyKey), value(0) {}
		std::uint64_t key;    // the memo key plus one, so zero means empty
		std::uint32_t value;
	};

	static const std::uint64_t kEmptyKey = 0;
	static const std::size_t kInitialSlots = 1024;

	// A multiplicative mix, because the low bits of a key are the rule number
	// and would otherwise collide across every position.
	static std::size_t hash(std::uint64_t key)
	{
		key *= 0x9E3779B97F4A7C15ull;
		return static_cast<std::size_t>(key >> 32);
	}

	static void place(std::vector<Slot>& slots, std::uint64_t key,
	                  std::uint32_t value);
	void grow();

	std::vector<Slot> slots_;
	std::size_t count_;
};
