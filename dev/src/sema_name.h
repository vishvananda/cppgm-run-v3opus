#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The structure of a name, which PA10 hands on as the spelling it was written
// with.
//
// A qualified-id reaches the semantic layer as one string, because that is what
// the PA10 dump names it: a name the grammar leaves unresolved is spelled from
// the terminals it was matched from.  The components are what 3.4.3 asks about,
// so exactly one place turns the spelling back into them, and it is aware of
// what a component may hold: a `::` inside a template-argument-list, a
// parenthesized decltype-specifier or a subscript belongs to the component
// around it rather than separating two of them.
//
// The spelling is borrowed rather than copied, and an unqualified name - which
// is nearly every name - is recognised by the one scan the split needs anyway.

// 14.2: whether the `<` written at `at` opens a template-argument-list.
//
// A list is written after a *name*, and 2.11p1 leaves no name opening with a
// digit - so the `<` in `Box<0 < 1, int>` is 5.9's operator between two
// operands and the one in `A<B<int> >` opens a list.  13.5 spells four
// operators inside an operator-function-id, which is a name and writes none.
bool opens_template_arguments(const std::string& spelling,
                              std::string::size_type at);

// The end of the balanced run `spelling[at]` opens, one past its closer, or
// `npos` where the run does not close.  8.1p1's type-id and 5.19's constant
// expression each write three of them - a template-argument-list, a
// parenthesized expression or parameter-clause, a subscript - and each may hold
// the others.
//
// 5.1.1p6's parentheses and 5.2.1p1's subscript hold 5's whole expression
// grammar, so a `<` or `>` inside one is an operator however it is spelled: a
// run of either is stepped over whole, and an argument list that would have to
// reach past the group it stands in to close never opened.
std::string::size_type spelling_balanced_end(const std::string& spelling,
                                             std::string::size_type at);

class QualifiedName
{
public:
	explicit QualifiedName(const std::string& spelling);

	// The number of components.  A name written `::x` has two, the first empty,
	// which 3.4.3p1 makes the global namespace.
	std::size_t size() const { return starts_.empty() ? 1 : starts_.size(); }
	std::string part(std::size_t index) const;
	// The name a declaration of this name introduces.
	std::string last() const { return part(size() - 1); }
	// True when a nested-name-specifier was written, so that the declaration
	// names an entity of another region.
	bool qualified() const { return !starts_.empty(); }

	// 7.3.3p5: whether the last component is a template-id, which a
	// using-declaration shall not name.  An operator-function-id whose operator
	// is spelled with `<` is not one.
	bool names_a_template_id() const;

private:
	const std::string* spelling_;
	// The offset each component starts at, empty for an unqualified name.
	std::vector<std::string::size_type> starts_;
};

// 14.2: the template-name and template-argument-list a template-id is written
// from.
//
// A template-id reaches the semantic layer as one spelling for the reason a
// qualified-id does, so the one place that turns a spelling back into what was
// written splits this one too: an argument is what a `,` outside every nested
// list separates, and a `<` inside one belongs to it rather than opening
// another.
class TemplateId
{
public:
	// `spelling` is one component of a name, which `QualifiedName::last` is.
	explicit TemplateId(const std::string& spelling);

	// False when `spelling` is not a template-id, or holds no argument list a
	// `>` closes.
	bool valid() const { return valid_; }
	const std::string& name() const { return name_; }
	const std::vector<std::string>& arguments() const { return arguments_; }

private:
	bool valid_;
	std::string name_;
	std::vector<std::string> arguments_;
};
