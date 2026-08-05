#include "macro_model.h"

#include <algorithm>
#include <cstring>
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

const std::size_t SpellingPool::kBlockSize;

SpellingPool::SpellingPool()
	: slots_(1024, 0)
	, mask_(1023)
	, used_(kBlockSize)
{
	// Id zero is the empty spelling, which is what a marker and a placemarker
	// carry, so no real token ever has to test for it.
	intern("", 0);
}

std::size_t SpellingPool::hash(const char* text, std::size_t size)
{
	// FNV-1a: a spelling is a handful of bytes, so a byte at a time with no
	// set-up beats a block hash.
	std::size_t value = 14695981039346656037ULL;
	for (std::size_t index = 0; index < size; ++index)
	{
		value ^= static_cast<unsigned char>(text[index]);
		value *= 1099511628211ULL;
	}
	return value;
}

bool SpellingPool::matches(SpellingId id, const char* text,
                           std::size_t size) const
{
	const Spelling& spelling = spellings_[id];
	return spelling.size == size &&
		std::memcmp(spelling.data, text, size) == 0;
}

std::size_t SpellingPool::slot_of(const char* text, std::size_t size) const
{
	std::size_t slot = hash(text, size) & mask_;
	while (slots_[slot] != 0 && !matches(slots_[slot] - 1, text, size))
	{
		slot = (slot + 1) & mask_;
	}
	return slot;
}

const char* SpellingPool::store(const char* text, std::size_t size)
{
	if (blocks_.empty() || used_ + size > kBlockSize)
	{
		blocks_.push_back(std::vector<char>());
		blocks_.back().resize(size > kBlockSize ? size : kBlockSize);
		used_ = 0;
	}
	char* at = blocks_.back().data() + used_;
	if (size != 0)
	{
		std::memcpy(at, text, size);
	}
	used_ += size;
	return at;
}

void SpellingPool::grow()
{
	slots_.assign(slots_.size() * 2, 0);
	mask_ = slots_.size() - 1;
	for (std::size_t id = 0; id < spellings_.size(); ++id)
	{
		const Spelling& spelling = spellings_[id];
		std::size_t slot = hash(spelling.data, spelling.size) & mask_;
		while (slots_[slot] != 0)
		{
			slot = (slot + 1) & mask_;
		}
		slots_[slot] = static_cast<SpellingId>(id + 1);
	}
}

SpellingId SpellingPool::intern(const char* text, std::size_t size)
{
	std::size_t slot = slot_of(text, size);
	if (slots_[slot] != 0)
	{
		return slots_[slot] - 1;
	}
	// Kept under three quarters full: linear probing degrades sharply past
	// that, and the table is one integer per slot.
	if ((spellings_.size() + 1) * 4 >= slots_.size() * 3)
	{
		grow();
		slot = slot_of(text, size);
	}
	const Spelling spelling = { store(text, size),
		static_cast<std::uint32_t>(size) };
	const SpellingId id = static_cast<SpellingId>(spellings_.size());
	spellings_.push_back(spelling);
	slots_[slot] = id + 1;
	return id;
}

PaintSets::PaintSets()
{
	// Id zero is the empty set, which is its own parent so that a walk over it
	// ends without a test of its own.
	Entry root;
	root.parent = 0;
	root.name = 0;
	root.size = 0;
	entries_.push_back(root);
}

bool PaintSets::contains(PaintId set, SpellingId name) const
{
	while (set != 0)
	{
		const Entry& entry = entries_[set];
		if (entry.name == name)
		{
			return true;
		}
		set = entry.parent;
	}
	return false;
}

void PaintSets::collect(PaintId set, std::vector<SpellingId>& names) const
{
	names.clear();
	names.reserve(entries_[set].size);
	while (set != 0)
	{
		names.push_back(entries_[set].name);
		set = entries_[set].parent;
	}
	std::sort(names.begin(), names.end());
}

// A set named by its members rather than by how it was reached, which is what
// an intersection produces.  Building it back up through `add` in one order
// gives it the id it would have had if it had been reached that way.
PaintId PaintSets::build(const std::vector<SpellingId>& names)
{
	PaintId set = 0;
	for (std::size_t index = 0; index < names.size(); ++index)
	{
		set = add(set, names[index]);
	}
	return set;
}

PaintId PaintSets::add(PaintId set, SpellingId name)
{
	const std::uint64_t key = (static_cast<std::uint64_t>(set) << 32) | name;
	const std::unordered_map<std::uint64_t, PaintId>::iterator found =
		added_.find(key);
	if (found != added_.end())
	{
		return found->second;
	}

	PaintId result = set;
	if (!contains(set, name))
	{
		Entry entry;
		entry.parent = set;
		entry.name = name;
		entry.size = entries_[set].size + 1;
		result = static_cast<PaintId>(entries_.size());
		entries_.push_back(entry);
	}
	added_.insert(std::make_pair(key, result));
	return result;
}

