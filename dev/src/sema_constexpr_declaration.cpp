// 7.1.5: the requirements the `constexpr` specifier puts on the declaration
// that wrote it.
//
// Everything else in this layer answers what an expression comes to.  This file
// answers the other question 7.1.5 asks, which no fold can: whether the
// declaration is one the program was allowed to write.  The two are different
// enough that reading one for the other is the whole of a class of silent
// acceptances - a `constexpr` object whose initializer folded to nothing is not
// "an object with no constant value", it is a program 7.1.5p9 refuses, and one
// lowered as a dynamic initialization instead is a translation of a program
// that has none.

#include "sema_constexpr.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_scope.h"

ConstexprRequirement::ConstexprRequirement(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

// 14.6p8 and 7.1.5p6: whether the declaration this requirement would be asked
// of is one a *template* wrote rather than one the program did.
//
// A pattern read where it stands says nothing about the types an argument list
// has yet to settle, and the declaration an instantiation makes from it is the
// same declaration read again with those arguments bound - so 7.1.5p6 leaves a
// specialization that fails these requirements simply not a constexpr function
// rather than an ill-formed program.  The three counts are the three ways a
// reading gets there: reading a pattern for what 14.6p8 settles, making a
// specialization of a function template, and instantiating a class, whose
// members' declarators are read again one by one.
bool ConstexprRequirement::instantiated() const
{
	return analyzer_.checking_ > 0 || analyzer_.instantiating_ != nullptr ||
		analyzer_.instantiating_class_ > 0;
}

// 3.9p10: whether an object of `type` is one a constant expression may build.
//
// The array bullet is read first because 8.3.4p1 puts the cv-qualifiers of an
// array on its elements, so the question about `const T[4]` is the question
// about `T`; a reference is a literal type whatever it binds to, because no
// object of one is built; and every scalar type is one.  A class carries the
// answer its own completion settled, and a type no walk has answered - a
// template parameter, a class a name only declared - answers yes, because a
// requirement refuses a declaration only where the answer is known to be no.
bool ConstexprRequirement::literal_type(TypeId type) const
{
	TypeTable& types = analyzer_.types_;
	TypeId bare = types.strip_cv(type);
	while (types.kind(bare) == TypeKind::Array)
	{
		bare = types.strip_cv(types.target(bare));
	}
	switch (types.kind(bare))
	{
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		return true;

	case TypeKind::Class:
	{
		const SemaEntity* const owner = analyzer_.model_.type_owner(bare);
		return owner == nullptr || owner->literal_class != kLiteralNo;
	}

	case TypeKind::Fundamental:
	case TypeKind::Pointer:
	case TypeKind::MemberPointer:
	case TypeKind::Enum:
		return types.is_scalar(bare);

	default:
		break;
	}
	// 8.3.5p1's function type, 14.5.3p1's pack and 14.3.2p1's value place are
	// none of them types an object is declared with, so nothing here refuses
	// one: whichever declaration wrote it is refused where it stands.
	return true;
}

// The same walk over the narrower set `SemaConstant` holds.  3.9p10 calls a
// pointer a literal type and this build has no constant that is one, so the two
// answers differ exactly there - and the difference is what tells a fold that
// found the program's error from one that ran out of this build's values.
bool ConstexprRequirement::valued_type(TypeId type) const
{
	TypeTable& types = analyzer_.types_;
	TypeId bare = types.strip_cv(type);
	while (types.kind(bare) == TypeKind::Array)
	{
		bare = types.strip_cv(types.target(bare));
	}
	if (types.kind(bare) == TypeKind::Class)
	{
		const SemaEntity* const owner = analyzer_.model_.type_owner(bare);
		return owner != nullptr && owner->valued_class == kLiteralYes;
	}
	// 3.9.1p8's two kinds of arithmetic value, 7.2p5's enumeration and 5.19p2's
	// address are what `SemaConstant` holds.  A pointer to member is the gap
	// that is left: no address here says which member of a class it names.
	return analyzer_.arithmetic_type(bare) != kNoType ||
		types.kind(bare) == TypeKind::Pointer;
}

// 7.1.5p9 asked of one declaration: whether this reading may hold it to a
// constant initializer at all.
//
// Three things say no.  A declaration a template pattern wrote, or one an
// instantiation made from it, is not where the program was written - 14.6p8
// leaves the first saying nothing about the types an argument list has yet to
// settle, and 7.1.5p6 leaves the second simply not constexpr.  An object of
// a type this build holds no constant of is one whose fold says nothing about
// the program: `constexpr const char *text = "ab";` is a valid declaration whose
// value 5.19 has and `SemaConstant` has not.  And a dialect that builds fewer of
// the declarations 5.19 reads through answers fewer folds: `--emit-types`
// collects no conversion functions, so `constexpr int n = c;` over a class with
// one folds under PA12 and PA21 and not there - which is this build's line and
// not the program's, so PA11's reading refuses nothing here.
bool ConstexprRequirement::demanded(TypeId type, const SemaContext& ctx) const
{
	return !instantiated() && analyzer_.semantics() &&
		!(ctx.scope != nullptr && analyzer_.dependent_reading(*ctx.scope)) &&
		valued_type(type);
}

// 12.1p5 with 7.1.5p4: whether the default constructor the standard defines for
// the class `scope` declares is a constexpr constructor.  It is exactly where a
// written `constexpr X() {}` would satisfy 7.1.5p4 - every base subobject and
// every non-static data member initialized, by a brace-or-equal-initializer or
// by a constexpr constructor of its own.
//
// `holds` says whether 7.1.5p4's "every non-static data member shall be
// initialized" is one of the bullets asked.  With it, the answer is 12.1p5's
// own and says whether *running* that constructor leaves a constant - which is
// what the fold gates on.  Without it, the answer is the one 3.9p10's third
// bullet reads: a class whose members some later initialization will fill is
// still a class that has a constexpr constructor, which is how both oracles
// answer `struct D : B { int x; };` and how they build `constexpr D d = {};`.
//
// It is the same walk 12.1p5's triviality and 15.4p14's specification each
// make, asked of a third fact of the same members, and it reads the answer each
// subobject's class already carries rather than descending into one.
bool ConstexprRequirement::constexpr_default_construction(Scope& scope,
                                                          bool holds) const
{
	for (std::size_t index = 0;
	     scope.owner != nullptr && index < scope.owner->bases.size(); ++index)
	{
		const SemaEntity* const base = scope.owner->bases[index].entity;
		if (!subobject_default_construction(base->type, *base, holds))
		{
			return false;
		}
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		if (member.default_initializer)
		{
			// 12.6.2p8: the member is initialized by what its own declaration
			// wrote, wherever a mem-initializer does not name it.
			continue;
		}
		const TypeId element =
			analyzer_.types_.strip_cv(analyzer_.types_.element_of(member.type));
		const SemaEntity* const owner =
			analyzer_.types_.is_class(element)
				? analyzer_.model_.type_owner(element)
				: nullptr;
		if (owner == nullptr)
		{
			// 8.5p6: default-initialization of a member of scalar type performs
			// no initialization at all, which leaves it holding no value 5.19
			// may read - 7.1.5p4's sentence, and one 3.9p10 has none about.
			if (holds)
			{
				return false;
			}
			continue;
		}
		if (!subobject_default_construction(element, *owner, holds))
		{
			return false;
		}
	}
	return true;
}

// One base or one member of class type, asked the question above.
//
// With `holds`, the constructor that runs shall be a constexpr one this reading
// walks, which is the answer its own class settled.  Without it, 3.9p10's
// third bullet asks only that the subobject's class *have* the constructor and
// be a literal type itself - which is 3.9p10's fourth bullet asked one level
// down, and which is how both oracles read a base whose own members some later
// initialization fills.
bool ConstexprRequirement::subobject_default_construction(TypeId type,
                                                          const SemaEntity& owner,
                                                          bool holds) const
{
	if (holds)
	{
		return owner.constructor != nullptr &&
			owner.constructor->constexpr_function &&
			analyzer_.types_.parameters(owner.constructor->type).size() == 1;
	}
	const SemaEntity* const built = analyzer_.default_constructor(type);
	return owner.literal_class == kLiteralYes && built != nullptr &&
		!built->deleted;
}

// 3.9p10 and 12.1p5 over one complete class, settled where 9.2p2 closes its
// specifier.
//
// The order inside is the standard's: 12.1p5 first, because whether the default
// constructor the standard defines is constexpr is what 3.9p10's third bullet
// then reads about a class that declared none of its own.
void ConstexprRequirement::settle_class(SemaEntity& entity, Scope& scope) const
{
	for (SemaEntity* at = entity.constructor; at != nullptr; at = at->next)
	{
		if (at->defaulted && !at->constexpr_function &&
		    analyzer_.types_.parameters(at->type).size() == 1 &&
		    constexpr_default_construction(scope, true))
		{
			// 12.1p5: the constructor is one the standard defines, and what it
			// does is what a program writing `constexpr` on it would have got -
			// so it *is* a constexpr constructor, whether or not any
			// declaration said so.
			at->constexpr_function = true;
		}
	}
	entity.literal_class = kLiteralNo;
	entity.valued_class = kLiteralNo;
	if (entity.destructor != nullptr && !entity.destructor->trivial)
	{
		// 3.9p10's first bullet: a class whose destructor runs anything holds
		// no object a constant expression may end.
		return;
	}
	bool constructs = entity.aggregate;
	for (SemaEntity* at = entity.constructor; !constructs && at != nullptr;
	     at = at->next)
	{
		// 3.9p10's third bullet: at least one constexpr constructor that is not
		// a copy or move constructor.  12.8p12's copy is excluded because
		// copying an object is no way of building the first one.
		constructs = at->constexpr_function &&
			at->transfer != kCopyConstructorTransfer &&
			at->transfer != kMoveConstructorTransfer;
	}
	for (SemaEntity* at = entity.constructor; !constructs && at != nullptr;
	     at = at->next)
	{
		// The same bullet over the default constructor 12.1p5 *defines* rather
		// than one a declaration wrote.  What 12.1p5 asks of that one is
		// 7.1.5p4's whole list, and a member of scalar type it leaves alone is
		// the one bullet 3.9p10 has no sentence about - so a class with a base,
		// a virtual function or 11p1's access, none of which 8.5.1p1 leaves an
		// aggregate, is still a literal type here as it is in both oracles.
		// What that member costs is the *fold*, which refuses the object where
		// it is built and not the type where it is declared.
		constructs = at->defaulted && !at->deleted &&
			analyzer_.types_.parameters(at->type).size() == 1 &&
			constexpr_default_construction(scope, false);
	}
	if (!constructs)
	{
		return;
	}
	// 9.5p1's one storage is the shape `ConstexprReading::object_of` holds no
	// object of - a union is one member of however many the walk down the class
	// finds, and the interned list has an entry for each.  It is a 3.9p10
	// literal type all the same, so this is where the two answers part; 10p1's
	// base subobject is an entry of that list like any other.
	bool valued = analyzer_.types_.class_tag(entity.type) != ClassTag::Union;
	for (std::size_t index = 0; index < entity.bases.size(); ++index)
	{
		// 3.9p10's fourth bullet: a base class subobject is part of the object,
		// so an object of a class deriving from a non-literal one is not one a
		// constant expression builds either.
		const TypeId base = entity.bases[index].entity->type;
		if (!literal_type(base))
		{
			return;
		}
		valued = valued && valued_type(base);
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		if (!literal_type(member.type) ||
		    (analyzer_.types_.cv(member.type) & kCvVolatile) != 0)
		{
			return;
		}
		valued = valued && valued_type(member.type);
	}
	entity.literal_class = kLiteralYes;
	entity.valued_class = valued ? kLiteralYes : kLiteralNo;
}

// 7.1.5p9: an object declared `constexpr` shall have literal type and shall be
// initialized, and every full-expression of its initializer shall be a constant
// expression.
//
// The second half is asked of `SemaEntity::constant`, which is what the fold
// beside this one already settled for the declaration - so the requirement is
// one read and no second evaluation.  A declaration whose initializer the fold
// *ran* and refused has already said why in the fold's own words; what is left
// here is the declaration that ran none at all, which 8.5p6 makes the one that
// wrote no initializer and gave 12.1p5 no constexpr constructor to call.
//
// `covered` is that fold's answer to a different question: whether it read the
// initializer to an end at all.  A reading that ran out - an address 5.19p2 has
// and `SemaConstant` has not, an operator this milestone does not evaluate -
// leaves nothing to hold the declaration to, and the object takes 3.6.2p2's
// dynamic initialization exactly as it did before this requirement existed.
void ConstexprRequirement::require_object(const SemaEntity& entity, TypeId type,
                                          const SemaContext& ctx,
                                          const AstNode* initializer,
                                          bool covered) const
{
	if (instantiated() ||
	    (ctx.scope != nullptr && analyzer_.dependent_reading(*ctx.scope)))
	{
		// 14.6p8 and 7.1.5p6: a declaration read where an argument list has yet
		// to settle says neither what its type is nor what its initializer is
		// worth, and the specialization the arguments make is not a second
		// declaration for these requirements to be asked of again.
		return;
	}
	if (!literal_type(type))
	{
		throw std::runtime_error("the constexpr object " + entity.name +
		                         " is declared with " +
		                         analyzer_.types_.description(type) +
		                         ", which is not a literal type");
	}
	if (!entity.constant && covered && analyzer_.semantics() &&
	    (valued_type(type) || uninitialized(type, initializer)))
	{
		throw std::runtime_error("the constexpr object " + entity.name +
		                         " is initialized by nothing that is a constant "
		                         "expression");
	}
}

// 7.1.5p9's other half: an object declared `constexpr` shall be *initialized*.
//
// A declaration that wrote no initializer is initialized by 8.5p6, which for
// anything but a class 12.1p5 gives a constexpr default constructor performs no
// initialization at all - and an object nothing initialized holds no value,
// whatever kind of value this build would have held for it.  So this is the one
// refusal `valued_type` does not gate: `constexpr D d;` over a class with a
// base, or over a union, is a program both oracles refuse and one
// `ConstexprReading::object_of` holds no object of either way.
bool ConstexprRequirement::uninitialized(TypeId type,
                                         const AstNode* initializer) const
{
	if (initializer != nullptr && !initializer->children.empty())
	{
		return false;
	}
	TypeTable& types = analyzer_.types_;
	const TypeId element = types.strip_cv(types.element_of(types.strip_cv(type)));
	if (!types.is_class(element))
	{
		return true;
	}
	const SemaEntity* const built = analyzer_.default_constructor(element);
	return built == nullptr || !built->constexpr_function;
}

// 7.1.5p3's first three bullets and 7.1.5p4's second, asked where the
// declaration stands.
//
// It is one question and four doors: an ordinary function or member function
// definition, a constructor, a destructor and a conversion function each write
// a declarator 7.1.5 is about, and each is read by a walk of its own.  What
// differs between them is only 12.1p1 and 12.4p1's sentence - neither a
// constructor nor a destructor writes a return type, so the type they are
// declared with returns `void` and 7.1.5p3's second bullet is asked of nothing
// there, while 7.1.5p4's parameter types are asked of a constructor exactly as
// p3's are of every other one.  The object parameter 9.3.1p3 gave a member is
// a pointer, so 3.9p10 answers yes for it whatever the class is; 7.1.5p8's
// requirement that the class itself be literal is a separate sentence and is
// not asked here.
void ConstexprRequirement::require_function(const SemaEntity& entity,
                                            TypeId type,
                                            const std::string& name) const
{
	if (instantiated())
	{
		// 7.1.5p6: an instantiated specialization that fails these is simply
		// not a constexpr function, and the pattern is where the program was
		// written - so the requirement is asked there and not again here.
		return;
	}
	if (entity.virtual_function)
	{
		throw std::runtime_error("the constexpr function " + name +
		                         " is declared virtual");
	}
	const bool structural = entity.special == kConstructorFunction ||
		entity.special == kDestructorFunction;
	if (!structural && !literal_type(analyzer_.types_.target(type)))
	{
		throw std::runtime_error("the constexpr function " + name +
		                         " returns " +
		                         analyzer_.types_.description(
			                         analyzer_.types_.target(type)) +
		                         ", which is not a literal type");
	}
	const std::vector<TypeId>& parameters = analyzer_.types_.parameters(type);
	for (std::size_t index = entity.object_member ? 1 : 0;
	     index < parameters.size(); ++index)
	{
		if (!literal_type(parameters[index]))
		{
			throw std::runtime_error("the constexpr function " + name +
			                         " takes a parameter of " +
			                         analyzer_.types_.description(
				                         parameters[index]) +
			                         ", which is not a literal type");
		}
	}
}

// 7.1.5p4: every non-variant non-static data member and base class subobject of
// a constexpr constructor's class shall be initialized.
//
// The caller is 12.6.2p10's walk down the class, at the two places it finds a
// subobject that no mem-initializer named, that wrote no
// brace-or-equal-initializer and that default-initialization does nothing to -
// which is exactly 7.1.5p4's violation, so the requirement costs one field read
// per member the definition already looked at.
void ConstexprRequirement::require_initialized(
	const SemaEntity& function, const std::string& subobject) const
{
	if (!function.constexpr_function || instantiated() || function.defaulted)
	{
		// 12.1p5 leaves a defaulted definition no ctor-initializer to write, so
		// what it initializes is the standard's own answer and not a
		// requirement on anything the program wrote; `constexpr_default_
		// construction` is where that one is asked.
		return;
	}
	throw std::runtime_error("the constexpr constructor of " +
	                         analyzer_.types_.description(function.type) +
	                         " initializes neither " + subobject +
	                         " nor anything that would");
}
