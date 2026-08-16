#include "sema_name.h"

namespace
{

bool is_identifier_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '$';
}

// The eight characters a balanced run of a spelling is written with.  A name is
// scanned once for every name written inside it, so every other character
// answers one question rather than eight.  5.2.3p3's braces are among them
// because a template-argument-list may write `A<S{1, 2}>`, whose comma belongs
// to the initializer and not to the list.
struct Brackets
{
	Brackets()
	{
		for (unsigned index = 0; index < 256; ++index)
		{
			written[index] = false;
		}
		const char* const marks = "<>()[]{}";
		for (const char* mark = marks; *mark != '\0'; ++mark)
		{
			written[static_cast<unsigned char>(*mark)] = true;
		}
	}

	bool written[256];
};

const Brackets kBrackets;

bool group_opens(char c)
{
	return c == '(' || c == '[' || c == '{';
}

bool group_closes(char c)
{
	return c == ')' || c == ']' || c == '}';
}

}

AngleReading::AngleReading(const std::string& spelling)
	: spelling_(&spelling)
{
	// The reading a spelling that closes everything it opened already has: one
	// `>` per `<` the candidate test admits, so none of them is 5.9's.  This is
	// nearly every name a program writes, and it costs the one scan the reader
	// was going to make anyway.
	unsigned depth = 0;
	unsigned grouped = 0;
	bool candidates = false;
	for (std::string::size_type at = 0; at < spelling.size(); ++at)
	{
		const char c = spelling[at];
		if (!kBrackets.written[static_cast<unsigned char>(c)])
		{
			continue;
		}
		if (group_opens(c))
		{
			++grouped;
		}
		else if (group_closes(c))
		{
			if (grouped != 0)
			{
				--grouped;
			}
		}
		else if (grouped != 0)
		{
			continue;
		}
		else if (c == '<')
		{
			if (opens_template_arguments(spelling, at))
			{
				candidates = true;
				++depth;
			}
		}
		else if (depth != 0)
		{
			--depth;
		}
	}
	if (depth != 0 && candidates)
	{
		resolve();
	}
}

// The one reading of a spelling whose `<` do not all close.
//
// `ends_[p]` is where the run enclosing `p` closes when the scan is resumed at
// `p` - one past its `>`, or `npos` where no reading closes it - and `tops_[p]`
// says the same of a scan resumed at `p` with no run open, which ends at the
// end of the spelling instead.  Both are facts of a *suffix*, so both are read
// off the entry to their right and the whole reading is one backward pass: a
// candidate `<` opens its list when the list closes and what follows the close
// still reads, and writes 5.9's operator when it does not.  The forward walk
// then replays those decisions from the one place that knows which of the two
// questions is being asked, which is the level the scan stands at.
void AngleReading::resolve()
{
	const std::string& spelling = *spelling_;
	const std::string::size_type size = spelling.size();
	// 5.1.1p6's parentheses, 5.2.1p1's subscript and 5.2.3p3's braces hold 5's
	// whole expression grammar, so each is stepped over whole and no `<` or `>`
	// inside one is read at all.
	std::vector<std::string::size_type> after(size + 1, std::string::npos);
	{
		std::vector<std::string::size_type> opened;
		for (std::string::size_type at = 0; at < size; ++at)
		{
			const char c = spelling[at];
			if (group_opens(c))
			{
				opened.push_back(at);
			}
			else if (group_closes(c) && !opened.empty())
			{
				after[opened.back()] = at + 1;
				opened.pop_back();
			}
		}
	}
	ends_.assign(size + 1, std::string::npos);
	std::vector<unsigned char> tops(size + 1, 0);
	tops[size] = 1;
	for (std::string::size_type at = size; at-- > 0;)
	{
		const char c = spelling[at];
		if (group_opens(c))
		{
			const std::string::size_type past = after[at];
			ends_[at] = past == std::string::npos
				? std::string::npos : ends_[past];
			tops[at] = past != std::string::npos && tops[past] != 0;
			continue;
		}
		if (group_closes(c))
		{
			continue;
		}
		if (c == '>')
		{
			ends_[at] = at + 1;
			continue;
		}
		if (c == '<' && opens_template_arguments(spelling, at))
		{
			const std::string::size_type inner = ends_[at + 1];
			const bool closes = inner != std::string::npos &&
				ends_[inner] != std::string::npos;
			ends_[at] = closes ? ends_[inner] : inner;
			tops[at] = inner != std::string::npos && tops[inner] != 0
				? 1 : tops[at + 1];
			continue;
		}
		ends_[at] = ends_[at + 1];
		tops[at] = tops[at + 1];
	}
	opens_.assign(size, 0);
	// The level the scan stands at, which is what says whether an opened list
	// has to close before the end of the spelling or before its enclosing `>`.
	// A name writes no run at its outermost level, so a `<` there always opens;
	// a template-argument is an expression, and its own may not.
	std::vector<unsigned char> level(1, 1);
	for (std::string::size_type at = 0; at < size;)
	{
		const char c = spelling[at];
		if (group_opens(c))
		{
			const std::string::size_type past = after[at];
			at = past == std::string::npos ? size : past;
			continue;
		}
		if (c == '>')
		{
			if (level.size() > 1)
			{
				level.pop_back();
			}
			++at;
			continue;
		}
		if (c == '<' && opens_template_arguments(spelling, at))
		{
			const std::string::size_type inner = ends_[at + 1];
			const bool closes = inner != std::string::npos &&
				(level.back() != 0 ? tops[inner] != 0
				                   : ends_[inner] != std::string::npos);
			if (closes)
			{
				opens_[at] = 1;
				level.push_back(0);
			}
		}
		++at;
	}
}

