#include "sema_name.h"

namespace
{

bool is_identifier_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '$';
}

// The offset of the first `character` of `spelling` at or after `from` that no
// template-argument-list, parenthesis or subscript encloses, or `npos`.
std::string::size_type outside_brackets(const std::string& spelling,
                                        std::string::size_type from,
                                        char character)
{
	unsigned depth = 0;
	for (std::string::size_type at = from; at < spelling.size(); ++at)
	{
		const char c = spelling[at];
		if (depth == 0 && c == character)
		{
			return at;
		}
		if (c == '<' || c == '(' || c == '[')
		{
			++depth;
		}
		else if ((c == '>' || c == ')' || c == ']') && depth != 0)
		{
			// A `>>` that closes two template-argument-lists is two terminals
			// here, because the spelling is of the terminals the parse matched.
			--depth;
		}
	}
	return std::string::npos;
}

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
