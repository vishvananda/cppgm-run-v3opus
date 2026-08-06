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
//
// A template-name is two kinds rather than one, because the two resolutions
// disagree about it.  `a < b > c` is a template-id for either, so both are
// template-names.  But 14.2p3 makes a template-id of a class or alias template
// a type, while one of a function template names an overload set, so `f(x)` and
// `f<int>(x)` are declarations of `x` for the first and calls for the second.
enum class NameKind
{
	Unknown,
	Type,
	Value,
	Template,
	FunctionTemplate
};

// Every name fact the parse of one translation unit establishes.
//
// A name reaches a rule either as it was written in the scope that declared it
// or behind a nested-name-specifier.  PA10 models no scope to look into, so
// the second reading is answered by spelling: a member declared while the
// prefix names `ns` is remembered under `ns::f` as well as under `f`.  Both
// readings are the same fact, so one owner answers both.
//
// Every answer is a function of what has been declared, so a caller that
// remembers one has to know when it stopped being true.  The table counts its
// own changes rather than leaving each mutation to tell whoever cached an
// answer, because a name enters or leaves through several rules and only the
// table sees all of them.
class DeclaredNames
{
public:
	DeclaredNames()
		: version_(0)
	{
		scopes_.resize(1);
	}

	// A name the innermost scope alone declares: a parameter, a template
	// parameter, or the name a handler catches.  No qualified name reaches one.
	void declare(const std::string& name, NameKind kind);

	// A name a nested-name-specifier can also reach, which is remembered by
	// the spelling the prefix in force gives it.
	void declare_member(const std::string& name, NameKind kind);

	// 7.3.2p1: a namespace alias names the namespace its target names, so a
	// name written behind the alias is the name written behind that namespace.
	void alias(const std::string& name, const std::string& target);

	// 7.3.4p2: the declarations of the nominated namespace appear in the scope
	// the directive was written in, so a name that scope does not declare may
	// still be one of theirs.
	void nominate(const std::string& target);

	// What the innermost declaration of `name` declared, or `Unknown` when no
	// declaration in scope names it.  A name written with a `::` in it is a
	// spelling rather than a name in a scope, so it is answered as one.
	NameKind kind_of(const std::string& name) const;

	bool is_value(const std::string& name) const
	{
		return kind_of(name) == NameKind::Value;
	}

	// How many times the scopes have changed.  Two answers taken at the same
	// version are the same answer.
	unsigned long version() const { return version_; }

	// The scope an alternative opens for as long as it runs, so one that fails
	// leaves the names it declared behind however it leaves.
	class Scope
	{
	public:
		explicit Scope(DeclaredNames& names)
			: names_(names)
		{
			names_.scopes_.resize(names_.scopes_.size() + 1);
			++names_.version_;
		}

		~Scope()
		{
			names_.scopes_.pop_back();
			++names_.version_;
		}

	private:
		Scope(const Scope&);
		Scope& operator=(const Scope&);
		DeclaredNames& names_;
	};

	// The scope name that members are spelled against, held for as long as the
	// body that opened it is being read.  It changes no answer, only how the
	// next member is remembered, so it is not a version of its own.
	class Prefix
	{
	public:
		Prefix(DeclaredNames& names, const std::string& name)
			: names_(names)
			, saved_(names.prefix_)
		{
			if (!name.empty())
			{
				names_.prefix_ += name + "::";
			}
		}

		~Prefix() { names_.prefix_ = saved_; }

	private:
		Prefix(const Prefix&);
		Prefix& operator=(const Prefix&);
		DeclaredNames& names_;
		std::string saved_;
	};

private:
	// One scope: the names declared in it, and the namespaces 7.3.4p2 makes the
	// declarations of appear in it as well.
	struct Region
	{
		std::unordered_map<std::string, NameKind> names;
		// The `N::` each using-directive written here reaches, which is how a
		// name that is declared in the nominated namespace and written without
		// its prefix is answered.
		std::vector<std::string> nominated;
	};

	// What a spelling with a nested-name-specifier names, following the
	// namespace aliases its prefix was written through.
	NameKind spelled_kind(const std::string& spelling) const;
	// 3.4.3: the same question asked from where the name is written.  A member
	// is remembered under the whole prefix its declaration stood in, while
	// `nnn::f` written inside `B` spells only the part of that prefix the
	// region it is written in does not already supply - so the prefixes in
	// force are tried outward, longest first, which is the order the regions
	// around the name are searched in.
	NameKind reached_kind(const std::string& spelling) const;

	std::vector<Region> scopes_;
	// The names a qualified-id can reach, spelled as they are written.  A
	// nested-name-specifier names a scope the parser does not model, so the
	// spelling is the key: `ns::f` is what the source asks about, whatever
	// scope declared it.
	std::unordered_map<std::string, NameKind> qualified_;
	// The namespace each alias names, which is what turns a spelling written
	// through an alias into the one the declarations were remembered under.
	std::unordered_map<std::string, std::string> aliases_;
	std::string prefix_;
	unsigned long version_;
};