PaintId PaintSets::intersect(PaintId left, PaintId right)
{
	// The head and the closing paren of an invocation almost always carry the
	// same set, because they almost always come from the same place.
	if (left == right)
	{
		return left;
	}
	if (left == 0 || right == 0)
	{
		return 0;
	}
	const std::uint64_t key = (static_cast<std::uint64_t>(left) << 32) | right;
	const std::unordered_map<std::uint64_t, PaintId>::iterator found =
		intersected_.find(key);
	if (found != intersected_.end())
	{
		return found->second;
	}

	collect(left, left_);
	collect(right, right_);
	common_.clear();
	std::set_intersection(left_.begin(), left_.end(),
		right_.begin(), right_.end(), std::back_inserter(common_));
	const PaintId result = build(common_);
	intersected_.insert(std::make_pair(key, result));
	return result;
}

MacroSpellings::MacroSpellings(SpellingPool& spellings)
	: va_args(spellings.intern("__VA_ARGS__"))
	, hash(spellings.intern("#"))
	, alt_hash(spellings.intern("%:"))
	, hash_hash(spellings.intern("##"))
	, alt_hash_hash(spellings.intern("%:%:"))
	, lparen(spellings.intern("("))
	, rparen(spellings.intern(")"))
	, comma(spellings.intern(","))
	, ellipsis(spellings.intern("..."))
	, less(spellings.intern("<"))
	, greater(spellings.intern(">"))
	, define(spellings.intern("define"))
	, undef(spellings.intern("undef"))
	, if_(spellings.intern("if"))
	, ifdef(spellings.intern("ifdef"))
	, ifndef(spellings.intern("ifndef"))
	, elif(spellings.intern("elif"))
	, else_(spellings.intern("else"))
	, endif(spellings.intern("endif"))
	, include(spellings.intern("include"))
	, line(spellings.intern("line"))
	, error(spellings.intern("error"))
	, pragma(spellings.intern("pragma"))
	, defined(spellings.intern("defined"))
	, once(spellings.intern("once"))
	, pragma_operator(spellings.intern("_Pragma"))
{}

MacroTable::MacroTable(SpellingPool& spellings, const MacroSpellings& spelled)
	: spellings_(spellings)
	, spelled_(spelled)
{}

void MacroTable::check_name(const MacroToken& token) const
{
	if (token.type != MacroTokenType::Identifier)
	{
		throw macro_error("a macro name has to be an identifier");
	}
	if (token.spelling == spelled_.va_args)
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
	    body->spelling == spelled_.lparen && !body->whitespace_before)
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
	if (at != end && is_punctuation(*at, spelled_.rparen))
	{
		return at + 1;
	}
	while (true)
	{
		if (at == end)
		{
			throw macro_error("#define parameter list is not terminated");
		}
		if (is_punctuation(*at, spelled_.ellipsis))
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
		if (is_punctuation(*at, spelled_.rparen))
		{
			return at + 1;
		}
		if (!is_punctuation(*at, spelled_.comma) || macro.variadic)
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
	if (token.spelling == spelled_.va_args)
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
		if (is_operator && (at->spelling == spelled_.hash_hash ||
		                    at->spelling == spelled_.alt_hash_hash))
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
		    (at->spelling == spelled_.hash || at->spelling == spelled_.alt_hash))
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
		         at->spelling == spelled_.va_args)
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
		if (item.kind == MacroBodyItem::Stringize)
		{
			macro.keeps_raw_arguments = true;
			continue;
		}
		if (item.kind != MacroBodyItem::Parameter)
		{
			continue;
		}
		const bool pasted = item.paste_before ||
			(index + 1 < macro.body.size() && macro.body[index + 1].paste_before);
		item.raw_argument = pasted;
		if (pasted)
		{
			macro.keeps_raw_arguments = true;
		}
		else
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
		    before.spelling != spelled_.comma)
		{
			continue;
		}
		// `, ## __VA_ARGS__` deletes the comma when the variadic argument is
		// empty and otherwise behaves as an ordinary substitution.
		before.drop_for_empty_va = true;
		item.paste_before = false;
	}
}

void MacroTable::define_builtin(SpellingId name, BuiltinMacro builtin)
{
	MacroDefinition macro;
	macro.builtin = builtin;
	install(name, macro);
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
	if (left.builtin != right.builtin ||
	    left.function_like != right.function_like ||
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
