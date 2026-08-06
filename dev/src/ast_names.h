#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

// What kind of thing the declaration of a name introduced.
//
// The shared grammar accepts any identifier where it names a type and any
// identifier where it names a template, so `foo(x);` is a declaration of `x`
// when `foo` names a type and a call when it names an object, and `a < b > c`
// is a template-id when `a` names a template and two comparisons when it names
// an object.  Both resolutions need only this much about a name; lookup proper
// is a later assignment.
enum class NameKind
{
	Unknown,
	Type,
	Value,
	Template
};

// The names a translation unit has declared so far, by scope.
class DeclaredNames
{
public:
	DeclaredNames() { push(); }

	void push() { scopes_.resize(scopes_.size() + 1); }
	void pop() { scopes_.pop_back(); }

	void declare(const std::string& name, NameKind kind)
	{
		if (!name.empty())
		{
			scopes_.back()[name] = kind;
		}
	}

	// What the innermost declaration of `name` declared, or `Unknown` when no
	// declaration in scope names it.
	NameKind kind_of(const std::string& name) const;

	bool is_value(const std::string& name) const
	{
		return kind_of(name) == NameKind::Value;
	}

private:
	std::vector<std::unordered_map<std::string, NameKind> > scopes_;
};
