#include "sema_derivation.h"

#include <cstddef>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_pack.h"

// 10p1, read as the tree it is.
//
// Every walk below has the same shape - descend the base-specifier lists from
// one class looking for another - and each stops as soon as it has its answer,
// because 10.1p3's repeated base is refused and so a class stands below another
// at most once.  That is what makes each of these one visit per class in the
// derivation rather than one per path into it, and what makes "the offset of the
// base subobject" a number rather than a set of them.

Derivation::Derivation(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
	, blocked_(nullptr)
{}

// 10p1: the base-clause of a class definition, which says what every object of
// the class holds a subobject of.  Each base is recorded on the class and on
// the region it declares, in the order the base-specifier-list wrote them, and
// every later question - 9.2p13 layout, 10.2 lookup, 11.2 access, 12.6.2
// construction, 12.4 destruction, 4.10p3 conversion - reads that one list
// rather than the syntax it was read from.
void Derivation::read_base_clause(const AstNode& node, SemaEntity& entity,
                                  Scope& scope, const SemaContext& ctx,
                                  const std::string& header)
{
	if (!analyzer_.lowering() && analyzer_.checking_ == 0)
	{
		// The PA12 dump has no line for a base class, so a class with one would
		// be written as the class it would have been without the base-clause,
		// which is a different class.  PA11 only spells the declaration it was
		// given and needs none of this.
		if (analyzer_.semantics())
		{
			throw std::runtime_error(header + " has a base class, which PA12 "
			                         "does not describe");
		}
		return;
	}
	// 14.6p8 and 10.2p2: a definition being read where it stands still reaches
	// what its base declares, because an unqualified name its body writes is
	// looked up in the base as well - so the base-clause is read for the
	// pattern too, in the dialect that describes only what a declaration says.
	//
	// 14.6.2p3 is one fact of the whole clause rather than of each specifier:
	// a class one of whose bases an argument list still has to say is one whose
	// definition 3.4.1 answers without the chain, because a name it writes has
	// to mean the same thing every argument list reads it under.
	bool dependent = false;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		read_base_specifier(*node.children[index], entity, scope, ctx, header,
		                    dependent);
	}
	scope.dependent_base = dependent;
	require_distinct(entity, header);
}

// 10p1: one base-specifier of that list, which names one class - or, where
// 14.5.3p4 wrote it as a pattern, one class per element of the run its packs
// are bound to.
void Derivation::read_base_specifier(const AstNode& specifier,
                                     SemaEntity& entity, Scope& scope,
                                     const SemaContext& ctx,
                                     const std::string& header,
                                     bool& dependent)
{
	// 11.2p2: a base-specifier with no access-specifier is `private` for a class
	// and `public` for a struct, which is what the class-key already decided for
	// the members.
	unsigned char access = analyzer_.types_.class_tag(entity.type) == ClassTag::Class
		? kPrivateAccess
		: kPublicAccess;
	std::string named;
	bool expanded = false;
	for (std::size_t index = 0; index < specifier.children.size(); ++index)
	{
		const AstNode& part = *specifier.children[index];
		if (part.kind == AstKind::ParameterPack)
		{
			expanded = true;
			continue;
		}
		if (part.kind == AstKind::Virtual)
		{
			if (analyzer_.checking_ > 0)
			{
				// What this milestone does not lay out is refused where a
				// specialization of it is made, not where a definition no
				// instantiation ever reads stands.
				return;
			}
			throw std::runtime_error(header + " has a virtual base class, which "
			                         "this milestone does not lay out");
		}
		if (part.kind == AstKind::AccessSpecifier)
		{
			access = part.token == KW_PRIVATE
				? kPrivateAccess
				: (part.token == KW_PROTECTED ? kProtectedAccess
				                              : kPublicAccess);
			continue;
		}
		if (part.kind == AstKind::BaseName)
		{
			named = part.text;
		}
	}
	// 10p1: the base-specifier names a class, which a typedef-name may stand
	// for - so what it names is the class the type belongs to rather than the
	// declaration the name was bound to.  14.5.3p4 writes it as a pattern
	// instead, and the class derives from one base per element of the run its
	// packs are bound to - each of them a direct base of its own, laid out and
	// initialized in the place the run put it.
	if (expanded)
	{
		std::vector<TypeId> run;
		PackReading(analyzer_).expand(named, ctx, kNoType, run);
		if (run.size() == 1 && analyzer_.types_.is_pack_expansion(run[0]))
		{
			if (analyzer_.checking_ > 0)
			{
				analyzer_.note_dependent_base(specifier);
				dependent = true;
				return;
			}
			throw std::runtime_error(named + " is expanded as a base class and "
			                                 "names an unsettled parameter pack");
		}
		for (std::size_t index = 0; index < run.size(); ++index)
		{
			// 14.5.3p4 over a run of none adds no base at all, which is a class
			// deriving from nothing.
			settle_base(run[index], nullptr, specifier, entity, scope, header,
			            access, dependent);
		}
		return;
	}
	const SemaEntity& found_name = analyzer_.require(
		analyzer_.resolve(named, ctx, LookupKind::Type), named);
	settle_base(found_name.type, &found_name, specifier, entity, scope, header,
	            access, dependent);
}

