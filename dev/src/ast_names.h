#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

	// 9.1: the class a class-head declared, whose members are remembered under
	// the prefix its name gives them.  It is what a base-specifier written
	// against the regions around a derived class is resolved to.
	void declare_class(const std::string& name);

	// 7.3.2p1: a namespace alias names the namespace its target names, so a
	// name written behind the alias is the name written behind that namespace.
	void alias(const std::string& name, const std::string& target);

	// 7.3.4p2: the declarations of the nominated namespace appear in the scope
	// the directive was written in, so a name that scope does not declare may
	// still be one of theirs.
	void nominate(const std::string& target);

	// 10.2p2: the declarations of a base class are found in the scope of a
	// class derived from it.  Only the direct bases are recorded, as they were
	// written, so a chain n deep costs n entries and not n^2; the chain itself
	// is walked where a name misses, which is the only place it is needed.
	void derive(const std::string& name, const std::string& base);

	// The prefix a region's own name brings into scope with it, whose bases the
	// walk that answers a name then follows.
	void reach(const std::string& qualifier);
	// The direct bases alone, which is what a class body's own scope adds: what
	// the class declares itself is in that scope already.
	void inherit(const std::string& qualifier);

	// 14.5.1.1: a member of a class template is remembered under the
	// template's own name, so a definition written on a template-id reaches it
	// once the argument list it was written with is dropped.  The spelling
	// without any argument list, or the spelling itself where it holds none.
	static std::string without_arguments(const std::string& spelling);

	// The prefix the members of `name` declared here are remembered under.
	std::string qualify(const std::string& name) const
	{
		return prefix_ + name + "::";
	}

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

	// 3.4.1p8: the region a qualified declarator-id names, whose declarations
	// the rest of that declarator and the body after it reach unqualified.  A
	// class encloses the regions around it, so every prefix of the qualifier is
	// reached and not only the innermost.
	//
	// A declarator-id that names no region opens nothing at all: an ordinary
	// declaration pays one comparison, and the version the parse memoizes
	// against does not move.
	class Reached
	{
	public:
		Reached(DeclaredNames& names, const std::string& qualifier)
			: names_(names)
			, opened_(qualifier.size() > 2)
		{
			if (!opened_)
			{
				return;
			}
			names_.scopes_.resize(names_.scopes_.size() + 1);
			const std::string bare = without_arguments(qualifier);
			for (std::string::size_type at = qualifier.size();
			     (at = qualifier.rfind("::", at - 1)) != std::string::npos;
			     )
			{
				names_.reach(qualifier.substr(0, at + 2));
				if (at == 0)
				{
					break;
				}
			}
			for (std::string::size_type at = bare.size();
			     bare != qualifier &&
			     (at = bare.rfind("::", at - 1)) != std::string::npos;
			     )
			{
				names_.reach(bare.substr(0, at + 2));
				if (at == 0)
				{
					break;
				}
			}
			++names_.version_;
		}

		~Reached()
		{
			if (opened_)
			{
				names_.scopes_.pop_back();
				++names_.version_;
			}
		}

	private:
		Reached(const Reached&);
		Reached& operator=(const Reached&);
		DeclaredNames& names_;
		bool opened_;
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

	// The prefix a class was remembered under, found by trying the prefixes in
	// force outward exactly as a name written behind it would be.  The
	// spelling itself for a name no class-head declared.
	std::string canonical(const std::string& reached) const;
	// 10.2p2: the direct bases of the class a prefix was remembered under, or
	// null for one no base-clause named.  The bases were resolved where the
	// base-clause was read, so a step along the chain is one probe.
	const std::vector<std::string>* bases_of(const std::string& canonical) const;
	// 10.2p2: `name` looked up behind `reached` and, where that finds nothing,
	// behind each base `reached` reaches - the chain walked once per name that
	// misses and bounded by the classes there are, so a base-clause written
	// through a cycle is answered rather than followed.
	NameKind reached_through(const std::string& reached,
	                         const std::string& name) const;
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
	// Every name a declaration of this translation unit introduced, whatever
	// region it was written in.  A name that is in no declaration at all is in
	// no region either, so the prefixes 7.3.4p2 and 10.2p2 reach are searched
	// only for a name some declaration wrote - which is what keeps a miss one
	// probe rather than a walk of every base and every nominated namespace.
	// 6.8p1: every spelling this unit has declared, and the kinds the
	// declarations of it gave it, one bit each.  It is not a scope: a name is
	// in it from the declaration that wrote it to the end of the unit, because
	// what it answers is what a declaration of the spelling could have made the
	// name anywhere - which is a question about the unit and not about a
	// region.
	std::unordered_map<std::string, unsigned> declared_;
	// The prefix each class-head gave its members, which is what a base written
	// against the regions around a derived class is resolved to.
	std::unordered_set<std::string> classes_;
	// 10.2p2: the prefixes each class reaches besides its own, keyed by the
	// prefix that class's own name gives its members.  A class with no base is
	// in no entry, so a program with no inheritance pays one probe per region
	// a name is written in.
	std::unordered_map<std::string, std::vector<std::string> > inherited_;
	std::string prefix_;
	unsigned long version_;
};