std::string::size_type AngleReading::list_end(std::string::size_type at) const
{
	return opens_.empty() ? std::string::npos : ends_[at + 1];
}

std::string AngleReading::spelled(std::string::size_type from,
                                  std::string::size_type to) const
{
	const std::string& spelling = *spelling_;
	if (opens_.empty())
	{
		return spelling.substr(from, to - from);
	}
	std::string written;
	for (std::string::size_type at = from; at < to; ++at)
	{
		// The separator phase 7 wrote and PA10's flattening dropped: without it
		// a reader of this run alone has no way back to what the `<` writes,
		// because the run no longer holds the `>` that settled the question.
		if (spelling[at] == '<' && opens_[at] == 0 &&
		    opens_template_arguments(spelling, at))
		{
			written += ' ';
		}
		written += spelling[at];
	}
	return written;
}

namespace
{

// The offset of the first `character` of `spelling` at or after `from` that no
// template-argument-list, parenthesis or subscript encloses, or `npos`.
std::string::size_type outside_brackets(const std::string& spelling,
                                        std::string::size_type from,
                                        char character,
                                        const AngleReading& angles)
{
	unsigned depth = 0;
	// 5.1.1p6's parentheses and 5.2.1p1's subscript are counted apart from
	// 14.2's list, because what they hold is an expression: a `<` inside one is
	// 5.9's operator and closes nothing.
	unsigned grouped = 0;
	for (std::string::size_type at = from; at < spelling.size(); ++at)
	{
		const char c = spelling[at];
		if (depth == 0 && grouped == 0 && c == character)
		{
			return at;
		}
		if (!kBrackets.written[static_cast<unsigned char>(c)])
		{
			continue;
		}
		if (c == '(' || c == '[' || c == '{')
		{
			++grouped;
		}
		else if (c == ')' || c == ']' || c == '}')
		{
			if (grouped != 0)
			{
				--grouped;
			}
		}
		else if (grouped != 0)
		{
			continue;
		}
		else if (c == '<')
		{
			if (angles.opens(at))
			{
				++depth;
			}
		}
		else if (c == '>' && depth != 0)
		{
			// A `>>` that closes two template-argument-lists is two terminals
			// here, because the spelling is of the terminals the parse matched.
			--depth;
		}
	}
	return std::string::npos;
}

}

bool opens_template_arguments(const std::string& spelling,
                              std::string::size_type at)
{
	if (at == 0 || !is_identifier_char(spelling[at - 1]))
	{
		// 5.9p1's operand stands here rather than a name, so what the `<`
		// writes is the operator.
		return false;
	}
	if (spelling[at - 1] < '0' || spelling[at - 1] > '9')
	{
		// A name whose last character is not a digit did not begin with one
		// either, so the only question left is 13.5p1's: `operator<` and its
		// three fellows are the name and take no argument list of their own.
		// Only a name ending in `r` can be that one, which is what keeps the
		// comparison off every other `<` a spelling writes.
		if (spelling[at - 1] != 'r' || at < 8 ||
		    spelling.compare(at - 8, 8, "operator") != 0)
		{
			return true;
		}
		return at != 8 && is_identifier_char(spelling[at - 9]);
	}
	std::string::size_type start = at;
	while (start != 0 && is_identifier_char(spelling[start - 1]))
	{
		--start;
	}
	// 2.11p1: an identifier does not open with a digit, so a run that does is
	// 2.14.2's number and the `<` after it is 5.9's operator.
	return spelling[start] < '0' || spelling[start] > '9';
}

std::string::size_type spelling_balanced_end(const std::string& spelling,
                                             std::string::size_type at)
{
	return AngleReading(spelling).balanced_end(at);
}