// 10p1: the one class a base-specifier came to, recorded on the derived class
// and on the region it declares.
void Derivation::settle_base(TypeId named_type, const SemaEntity* found_name,
                             const AstNode& specifier, SemaEntity& entity,
                             Scope& scope, const std::string& header,
                             unsigned char access, bool& dependent)
{
	// 10p1 and 14.7.1p1: a base class shall be a complete class type, which is
	// what asks a specialization the base-specifier named for its definition.
	analyzer_.require_settled_type(named_type);
	if (analyzer_.checking_ > 0 && analyzer_.types_.is_dependent(analyzer_.types_.strip_cv(named_type)))
	{
		// 14.6.2p3: an unqualified name written in the definition is not looked
		// up in a base class that depends on a template parameter, because
		// which class that is only an argument list says.  So the reading
		// leaves the base off the chain, and the specialization the arguments
		// make is read against the base they name - with the same clause left
		// off *its* chain, because 3.4.1 answers a name the definition wrote
		// where the definition stands however many argument lists read it.
		analyzer_.note_dependent_base(specifier);
		dependent = true;
		return;
	}
	// 14.6.2p3: whether *this* specifier is the one an argument list settles,
	// which is what says the search of a member's unqualified name looks in the
	// base it named.  The fact was recorded where the pattern was read, so the
	// specialization an argument list makes answers it the same way.
	const bool wrote_dependent = analyzer_.wrote_dependent_base(specifier);
	dependent = dependent || wrote_dependent;
	const std::string named = analyzer_.types_.description(named_type);
	if ((found_name != nullptr && !names_a_type(*found_name)) ||
	    !analyzer_.types_.is_class(analyzer_.types_.strip_cv(named_type)))
	{
		throw std::runtime_error(named + " is named as a base class and is not "
		                                 "a class");
	}
	SemaEntity* const base = analyzer_.model_.type_owner(analyzer_.types_.strip_cv(named_type));
	if (base == nullptr || !base->defined || base->scope == nullptr)
	{
		// 10p1: a base class shall be a complete class type, because the
		// derived class holds a subobject of it.
		throw std::runtime_error(named + " is named as a base class and is an "
		                                 "incomplete class");
	}
	if (base == &entity)
	{
		throw std::runtime_error(header + " is named as its own base class");
	}
	if (analyzer_.types_.class_tag(base->type) == ClassTag::Union ||
	    analyzer_.types_.class_tag(entity.type) == ClassTag::Union)
	{
		// 9.5p3: a union shall not have base classes and shall not be used as a
		// base class.
		throw std::runtime_error(header + " derives from or is a union, which "
		                                  "9.5p3 does not allow");
	}
	BaseClass link;
	link.entity = base;
	link.offset = 0;
	link.access = access;
	entity.bases.push_back(link);
	scope.bases.push_back(base->scope);
	if (!wrote_dependent)
	{
		scope.open_bases.push_back(base->scope);
	}
}

// 10.1p3 lets one class stand twice below another, as two subobjects a name of
// either is ambiguous between.  Nothing here tells two subobjects of one class
// apart - a conversion names the class and carries one offset - so a derivation
// that would hold two is refused rather than laid out as one.
//
// The walk stops at the first class it arrives at twice, so it is one visit per
// class as well.  A class with fewer than two direct bases can hold no two of
// anything the classes it derives from did not already hold, and each of those
// was completed under this same walk - so nearly every class pays one test.
//
// What a class was reached by is a number on the class rather than an entry in
// a table this walk builds.  The walk is made once per class completed and
// covers the whole derivation below it, so a program adding a base at every
// level makes one per level - and a table of its own per walk is what those
// levels were paying for, in hashing and in an allocation each.
void Derivation::require_distinct(const SemaEntity& entity,
                                  const std::string& header)
{
	if (entity.bases.size() < 2)
	{
		return;
	}
	const unsigned long long reach = analyzer_.model_.next_reach();
	std::vector<const SemaEntity*> pending(1, &entity);
	while (!pending.empty())
	{
		const SemaEntity& at = *pending.back();
		pending.pop_back();
		for (std::size_t index = 0; index < at.bases.size(); ++index)
		{
			SemaEntity* const base = at.bases[index].entity;
			if (base->reached_at == reach)
			{
				throw std::runtime_error(
					header + " holds more than one subobject of " +
					analyzer_.types_.description(base->type) +
					", which this milestone does not lay out");
			}
			base->reached_at = reach;
			pending.push_back(base);
		}
	}
}

