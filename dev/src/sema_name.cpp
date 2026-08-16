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

// The offset of the first `character` of `spelling` at or after `from` that no
// template-argument-list, parenthesis or subscript encloses, or `npos`.
std::string::size_type outside_brackets(const std::string& spelling,
                                        std::string::size_type from,
                                        char character)
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
			if (opens_template_arguments(spelling, at))
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
		if (angled && c == '<' && at != opened &&
		    !opens_template_arguments(spelling, at))
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
	std::string::size_type at = 0;
	for (;;)
	{
		const std::string::size_type next = outside_brackets(spelling, at, ':');
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
// that would otherwise have run together.
std::string trimmed(const std::string& spelling, std::string::size_type from,
                    std::string::size_type to)
{
	while (from < to && spelling[from] == ' ')
	{
		++from;
	}
	while (to > from && spelling[to - 1] == ' ')
	{
		--to;
	}
	return spelling.substr(from, to - from);
}

}

TemplateId::TemplateId(const std::string& spelling)
	: valid_(false)
{
	const std::string::size_type angle = outside_brackets(spelling, 0, '<');
	if (angle == std::string::npos || spelling.empty() ||
	    spelling[spelling.size() - 1] != '>')
	{
		return;
	}
	name_ = trimmed(spelling, 0, angle);
	if (name_.empty())
	{
		return;
	}
	// The arguments are what the `,` at the depth of the list itself separate;
	// the list is scanned from inside the `<`, so every bracket the scan meets
	// is one an argument opened.
	const std::string inside =
		spelling.substr(angle + 1, spelling.size() - angle - 2);
	std::string::size_type at = 0;
	for (;;)
	{
		const std::string::size_type next = outside_brackets(inside, at, ',');
		arguments_.push_back(
			trimmed(inside, at, next == std::string::npos ? inside.size() : next));
		if (next == std::string::npos)
		{
			break;
		}
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
	const std::string::size_type angle = outside_brackets(name, 0, '<');
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
