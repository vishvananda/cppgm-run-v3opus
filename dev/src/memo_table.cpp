#include "memo_table.h"

void MemoTable::insert(std::uint64_t key, std::uint32_t value)
{
	if ((count_ + 1) * 4 >= slots_.size() * 3)
	{
		grow();
	}
	place(slots_, key, value);
	++count_;
}

void MemoTable::clear()
{
	std::vector<Slot> empty(kInitialSlots);
	slots_.swap(empty);
	count_ = 0;
}

void MemoTable::place(std::vector<Slot>& slots, std::uint64_t key,
                      std::uint32_t value)
{
	const std::size_t mask = slots.size() - 1;
	std::size_t i = hash(key) & mask;
	while (slots[i].key != kEmptyKey)
	{
		i = (i + 1) & mask;
	}
	slots[i].key = key + 1;
	slots[i].value = value;
}

void MemoTable::grow()
{
	std::vector<Slot> bigger(slots_.size() * 2);
	for (std::size_t i = 0; i < slots_.size(); ++i)
	{
		if (slots_[i].key != kEmptyKey)
		{
			place(bigger, slots_[i].key - 1, slots_[i].value);
		}
	}
	slots_.swap(bigger);
}