// 10p1 and 9.2p13: where the base class subobject `base` stands inside an
// object of class type `from`.
//
// Each class in the derivation already holds where it put each of its own
// direct bases, so the walk is one addition per level of the path the
// conversion names, and it is the path the caller already found the base
// through.  Zero wherever no class on it added a vpointer and no base-specifier
// but the first placed a subobject, which is every hierarchy of the PA17 subset.
unsigned long long Derivation::subobject_offset(TypeId from,
                                                const SemaEntity& base)
{
	const SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(from));
	unsigned long long offset = 0;
	if (owner != nullptr)
	{
		path(*owner, base, offset, nullptr);
	}
	return offset;
}

bool derives_from(const Scope& derived, const Scope& base)
{
	if (&derived == &base)
	{
		return true;
	}
	for (std::size_t index = 0; index < derived.bases.size(); ++index)
	{
		if (derives_from(*derived.bases[index], base))
		{
			return true;
		}
	}
	return false;
}

SemaEntity* Derivation::base_in(TypeId derived, TypeId base)
{
	TypeTable& types = analyzer_.types_;
	const TypeId wanted = types.strip_cv(base);
	if (!types.is_class(wanted) || !types.is_class(types.strip_cv(derived)))
	{
		return nullptr;
	}
	SemaEntity* owner = analyzer_.model_.type_owner(types.strip_cv(derived));
	if (owner == nullptr || types.strip_cv(owner->type) == wanted)
	{
		// 10p1: a class is no base of itself, so the two naming one class is no
		// derivation and asks for no definition of it.
		return nullptr;
	}
	// 4.10p3 and 14.7.1p1: which classes an object of `derived` holds a
	// subobject of is a fact of its definition, so a specialization no use has
	// asked for yet is asked for here - a conversion to a base is one of
	// 3.9p5's contexts requiring a completely-defined type.  A program with no
	// specialization still owing a definition pays one integer test.
	analyzer_.require_complete_type(derived);
	owner = analyzer_.model_.type_owner(types.strip_cv(derived));
	return owner == nullptr ? nullptr : below(*owner, wanted);
}

SemaEntity* Derivation::below(const SemaEntity& at, TypeId wanted) const
{
	for (std::size_t index = 0; index < at.bases.size(); ++index)
	{
		SemaEntity* const base = at.bases[index].entity;
		if (analyzer_.types_.strip_cv(base->type) == wanted)
		{
			return base;
		}
		SemaEntity* const deeper = below(*base, wanted);
		if (deeper != nullptr)
		{
			return deeper;
		}
	}
	return nullptr;
}

void Derivation::require_access(const SemaEntity* derived,
                                const SemaEntity& base)
{
	if (accessible(derived, base))
	{
		return;
	}
	throw std::runtime_error(
		"a conversion to a base class of " +
		analyzer_.types_.description(blocked_ != nullptr ? blocked_->type
		                                                 : derived->type) +
		" is written where the access its base-specifier gave it does not "
		"reach");
}

bool Derivation::accessible(const SemaEntity* derived, const SemaEntity& base)
{
	bool reaches = true;
	unsigned long long offset = 0;
	if (derived != nullptr)
	{
		path(*derived, base, offset, &reaches);
	}
	return reaches;
}

bool Derivation::path(const SemaEntity& at, const SemaEntity& base,
                      unsigned long long& offset, bool* reaches)
{
	if (&at == &base)
	{
		offset = 0;
		return true;
	}
	for (std::size_t index = 0; index < at.bases.size(); ++index)
	{
		const BaseClass& link = at.bases[index];
		unsigned long long below_here = 0;
		if (!path(*link.entity, base, below_here, reaches))
		{
			continue;
		}
		offset = link.offset + below_here;
		if (reaches != nullptr && *reaches &&
		    !link_accessible(at, link.access))
		{
			*reaches = false;
			blocked_ = &at;
		}
		return true;
	}
	return false;
}

bool Derivation::link_accessible(const SemaEntity& derived,
                                 unsigned char access)
{
	if (access == kPublicAccess || derived.scope == nullptr)
	{
		return true;
	}
	const Scope* const from =
		analyzer_.naming_ != nullptr ? analyzer_.naming_ : analyzer_.reading_;
	const bool friends = analyzer_.model_.has_friends();
	for (const Scope* at = from; at != nullptr; at = at->parent)
	{
		if (at == derived.scope)
		{
			return true;
		}
		if (friends && analyzer_.befriended(*derived.scope, *at))
		{
			// 11.2p1 and 11.3p1: a friend of the derived class reaches what
			// the class itself reaches, which is the base its base-specifier
			// named however it named it.
			return true;
		}
		if (access != kProtectedAccess || at->kind != ScopeKind::Class)
		{
			continue;
		}
		// 11.2p1 again: a class derived from the one that named the base
		// reaches a protected base-specifier of it, which is one walk of that
		// class's own derivation.
		for (std::size_t index = 0; index < at->bases.size(); ++index)
		{
			if (derives_from(*at->bases[index], *derived.scope))
			{
				return true;
			}
		}
	}
	return false;
}