std::string::size_type AngleReading::balanced_end(
	std::string::size_type at) const
{
	const std::string& spelling = *spelling_;
	if (spelling[at] == '<')
	{
		if (!opens(at))
		{
			return std::string::npos;
		}
		const std::string::size_type end = list_end(at);
		if (end != std::string::npos)
		{
			return end;
		}
	}
	const char open = spelling[at];
	const char close = open == '<' ? '>'
		: (open == '(' ? ')' : (open == '{' ? '}' : ']'));
	const std::string::size_type opened = at;
	const bool angled = open == '<';
	unsigned depth = 0;
	unsigned grouped = 0;
	for (; at < spelling.size(); ++at)
	{
		const char c = spelling[at];
		if (!kBrackets.written[static_cast<unsigned char>(c)])
		{
			continue;
		}
		if (angled)
		{
			if (c == '(' || c == '[' || c == '{')
			{
				++grouped;
				continue;
			}
			if (c == ')' || c == ']' || c == '}')
			{
				if (grouped == 0)
				{
					// The run would have to close outside the group it was
					// opened in, so what opened it was 5.9's operator.
					return std::string::npos;
				}
				--grouped;
				continue;
			}
			if (grouped != 0)
			{
				continue;
			}
		}
		if (angled && c == '<' && at != opened && !opens(at))
		{
			continue;
		}
		if (c == open)
		{
			++depth;
			continue;
		}
		if (c != close)
		{
			continue;
		}
		if (--depth == 0)
		{
			return at + 1;
		}
	}
	return std::string::npos;
}

QualifiedName::QualifiedName(const std::string& spelling)
	: spelling_(&spelling)
{
	const AngleReading angles(spelling);
	std::string::size_type at = 0;
	for (;;)
	{
		const std::string::size_type next =
			outside_brackets(spelling, at, ':', angles);
		if (next == std::string::npos || next + 1 >= spelling.size() ||
		    spelling[next + 1] != ':')
		{
			return;
		}
		if (starts_.empty())
		{
			starts_.push_back(0);
		}
		at = next + 2;
		starts_.push_back(at);
	}
}

std::string QualifiedName::part(std::size_t index) const
{
	if (starts_.empty())
	{
		return *spelling_;
	}
	const std::string::size_type start = starts_[index];
	const std::string::size_type end = index + 1 < starts_.size()
		? starts_[index + 1] - 2
		: spelling_->size();
	return spelling_->substr(start, end - start);
}

namespace
{

// `spelling[from, to)` without the spaces the parse left between two terminals
// that would otherwise have run together, and with the ones `angles` says a
// reader of that run alone would need put back.
std::string trimmed(const AngleReading& angles, const std::string& spelling,
                    std::string::size_type from, std::string::size_type to)
{
	while (from < to && spelling[from] == ' ')
	{
		++from;
	}
	while (to > from && spelling[to - 1] == ' ')
	{
		--to;
	}
	return angles.spelled(from, to);
}

}

TemplateId::TemplateId(const std::string& spelling)
	: valid_(false)
{
	const AngleReading angles(spelling);
	const std::string::size_type angle =
		outside_brackets(spelling, 0, '<', angles);
	if (angle == std::string::npos || spelling.empty() ||
	    spelling[spelling.size() - 1] != '>')
	{
		return;
	}
	name_ = trimmed(angles, spelling, 0, angle);
	if (name_.empty())
	{
		return;
	}
	// The arguments are what the `,` at the depth of the list itself separate.
	// The scan stays on the whole spelling rather than on the run inside the
	// `<`, because what a `<` written in there is was settled by the `>` that
	// closes the list: a reading of the run alone no longer holds it.
	const std::string::size_type inside = spelling.size() - 1;
	std::string::size_type at = angle + 1;
	for (;;)
	{
		std::string::size_type next =
			outside_brackets(spelling, at, ',', angles);
		if (next == std::string::npos || next > inside)
		{
			next = inside;
			arguments_.push_back(trimmed(angles, spelling, at, next));
			break;
		}
		arguments_.push_back(trimmed(angles, spelling, at, next));
		at = next + 1;
	}
	// `f<>` names a template with an empty argument list rather than a
	// template-id with one empty argument.
	if (arguments_.size() == 1 && arguments_[0].empty())
	{
		arguments_.clear();
	}
	valid_ = true;
}

bool QualifiedName::names_a_template_id() const
{
	const std::string name = last();
	const AngleReading angles(name);
	const std::string::size_type angle = outside_brackets(name, 0, '<', angles);
	if (angle == std::string::npos)
	{
		return false;
	}
	std::string::size_type end = angle;
	while (end > 0 && name[end - 1] == ' ')
	{
		--end;
	}
	// 13.5: an operator-function-id is written `operator` and an operator, and
	// four of the operators are spelled with `<`.
	if (end >= 8 && name.compare(end - 8, 8, "operator") == 0 &&
	    (end == 8 || !is_identifier_char(name[end - 9])))
	{
		return false;
	}
	return end != 0;
}
