#include "macro_model.h"

#include <algorithm>
#include <utility>

namespace
{

SourceError macro_error(const std::string& message)
{
	return SourceError(" " + message);
}

}

bool is_punctuation(const MacroToken& token, SpellingId spelling)
{
	return token.type == MacroTokenType::PreprocessingOpOrPunc &&
		token.spelling == spelling;
}

SpellingPool::SpellingPool()
{
	// Id zero is the empty spelling, which is what a marker and a placemarker
	// carry, so no real token ever has to test for it.
	intern(std::string());
}

SpellingId SpellingPool::intern(const std::string& text)
{
	std::pair<std::unordered_map<std::string, SpellingId>::iterator, bool> entry =
		ids_.insert(std::make_pair(text, static_cast<SpellingId>(texts_.size())));
	if (entry.second)
	{
		// The map owns the text and never rehashes its nodes, so the pointer
		// stays valid and the bytes are held once.
		texts_.push_back(&entry.first->first);
	}
	return entry.first->second;
}

PaintSets::PaintSets()
{
	entries_.push_back(Entry());
	entries_.back().begin = 0;
	entries_.back().size = 0;
	key_.clear();
	interned_.insert(std::make_pair(key_, static_cast<PaintId>(0)));
}

bool PaintSets::contains(PaintId set, SpellingId name) const
{
	const Entry& entry = entries_[set];
	const SpellingId* begin = names_.data() + entry.begin;
	return std::binary_search(begin, begin + entry.size, name);
}

PaintId PaintSets::intern(const std::vector<SpellingId>& names)
{
	key_.assign(reinterpret_cast<const char*>(names.data()),
		names.size() * sizeof(SpellingId));
	std::pair<std::unordered_map<std::string, PaintId>::iterator, bool> entry =
		interned_.insert(std::make_pair(key_, static_cast<PaintId>(entries_.size())));
	if (entry.second)
	{
		Entry record;
		record.begin = static_cast<std::uint32_t>(names_.size());
		record.size = static_cast<std::uint32_t>(names.size());
		names_.insert(names_.end(), names.begin(), names.end());
		entries_.push_back(record);
	}
	return entry.first->second;
}

PaintId PaintSets::add(PaintId set, SpellingId name)
{
	const std::uint64_t key = (static_cast<std::uint64_t>(set) << 32) | name;
	std::unordered_map<std::uint64_t, PaintId>::iterator found = added_.find(key);
	if (found != added_.end())
	{
		return found->second;
	}

	const Entry entry = entries_[set];
	scratch_.assign(names_.begin() + entry.begin,
		names_.begin() + entry.begin + entry.size);
	std::vector<SpellingId>::iterator at =
		std::lower_bound(scratch_.begin(), scratch_.end(), name);
	if (at == scratch_.end() || *at != name)
	{
		scratch_.insert(at, name);
	}
	const PaintId result = intern(scratch_);
	added_.insert(std::make_pair(key, result));
	return result;
}

PaintId PaintSets::intersect(PaintId left, PaintId right)
{
	if (left == right)
	{
		return left;
	}
	if (left == 0 || right == 0)
	{
		return 0;
	}
	const std::uint64_t key = (static_cast<std::uint64_t>(left) << 32) | right;
	std::unordered_map<std::uint64_t, PaintId>::iterator found = intersected_.find(key);
	if (found != intersected_.end())
	{
		return found->second;
	}

	const Entry a = entries_[left];
	const Entry b = entries_[right];
	scratch_.clear();
	std::set_intersection(
		names_.begin() + a.begin, names_.begin() + a.begin + a.size,
		names_.begin() + b.begin, names_.begin() + b.begin + b.size,
		std::back_inserter(scratch_));
	const PaintId result = intern(scratch_);
	intersected_.insert(std::make_pair(key, result));
	return result;
}

MacroTable::MacroTable(SpellingPool& spellings)
	: spellings_(spellings)
	, va_args_(spellings.intern("__VA_ARGS__"))
	, hash_(spellings.intern("#"))
	, alt_hash_(spellings.intern("%:"))
	, hash_hash_(spellings.intern("##"))
	, alt_hash_hash_(spellings.intern("%:%:"))
	, lparen_(spellings.intern("("))
	, rparen_(spellings.intern(")"))
	, comma_(spellings.intern(","))
	, ellipsis_(spellings.intern("..."))
{}

