#pragma once

#include <string>
#include <vector>

#include "type_model.h"

class SemaAnalyzer;
class Scope;
struct AstNode;
struct SemaContext;
struct SemaEntity;

// 10p1: the base class subobjects an object holds, and the four questions every
// other reader asks of them.
//
// A base-specifier-list of more than one entry makes a derivation a tree rather
// than a chain, so "where does this base subobject begin", "is this class one of
// the bases", "which class in the derivation is this type" and "does 11.2's
// access reach through every base-specifier on the way" stop being one walk of a
// chain and become one walk of that tree.  They are kept together because each
// is the same walk answering a different question, and apart from the class
// because 10.1p3 is what makes the walk well defined at all: a repeated base is
// refused where the class is completed, so every class below one stands there
// once, the path to a base subobject is the one path there is, and each walk
// costs one visit per class rather than one per path into it.

// 10p1: whether one class region is another or holds a subobject of it.  It
// needs nothing but the regions, so every reader asks it directly - including
// the ones that answer 11.2's access, which are asked of a class being read.
bool derives_from(const Scope& derived, const Scope& base);

class Derivation
{
public:
	explicit Derivation(SemaAnalyzer& analyzer);

	// 10p1: the base-clause of a class definition, read before its members
	// because they are read against what its bases declare, and one of its
	// base-specifiers - which 14.5.3p4 may write as a pattern standing for a
	// run of them.
	void read_base_clause(const AstNode& node, SemaEntity& entity, Scope& scope,
	                      const SemaContext& ctx, const std::string& header);

	// 10.1p3: whether an object of `entity` would hold two subobjects of one
	// class, which this milestone refuses rather than lays out.
	void require_distinct(const SemaEntity& entity, const std::string& header);

	// 10p1 and 9.2p13: where the base class subobject `base` begins inside an
	// object of the class type `from`, which is the sum of the places each
	// class on the path gave the base below it.
	unsigned long long subobject_offset(TypeId from, const SemaEntity& base);

	// 10p1: the class in the derivation of `derived` that `base` names, or null
	// where `derived` holds no subobject of it.  A `derived` that already *is*
	// `base` is no derivation at all, so that answers null too.
	SemaEntity* base_in(TypeId derived, TypeId base);

	// The same walk asked for the *steps* rather than for the byte: which
	// base-specifier of each class on the path names the one below it, appended
	// to `out` from `from` downwards.  A constant expression indexes an object's
	// subobjects rather than its storage, and 12.6.2p10 puts the base class
	// subobjects first - so a step is the base's own index and the path is what
	// reaches the subobject there.  False where `from` holds no subobject of
	// `base`, which leaves `out` as it was; an empty path answers true, because
	// a class is the subobject of itself no step reaches.
	bool subobject_path(TypeId from, const SemaEntity& base,
	                    std::vector<unsigned long long>& out);

	// 11.2p4 and 11.2p5: a conversion to a base class passes through every
	// base-specifier between the two classes, so each one is asked in turn and
	// one link the access does not reach makes the whole conversion ill formed.
	void require_access(const SemaEntity* derived, const SemaEntity& base);

	// The same question asked rather than refused, because 10.3p7's covariant
	// return wants the answer and not the diagnosis.
	bool accessible(const SemaEntity* derived, const SemaEntity& base);

private:
	void read_base_specifier(const AstNode& specifier, SemaEntity& entity,
	                         Scope& scope, const SemaContext& ctx,
	                         const std::string& header, bool& dependent);
	// 10p1: the one class a base-specifier came to, recorded on the derived
	// class and on the region it declares.
	void settle_base(TypeId named_type, const SemaEntity* found_name,
	                 const AstNode& specifier, SemaEntity& entity, Scope& scope,
	                 const std::string& header, unsigned char access,
	                 bool& dependent);

	Derivation(const Derivation&);
	Derivation& operator=(const Derivation&);

	// The one walk: the path from `at` down to `base`, leaving the byte the
	// subobject begins at in `offset`.  Where `reaches` is given, it is left
	// false for a path 11.2 does not reach through and `blocked_` names the
	// class whose base-specifier stopped it; where `steps` is given, it takes
	// the base-specifier index of each class on the path.  False where `at`
	// holds no subobject of `base`, which leaves all three untouched.
	bool path(const SemaEntity& at, const SemaEntity& base,
	          unsigned long long& offset, bool* reaches,
	          std::vector<unsigned long long>* steps = nullptr);
	// 11.2p1: the access one base-specifier gave the base it named, asked of
	// where the conversion through it is written.  A public base reaches
	// everywhere, a protected one reaches the classes derived from the one that
	// named it, and a private one reaches that class alone.
	bool link_accessible(const SemaEntity& derived, unsigned char access);
	SemaEntity* below(const SemaEntity& at, TypeId wanted) const;

	SemaAnalyzer& analyzer_;
	// Scratch of one access walk: the class whose base-specifier the access did
	// not reach through, which is what the refusal names.
	const SemaEntity* blocked_;
};
