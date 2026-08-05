#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// The identifiers of a run, each stored once.
//
// From phase 7 on, a name is carried, compared and keyed on as the small
// integer this hands out rather than as its letters, so a scope map is keyed
// by a word and a lookup costs no string compare.  The spelling is kept
// because the output formats ask for it back.
typedef std::uint32_t NameId;

// The identifier no name has.  It is the empty spelling, which no identifier
// can have, so an unnamed namespace and a missing declarator-id can both use
// it as "no name here" without colliding with a real one.
const NameId kNoName = 0;

class NameTable
{
public:
	NameTable() { intern(std::string()); }

	NameId intern(const std::string& text)
	{
		const std::pair<Map::iterator, bool> entry =
			ids_.insert(std::make_pair(text, static_cast<NameId>(texts_.size())));
		if (entry.second)
		{
			texts_.push_back(&entry.first->first);
		}
		return entry.first->second;
	}

	const std::string& text(NameId name) const { return *texts_[name]; }

private:
	typedef std::unordered_map<std::string, NameId> Map;

	// A key of `ids_` keeps its address for as long as the entry lives, so
	// `texts_` points at one rather than holding a second copy of it.
	Map ids_;
	std::vector<const std::string*> texts_;
};