void MacroTable::check_name(const MacroToken& token) const
{
	if (token.type != MacroTokenType::Identifier)
	{
		throw macro_error("a macro name has to be an identifier");
	}
	if (token.spelling == va_args_)
	{
		// 16.3/5: `__VA_ARGS__` belongs to the replacement list of a variadic
		// macro and nowhere else.
		throw macro_error("__VA_ARGS__ is not a macro name");
	}
}

void MacroTable::define(const MacroToken* begin, const MacroToken* end)
{
	if (end - begin < 2)
	{
		throw macro_error("#define needs a macro name");
	}
	const MacroToken& name = begin[1];
	check_name(name);

	MacroDefinition macro;
	const MacroToken* body = begin + 2;
	if (body != end && body->type == MacroTokenType::PreprocessingOpOrPunc &&
	    body->spelling == lparen_ && !body->whitespace_before)
	{
		macro.function_like = true;
		body = parse_parameters(body + 1, end, macro);
	}
	analyse_body(body, end, macro);
	macro.replacement.assign(body, end);
	install(name.spelling, macro);
}

const MacroToken* MacroTable::parse_parameters(const MacroToken* begin,
                                               const MacroToken* end,
                                               MacroDefinition& macro) const
{
	const MacroToken* at = begin;
	if (at != end && is_punctuation(*at, rparen_))
	{
		return at + 1;
	}
	while (true)
	{
		if (at == end)
		{
			throw macro_error("#define parameter list is not terminated");
		}
		if (is_punctuation(*at, ellipsis_))
		{
			macro.variadic = true;
		}
		else
		{
			check_name(*at);
			if (std::find(macro.parameters.begin(), macro.parameters.end(),
			              at->spelling) != macro.parameters.end())
			{
				throw macro_error("#define repeats a parameter name");
			}
			macro.parameters.push_back(at->spelling);
		}
		++at;

		if (at == end)
		{
			throw macro_error("#define parameter list is not terminated");
		}
		if (is_punctuation(*at, rparen_))
		{
			return at + 1;
		}
		if (!is_punctuation(*at, comma_) || macro.variadic)
		{
			throw macro_error("#define parameters are a comma separated list");
		}
		++at;
	}
}

std::uint32_t MacroTable::parameter_index(const MacroDefinition& macro,
                                          const MacroToken& token) const
{
	if (token.type != MacroTokenType::Identifier || !macro.function_like)
	{
		return static_cast<std::uint32_t>(-1);
	}
	if (token.spelling == va_args_)
	{
		return macro.variadic
			? static_cast<std::uint32_t>(macro.variadic_index())
			: static_cast<std::uint32_t>(-1);
	}
	for (std::size_t index = 0; index < macro.parameters.size(); ++index)
	{
		if (macro.parameters[index] == token.spelling)
		{
			return static_cast<std::uint32_t>(index);
		}
	}
	return static_cast<std::uint32_t>(-1);
}

