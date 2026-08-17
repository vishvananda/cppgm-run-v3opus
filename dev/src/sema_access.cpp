#include "sema_access.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "sema_analyzer.h"
#include "sema_derivation.h"
#include "sema_name.h"

// 11's two halves: which contexts reach what a class declared, and what a class
// granted.  See `sema_access.h` for why they are one reader.

Access::Access(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
	, context_point_(nullptr)
	, context_walked_(false)
{}

// 11.2p1's second sentence: whether one of the classes the access occurs in
// derives from the class that wrote the base-specifier.
//
// `base_path` asks this of every link on its way down and the point the name
// was written at is the same at each of them, so what the classes around that
// point derive from is one walk of their own - made where the first link needs
// it and read by the rest.  The walk that answered it per link was the whole
// derivation read again once per level of the levels below it, which put 92 %
// of a depth-512 run in `derives_from`.
bool Access::context_derives(const Scope* from, const Scope& base) const
{
	if (!context_walked_ || context_point_ != from)
	{
		context_bases_.clear();
		context_point_ = from;
		context_walked_ = true;
		for (const Scope* at = from; at != nullptr; at = at->parent)
		{
			if (at->kind != ScopeKind::Class)
			{
				continue;
			}
			for (std::size_t index = 0; index < at->bases.size(); ++index)
			{
				collect_bases(*at->bases[index]);
			}
		}
	}
	return context_bases_.find(&base) != context_bases_.end();
}

// One visit per class of a derivation, which is what makes the walk above cost
// the classes it reaches rather than the paths that reach them.
void Access::collect_bases(const Scope& at) const
{
	if (!context_bases_.insert(&at).second)
	{
		return;
	}
	for (std::size_t index = 0; index < at.bases.size(); ++index)
	{
		collect_bases(*at.bases[index]);
	}
}

// 11.3p1: the class a friend declaration written in it grants access to, which
// is the innermost class around the declaration.
SemaEntity* Access::granting_class(const SemaContext& ctx) const
{
	for (Scope* at = ctx.scope; at != nullptr; at = at->parent)
	{
		if (at->kind == ScopeKind::Class)
		{
			return at->owner;
		}
	}
	return nullptr;
}

// 11.3p2: `friend C;` and `friend class C;` name a class and declare nothing.
// The specifiers already found or declared it, so what is left is the grant.
void Access::grant_class_friendship(const SemaContext& ctx,
                                    const DeclSpecifiers& specifiers)
{
	SemaEntity* const granting = granting_class(ctx);
	SemaEntity* friendly = specifiers.introduced;
	if (friendly == nullptr && specifiers.has_type_name)
	{
		const TypeId named = analyzer_.types_.strip_cv(specifiers.type_name);
		if (analyzer_.types_.is_dependent(named))
		{
			// 14.6p8 with 11.3p2: which class `friend typename C::self;` names,
			// an argument list is what says.  The reading of the definition has
			// none, so it records no grant at all - 14.7.1p1 reads the same
			// declaration again where the arguments stand, and the type is a
			// class there.  Refusing here would refuse a class this reading has
			// not been given rather than one the program did not write.
			return;
		}
		friendly = analyzer_.model_.type_owner(named);
	}
	if (granting == nullptr)
	{
		throw std::runtime_error("a friend declaration with no declarator is "
		                         "written outside a class definition");
	}
	if (friendly == nullptr || friendly->kind != SemaKind::Class)
	{
		// 11.3p3: `friend int;` is a friend declaration whose type-specifier
		// names no class, and the standard's own answer to it is that the
		// declaration is ignored - so there is a grant to make or there is
		// nothing, and never an error.
		return;
	}
	analyzer_.model_.befriend(*granting, *friendly);
}

// 14.5.4p1: a friend declaration whose declarator-id is a template-id names a
// specialization of a function template and declares nothing of its own.  The
// template is what some region already declared - 11.3p6's namespace is where
// this declaration would have made one - so all that is left is 11.3p1's grant.
//
// The grant is recorded between the two *templates*, which is the pair
// `SemaModel::befriended` asks about: a grant written inside a class template's
// body was made by a template, and both tiers put the declaration a
// specialization came from on it, so one entry answers `str<C>` befriending
// `operator+<C>` for every argument list either of them is made with.  What
// 14.8.2 would deduce from the declaration for a list the id left empty is
// PA23's, and is not what says whether the grant was made.
void Access::grant_template_friendship(const std::string& component,
                                       const SemaContext& target,
                                       SemaEntity& granting)
{
	const TemplateId id(component);
	SemaEntity* const named =
		analyzer_.model_.lookup(*target.scope, id.name(), LookupKind::Any);
	bool any = false;
	for (SemaEntity* at = named; at != nullptr; at = at->next)
	{
		if (at->kind != SemaKind::Function || at->template_parameters == nullptr)
		{
			continue;
		}
		analyzer_.model_.befriend(granting, *at);
		any = true;
	}
	if (!any)
	{
		// 11.3p10: a friend declaration that names a specialization names one
		// the region already declares a template of, and declares none itself.
		throw std::runtime_error(component + " is declared a friend and names "
		                         "no function template that region declares");
	}
}

// 11.3p6 and 7.3.1.2p3: a friend declaration declares its function in the
// innermost enclosing namespace rather than in the class it is written in, and
// - where the declarator-id is unqualified - binds its name nowhere.  A
// qualified declarator-id names a function that region already declares, which
// 11.3p10 is the only thing it may name.
//
// `granting` is taken by the caller before the declarator is read, because
// 3.4.1p8 moves the head a qualified declarator-id stands under *inside* the
// region that name reaches - so by the time the declarator has been read the
// class the declaration was written in is on none of the regions around it.
SemaEntity* Access::friend_target(const SemaContext& ctx,
                                  const QualifiedName& spelled,
                                  SemaContext& target, SemaEntity* granting)
{
	if (granting == nullptr)
	{
		throw std::runtime_error("a friend declaration is written outside a "
		                         "class definition");
	}
	if (!spelled.qualified())
	{
		target.scope = &SemaAnalyzer::friend_namespace(*ctx.scope);
		target.dump = target.scope->dump;
	}
	return granting;
}

// 11.3p1: the class `granting` gave `friendly` the reach its own members have.
// A friend is a function or a class, and both are entities a region a name is
// read in is owned by, so the question the access check asks of each region
// around it is the same one.
bool Access::befriended(const Scope& granting, const Scope& from) const
{
	return granting.owner != nullptr && from.owner != nullptr &&
		analyzer_.model_.befriended(*granting.owner, *from.owner);
}

// 11.2p5: whether any class between the one the name was written on and the one
// that declared the member befriends `from`.
//
// The classes between them are the one path there is - 10.1p3's repeated base
// being refused is what says so - so this is one step per level of it, and the
// walk that finds the path is the same one every other reader of a base
// subobject makes.
bool Access::befriends_between(const Scope& at, const Scope& declaring,
                               const Scope& from) const
{
	bool grants = false;
	friend_path(at, declaring, from, grants);
	return grants;
}

// The walk that finds that path, asking each class on it as it comes back up -
// which is the one `base_path` makes for the same path.  Choosing the branch by
// asking `derives_from` of each base is the derivation read again once per
// level, where one visit per class costs the path itself.
bool Access::friend_path(const Scope& at, const Scope& declaring,
                         const Scope& from, bool& grants) const
{
	if (&at == &declaring)
	{
		return true;
	}
	for (std::size_t index = 0; index < at.bases.size(); ++index)
	{
		if (!friend_path(*at.bases[index], declaring, from, grants))
		{
			continue;
		}
		if (befriended(at, from))
		{
			grants = true;
		}
		return true;
	}
	return false;
}

// 11.2p1: what a base-specifier's own access-specifier gave the base class.
// The question is the member one asked of a class rather than of a member: a
// member or friend of the class that wrote the specifier reaches it however it
// was written, and 11.2p1's second sentence gives a class derived from that one
// the reach of a protected specifier.
bool Access::base_accessible(const Scope& derived, unsigned char access,
                             const Scope* from) const
{
	if (access == kPublicAccess)
	{
		return true;
	}
	const bool friends = analyzer_.model_.has_friends();
	for (const Scope* at = from; at != nullptr; at = at->parent)
	{
		if (at == &derived)
		{
			return true;
		}
		if (friends && befriended(derived, *at))
		{
			// 11.2p1 and 11.3p1: a friend of the derived class reaches what the
			// class itself reaches, which is the base its base-specifier named
			// however it named it.
			return true;
		}
	}
	// 11.2p1 again: a class derived from the one that named the base reaches a
	// protected base-specifier of it.  The classes the access occurs in are the
	// ones the loop above walked, so this is one question about the point
	// rather than one per class of it - and one walk of what that point
	// derives from, however many links of a derivation ask it.
	return access == kProtectedAccess && context_derives(from, derived);
}

// 11.2p4's last bullet: a member is accessible when named in a class that did
// not declare it only where some base class of it is accessible at the same
// point *and* the member is accessible when named in that base.  10.1p3 leaves
// one path between the two classes - a derivation that would hold two
// subobjects of one class is refused where it is read - so this is one step per
// level of it, over the `Scope::bases` links the base-specifiers wrote and the
// access `SemaEntity::bases` holds beside each of them.
bool Access::base_path_accessible(const Scope& naming,
                                  const Scope& declaring,
                                  const Scope* from) const
{
	bool reaches = true;
	base_path(naming, declaring, from, reaches);
	return reaches;
}

// The walk that finds that path and asks each link on the way down, which is
// the one `Derivation::path` already makes for the offset: one visit per class
// of the derivation rather than a reachability question asked again at every
// level, so a member inherited from d classes up costs d steps and not d².
bool Access::base_path(const Scope& naming, const Scope& declaring,
                       const Scope* from, bool& reaches) const
{
	if (&naming == &declaring)
	{
		return true;
	}
	for (std::size_t index = 0; index < naming.bases.size(); ++index)
	{
		if (!base_path(*naming.bases[index], declaring, from, reaches))
		{
			continue;
		}
		// 10p1: `SemaEntity::bases` holds the access each base-specifier wrote
		// and `Scope::bases` the region it named, pushed together where the
		// specifier is read, so the two are one list indexed alike.
		if (reaches && naming.owner != nullptr &&
		    index < naming.owner->bases.size() &&
		    !base_accessible(naming, naming.owner->bases[index].access, from))
		{
			reaches = false;
		}
		return true;
	}
	return false;
}

bool Access::accessible(const SemaEntity& member, const Scope* from,
                        const Scope* naming_class) const
{
	if (member.region == nullptr)
	{
		return true;
	}
	// 11.2p4: what the member's own access-specifier gave it is the answer only
	// where the class the name was written on is the one that declared it.
	// Written on a class further down, the member is reached through the bases
	// between the two, and each of those is a base-specifier whose own access
	// has to reach the point the name was written at.
	if (naming_class != nullptr && naming_class != member.region &&
	    !base_path_accessible(*naming_class, *member.region, from))
	{
		return false;
	}
	if (member.access == kPublicAccess)
	{
		return true;
	}
	const bool friends = analyzer_.model_.has_friends();
	for (const Scope* at = from; at != nullptr; at = at->parent)
	{
		if (at == member.region)
		{
			return true;
		}
		// 11.3p1 and 11.3p2: a friend of the class reaches every member of it,
		// and so does a member of a befriended class, which is the same
		// question asked of the region that member is read in.
		if (friends && befriended(*member.region, *at))
		{
			return true;
		}
		if (member.access != kProtectedAccess)
		{
			continue;
		}
		// 11.4p1: a protected member is also named from a member of a class
		// derived from the one that declared it, and 11.7 gives that reach to a
		// class nested in such a class as well.
		if (at->kind == ScopeKind::Class && at != member.region &&
		    derives_from(*at, *member.region))
		{
			return true;
		}
		// 11.2p5: where the member is named on an object, the access is also
		// granted by a base class of that object's class which itself grants
		// it - so the classes between the one the name was written on and the
		// one that declared the member are each asked in turn.  A private
		// member is not one of those: no class but its own ever reaches it.
		if (friends && naming_class != nullptr &&
		    befriends_between(*naming_class, *member.region, *at))
		{
			return true;
		}
	}
	return false;
}

// 12.4p11: an object's lifetime ends in a call of the destructor of its class,
// and a program that names an inaccessible function is ill formed - so the
// access 11 gave the destructor is asked for in the region that declares the
// object, which is where that call is written.  A class type reached through an
// array is the same question about each element.
void Access::require_destruction_access(const SemaEntity& entity,
                                        const Scope* from)
{
	SemaEntity* const destructor =
		analyzer_.class_destructor(analyzer_.types_.element_of(entity.type));
	if (destructor == nullptr || accessible(*destructor, from))
	{
		return;
	}
	throw std::runtime_error("an object of " +
	                         analyzer_.types_.description(entity.type) +
	                         " is declared where the access its class gave the "
	                         "destructor its lifetime ends with does not reach");
}

// 11.4p1: the additional check on a protected non-static member named on an
// object.  Where the access is granted only because the class it occurs in
// derives from the one that declared the member, that class may reach the
// member of its own objects and not of the base's - so the object expression
// has to name a class derived from the one the access occurs in.  A member the
// declaring class itself reaches is not the case 11.4 adds a check to.
void Access::require_protected_object(
	const std::vector<SemaEntity*>& found, const SemaEntity& member,
	const Scope* from, const Scope* object_class)
{
	if (object_class == nullptr || member.region == nullptr)
	{
		return;
	}
	if (found.empty())
	{
		if (member.access != kProtectedAccess || !member.object_member)
		{
			return;
		}
	}
	else
	{
		// 13.3 has not chosen between the declarations of an overloaded name
		// yet, so the question is asked only where every one the lookup reached
		// answers it the same way: a protected non-static member of the one
		// class.  A name that reached a public declaration too is one whose
		// access the choice settles, and the choice is not made here.
		for (std::size_t index = 0; index < found.size(); ++index)
		{
			for (const SemaEntity* at = found[index]; at != nullptr;
			     at = at->next)
			{
				if (at->access != kProtectedAccess || !at->object_member ||
				    at->region != member.region)
				{
					return;
				}
			}
		}
	}
	if (analyzer_.naming_ != nullptr)
	{
		from = analyzer_.naming_;
	}
	for (const Scope* at = from; at != nullptr; at = at->parent)
	{
		if (at == member.region)
		{
			return;
		}
		if (at->kind != ScopeKind::Class || !derives_from(*at, *member.region))
		{
			continue;
		}
		// `at` is the class the access occurs in, and 11.4p1 lets it reach the
		// member of that class and of the classes derived from it alone.
		if (derives_from(*object_class, *at))
		{
			return;
		}
		throw std::runtime_error(member.name + " is a protected member named on "
		                         "an object of a class the one the access is "
		                         "written in does not derive from");
	}
}

void Access::require_access(const SemaEntity& member, const Scope* from,
                            const Scope* naming_class)
{
	if (analyzer_.naming_ != nullptr)
	{
		// 11p6: the entity being declared is what the access is checked for,
		// wherever in its declaration the name stands.
		from = analyzer_.naming_;
	}
	if (!accessible(member, from, naming_class))
	{
		throw std::runtime_error(member.name + " is named where the access its "
		                         "class gave it does not reach");
	}
}

// 11.2 and 14.2: a component written as a template-id names the specialization
// its arguments make, and the access-specifier the program wrote stands over
// the *template* the lookup finds - the specialization is no declaration it
// could have been written on.
void Access::require_component_access(const std::string& component,
                                      const SemaContext& ctx, Scope& in)
{
	const TemplateId id(component);
	if (!id.valid())
	{
		return;
	}
	SemaEntity* const primary =
		analyzer_.model_.lookup_in(in, id.name(), LookupKind::Type);
	if (primary != nullptr)
	{
		require_access(*primary, ctx.scope,
		               in.kind == ScopeKind::Class ? &in : nullptr);
	}
}