void MacroTable::analyse_body(const MacroToken* begin, const MacroToken* end,
                              MacroDefinition& macro) const
{
	bool paste_pending = false;
	for (const MacroToken* at = begin; at != end; ++at)
	{
		const bool is_operator = at->type == MacroTokenType::PreprocessingOpOrPunc;
		if (is_operator && (at->spelling == hash_hash_ ||
		                    at->spelling == alt_hash_hash_))
		{
			if (macro.body.empty() || paste_pending)
			{
				throw macro_error("## has to join two replacement list tokens");
			}
			paste_pending = true;
			continue;
		}

		MacroBodyItem item;
		item.kind = MacroBodyItem::Token;
		item.type = at->type;
		item.spelling = at->spelling;
		item.parameter = static_cast<std::uint32_t>(-1);
		item.whitespace_before = at->whitespace_before;
		item.paste_before = paste_pending;
		item.raw_argument = false;
		item.drop_for_empty_va = false;
		paste_pending = false;

		if (macro.function_like && is_operator &&
		    (at->spelling == hash_ || at->spelling == alt_hash_))
		{
			// 16.3.2: in a function-like macro `#` stringizes, so it has to
			// name a parameter.
			const std::uint32_t parameter = at + 1 == end
				? static_cast<std::uint32_t>(-1)
				: parameter_index(macro, at[1]);
			if (parameter == static_cast<std::uint32_t>(-1))
			{
				throw macro_error("# has to be followed by a macro parameter");
			}
			item.kind = MacroBodyItem::Stringize;
			item.parameter = parameter;
			++at;
			macro.body.push_back(item);
			continue;
		}

		const std::uint32_t parameter = parameter_index(macro, *at);
		if (parameter != static_cast<std::uint32_t>(-1))
		{
			item.kind = MacroBodyItem::Parameter;
			item.parameter = parameter;
		}
		else if (at->type == MacroTokenType::Identifier &&
		         at->spelling == va_args_)
		{
			throw macro_error("__VA_ARGS__ needs a variadic macro");
		}
		macro.body.push_back(item);
	}
	if (paste_pending)
	{
		throw macro_error("## has to join two replacement list tokens");
	}

	apply_gnu_comma_extension(macro);

	macro.expands_argument.assign(macro.parameter_count(), false);
	for (std::size_t index = 0; index < macro.body.size(); ++index)
	{
		MacroBodyItem& item = macro.body[index];
		if (item.kind != MacroBodyItem::Parameter)
		{
			continue;
		}
		const bool pasted = item.paste_before ||
			(index + 1 < macro.body.size() && macro.body[index + 1].paste_before);
		item.raw_argument = pasted;
		if (!pasted)
		{
			macro.expands_argument[item.parameter] = true;
		}
	}
}

void MacroTable::apply_gnu_comma_extension(MacroDefinition& macro) const
{
	if (!macro.variadic)
	{
		return;
	}
	const std::uint32_t va = static_cast<std::uint32_t>(macro.variadic_index());
	for (std::size_t index = 1; index < macro.body.size(); ++index)
	{
		MacroBodyItem& item = macro.body[index];
		MacroBodyItem& before = macro.body[index - 1];
		if (!item.paste_before || item.kind != MacroBodyItem::Parameter ||
		    item.parameter != va || before.kind != MacroBodyItem::Token ||
		    before.spelling != comma_)
		{
			continue;
		}
		// `, ## __VA_ARGS__` deletes the comma when the variadic argument is
		// empty and otherwise behaves as an ordinary substitution.
		before.drop_for_empty_va = true;
		item.paste_before = false;
	}
}

void MacroTable::undefine(const MacroToken* begin, const MacroToken* end)
{
	if (end - begin != 2)
	{
		throw macro_error("#undef needs exactly one macro name");
	}
	check_name(begin[1]);
	const SpellingId name = begin[1].spelling;
	if (name < by_name_.size())
	{
		by_name_[name] = -1;
	}
}

bool MacroTable::same_definition(const MacroDefinition& left,
                                 const MacroDefinition& right) const
{
	if (left.function_like != right.function_like ||
	    left.variadic != right.variadic ||
	    left.parameters != right.parameters ||
	    left.replacement.size() != right.replacement.size())
	{
		return false;
	}
	for (std::size_t index = 0; index < left.replacement.size(); ++index)
	{
		const MacroToken& a = left.replacement[index];
		const MacroToken& b = right.replacement[index];
		// 16.3/1: same spelling and same white-space separation, and every
		// separation counts as the same one.  What precedes the first token is
		// not a separation between replacement list tokens.
		if (a.spelling != b.spelling || a.type != b.type ||
		    (index != 0 && a.whitespace_before != b.whitespace_before))
		{
			return false;
		}
	}
	return true;
}

void MacroTable::install(SpellingId name, MacroDefinition& macro)
{
	if (name >= by_name_.size())
	{
		by_name_.resize(spellings_.size(), -1);
	}
	const std::int32_t existing = by_name_[name];
	if (existing >= 0)
	{
		// 16.3/2: a redefinition has to repeat the definition exactly.
		if (!same_definition(definitions_[static_cast<std::size_t>(existing)], macro))
		{
			throw macro_error("macro is redefined differently");
		}
		return;
	}
	by_name_[name] = static_cast<std::int32_t>(definitions_.size());
	definitions_.push_back(std::move(macro));
}
