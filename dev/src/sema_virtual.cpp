#include "sema_analyzer.h"

#include <stdexcept>
#include <unordered_map>

#include "ast_model.h"

// 10.3 and 10.4: what a class dispatches.
//
// Every fact here is a fact of one class, settled once where 9.2p2 completes
// it and read by name afterwards.  Nothing below walks a base class's members:
// the direct base has already answered the same questions about itself, so the
// derived class starts from the base's answer and changes the entries its own
// declarations override.

namespace
{

// The key 10.3p2 tells an overriding declaration from one that merely reuses a
// name by: the name, and the parameter-type-list, cv-qualifier-seq and
// ref-qualifier that 13.1 already tells two declarations of one name apart by.
std::string override_key(const std::string& name, std::uint32_t signature)
{
	std::string key = name;
	key.push_back('\0');
	for (std::uint32_t rest = signature; rest != 0; rest >>= 8)
	{
		key.push_back(static_cast<char>(rest & 0xffu));
	}
	return key;
}

// Whether a declaration of `scope` can take a vtable slot at all.  A
// constructor cannot be virtual - 12.1p4 - and a static member function has no
// object to dispatch on; a using-declaration's member stands for a declaration
// of the base, which that base already gave a slot.
bool dispatchable_member(const SemaEntity& member)
{
	return member.kind == SemaKind::Function && member.object_member &&
		member.special != kConstructorFunction && member.shadowed == nullptr &&
		member.surrogate_for == nullptr && member.inherited == nullptr;
}

}

// 10.3p1 read before 9.2p13 lays the class out: whether an object of this class
// carries a vpointer, and whether this class is the one that adds it.
//
// Only the declarations say this much.  10.3p2's overriding can make a function
// virtual that no declaration wrote `virtual` on, but a class that has one has
// a polymorphic base and is therefore polymorphic already - so the layout
// question is answered without settling any override.
void SemaAnalyzer::note_polymorphism(SemaEntity& entity, Scope& scope)
{
	if (entity.base != nullptr && entity.base->polymorphic)
	{
		// 10p1: the base subobject stands at the start of the object and
		// carries the pointer, so this class adds nothing and lays its own
		// members out after the base exactly as a non-polymorphic one does.
		entity.polymorphic = true;
		return;
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (member.virtual_function && dispatchable_member(member))
		{
			entity.polymorphic = true;
			entity.introduces_vptr = true;
			return;
		}
	}
}

// 10p1 and 9.2p13: where the base class subobject of type `base` stands inside
// an object of class type `from`.
//
// Each class in the derivation already holds where it put its own direct base,
// so the walk is one addition per level of the chain the conversion names, and
// it is the chain the caller already found the base through.  Zero wherever no
// class in it added a vpointer, which is every hierarchy of the PA17 subset.
unsigned long long SemaAnalyzer::base_subobject_offset(TypeId from,
                                                       const SemaEntity& base)
{
	unsigned long long offset = 0;
	for (const SemaEntity* at = model_.type_owner(types_.strip_cv(from));
	     at != nullptr && at != &base; at = at->base)
	{
		offset += at->base_offset;
	}
	return offset;
}

// 10.3p7: whether the return type of an overriding function is the one the
// overridden function wrote, or one covariant with it.
//
// The classes have to be in this milestone's single-inheritance slice for the
// pointer the base's caller holds to be the pointer the override hands back
// with no adjustment, which is what the Assignment Boundary's "no thunks" says;
// the derivation walk `derives_from` does is that same single chain.
bool SemaAnalyzer::covariant_return(TypeId overriding, TypeId overridden)
{
	if (overriding == overridden)
	{
		return true;
	}
	const TypeKind shape = types_.kind(overriding);
	if (shape != types_.kind(overridden))
	{
		return false;
	}
	if (shape != TypeKind::Pointer && shape != TypeKind::LValueReference)
	{
		return false;
	}
	// 10.3p7: both are pointers to classes or both are lvalue references to
	// classes, and the one the override wrote is no more cv-qualified than the
	// one it overrides.
	const TypeId to = types_.target(overriding);
	const TypeId from = types_.target(overridden);
	if (!types_.is_class(types_.strip_cv(to)) ||
	    !types_.is_class(types_.strip_cv(from)))
	{
		return false;
	}
	const unsigned to_cv = types_.object_cv(to);
	const unsigned from_cv = types_.object_cv(from);
	if ((to_cv & ~from_cv) != 0)
	{
		return false;
	}
	if (types_.cv(overriding) != types_.cv(overridden))
	{
		return false;
	}
	const SemaEntity* const derived = model_.type_owner(types_.strip_cv(to));
	const SemaEntity* const base = model_.type_owner(types_.strip_cv(from));
	if (derived == nullptr || base == nullptr || derived->scope == nullptr ||
	    base->scope == nullptr)
	{
		return false;
	}
	return derives_from(*derived->scope, *base->scope);
}

// 10.3p2 and 10.3p10, settled where 9.2p2 completes the class and after 12.1p5
// and 12.4p3 have given it the members no declaration wrote: which of this
// class's member functions are virtual, which slot each has, and what the
// class's table holds.
void SemaAnalyzer::settle_virtual_members(SemaEntity& entity, Scope& scope)
{
	SemaEntity* const base = entity.base;
	const bool inherits = base != nullptr && base->polymorphic;
	// 10.3p10: the derived class's table is the base's, with the entries its
	// own declarations override replaced in place.  The copy is what the ABI
	// emits for this class, so it is the table and not a view of the base's.
	std::unordered_map<std::string, unsigned> inherited;
	if (inherits)
	{
		entity.vtable = base->vtable;
		for (std::size_t slot = 0; slot < entity.vtable.size(); ++slot)
		{
			const SemaEntity* const at = entity.vtable[slot].overrider;
			if (at == nullptr || entity.vtable[slot].deleting ||
			    at->special == kDestructorFunction)
			{
				// A destructor overrides by being one and not by its name,
				// which is its own class's; the two entries it holds are found
				// through `base->destructor` rather than through this map.
				continue;
			}
			inherited[override_key(at->name, member_signature(*at))] =
				static_cast<unsigned>(slot);
		}
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		if (!dispatchable_member(member))
		{
			require_no_virtual_specifier(member);
			continue;
		}
		if (member.special == kDestructorFunction)
		{
			settle_virtual_destructor(entity, member);
			continue;
		}
		const std::string key =
			override_key(member.name, member_signature(member));
		const std::unordered_map<std::string, unsigned>::const_iterator found =
			inherited.find(key);
		if (found != inherited.end())
		{
			SemaEntity& over = *entity.vtable[found->second].overrider;
			// 10.3p2: a function that overrides a virtual function is itself
			// virtual, whether or not its declaration wrote `virtual`.
			member.virtual_function = true;
			member.overridden = &over;
			member.vtable_index = found->second;
			entity.vtable[found->second].overrider = &member;
			require_overridable(member, over);
			continue;
		}
		require_dispatches(member);
		if (!member.virtual_function)
		{
			continue;
		}
		member.vtable_index = static_cast<unsigned>(entity.vtable.size());
		const VirtualSlot slot = { &member, false };
		entity.vtable.push_back(slot);
	}
	// 10.3p1: a class with a table dispatches, whatever its declarations wrote -
	// which is the same answer `note_polymorphism` already gave the layout,
	// reached from the table this settlement built rather than from the
	// declarations it built it out of.
	entity.polymorphic = entity.polymorphic || !entity.vtable.empty();
	// 10.4p2: the class is abstract where any of its final overriders is a
	// declaration the program left pure.  One pass of the table settles it, and
	// every use of the class afterwards reads the one answer.
	for (std::size_t slot = 0; slot < entity.vtable.size(); ++slot)
	{
		const SemaEntity* const at = entity.vtable[slot].overrider;
		if (at != nullptr && at->pure_virtual)
		{
			entity.abstract = true;
			break;
		}
	}
}

// 12.4p9 and the ABI: the destructor's place in the table.
//
// A destructor overrides the base's by being a destructor - 10.3p6 - and not by
// name or signature, and it takes the pair of consecutive entries the ABI gives
// one: the complete-object entry the program's own `delete` of a base pointer
// does not use, and the deleting entry it does.
void SemaAnalyzer::settle_virtual_destructor(SemaEntity& entity,
                                             SemaEntity& destructor)
{
	SemaEntity* const base = entity.base;
	SemaEntity* const over =
		base != nullptr && base->destructor != nullptr &&
			base->destructor->virtual_function
		? base->destructor
		: nullptr;
	if (over != nullptr)
	{
		destructor.virtual_function = true;
		destructor.overridden = over;
		destructor.vtable_index = over->vtable_index;
		entity.vtable[over->vtable_index].overrider = &destructor;
		entity.vtable[over->vtable_index + 1].overrider = &destructor;
		require_overridable(destructor, *over);
		return;
	}
	require_dispatches(destructor);
	if (!destructor.virtual_function)
	{
		return;
	}
	destructor.vtable_index = static_cast<unsigned>(entity.vtable.size());
	// The ABI's pair: 12.4's complete-object entry, and the one that runs it and
	// then gives the storage back, which is what 5.3.5p3's `delete` reaches.
	const VirtualSlot complete = { &destructor, false };
	entity.vtable.push_back(complete);
	const VirtualSlot deleting = { &destructor, true };
	entity.vtable.push_back(deleting);
}

// 10.3p4 and 10.3p7: what the class an override is written in has to agree with.
void SemaAnalyzer::require_overridable(const SemaEntity& member,
                                       const SemaEntity& overridden)
{
	if (overridden.final_virtual)
	{
		throw std::runtime_error(member.name + " overrides a virtual function "
		                         "declared `final`");
	}
	if (!covariant_return(types_.target(member.type),
	                      types_.target(overridden.type)))
	{
		throw std::runtime_error(member.name + " overrides a virtual function "
		                         "whose return type it is not covariant with");
	}
}

// 10.3p5 and 10.4p2: what a member function that overrides nothing may not have
// written.  `override` asks the class for an overridden function it does not
// have, and a pure-specifier is written only where the declaration is virtual -
// which for one that overrides nothing means its own `virtual` keyword.
void SemaAnalyzer::require_dispatches(const SemaEntity& member)
{
	if (member.override_written)
	{
		throw std::runtime_error(member.name + " is declared `override` and "
		                         "overrides no virtual function of a base "
		                         "class");
	}
	if (member.pure_virtual && !member.virtual_function)
	{
		throw std::runtime_error(member.name + " is declared with a "
		                         "pure-specifier and is not virtual");
	}
}

// 7.1.2p1: `virtual` is written only on the declaration a class body makes.  A
// member function defined outside its class repeats neither the keyword nor the
// virt-specifiers, and a function that is no member of a class dispatches on
// nothing at all - so where the keyword stands is asked where the declarator
// was read, and which *member* may carry it is asked once the class is
// complete.
void SemaAnalyzer::require_virtual_placement(bool wrote_virtual,
                                             const Scope& where,
                                             bool qualified,
                                             const std::string& name)
{
	if (wrote_virtual && (where.kind != ScopeKind::Class || qualified))
	{
		throw std::runtime_error(name + " is declared `virtual` outside the "
		                         "body of a class");
	}
}

// 10.3p1 and 9.3p3: `virtual`, `override` and `final` are written on a
// non-static member function, which a constructor and a static member are not.
void SemaAnalyzer::require_no_virtual_specifier(const SemaEntity& member)
{
	if (member.kind != SemaKind::Function || member.shadowed != nullptr ||
	    member.inherited != nullptr)
	{
		return;
	}
	if (member.virtual_function || member.override_written ||
	    member.final_virtual || member.pure_virtual)
	{
		throw std::runtime_error(member.name + " is not a non-static member "
		                         "function and is declared `virtual`, "
		                         "`override`, `final` or pure");
	}
}

// 10.4p2: whether an object of `type` cannot be created because a final
// overrider of its class is pure.
bool SemaAnalyzer::abstract_class_type(TypeId type)
{
	if (!types_.is_class(types_.strip_cv(type)))
	{
		return false;
	}
	const SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	return owner != nullptr && owner->abstract;
}

// 10.4p2: no object of an abstract class can be created, because the object
// would have a vtable slot with nothing to run in it.  An array of them is as
// many of those objects as it has elements, so the element is what is asked
// about; a pointer or a reference to one creates nothing and is fine, which is
// what makes the class worth declaring at all.
void SemaAnalyzer::require_creatable_object(TypeId type,
                                            const std::string& name)
{
	if (abstract_class_type(element_of(type)))
	{
		throw std::runtime_error(name + " is an object of an abstract class");
	}
}

// 10.4p3: an abstract class shall not be used as a parameter type, as a
// function return type, or as the type of an explicit conversion.  A pointer or
// a reference to one is fine, which is what makes the class worth declaring:
// the check is on the type itself and never on what points at it.
void SemaAnalyzer::require_no_abstract_boundary(TypeId type,
                                                const std::string& name)
{
	if (types_.kind(type) != TypeKind::Function)
	{
		return;
	}
	if (abstract_class_type(types_.target(type)))
	{
		throw std::runtime_error(name + " returns an object of an abstract "
		                         "class");
	}
	const std::vector<TypeId>& parameters = types_.parameters(type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		if (abstract_class_type(parameters[index]))
		{
			throw std::runtime_error(name + " takes a parameter of an abstract "
			                         "class");
		}
	}
}

// 9.2 and 10.3: the three things a member function's declarator can say about
// dispatch that its decl-specifier-seq does not - `override`, `final` and
// 10.4p2's pure-specifier.
void SemaAnalyzer::read_virt_specifiers(SemaEntity& function,
                                        const AstNode& declarator,
                                        const AstNode* initializer)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& part = *declarator.children[index];
		if (part.kind != AstKind::VirtSpecifier)
		{
			continue;
		}
		// 10.3p4 and 10.3p5: both are facts of the declaration, so one written
		// on any declaration of the function stands for the function.
		function.override_written =
			function.override_written || part.text == "override";
		function.final_virtual =
			function.final_virtual || part.text == "final";
	}
	if (initializer != nullptr && !initializer->children.empty() &&
	    initializer->children[0]->kind == AstKind::Literal &&
	    initializer->children[0]->text == "0")
	{
		// 10.4p2: the pure-specifier says the class declares the function and
		// leaves the definition to whatever overrides it, which is what makes
		// the class abstract.  9.2 allows it only on a virtual function, and
		//10.3p2 makes a declaration that overrides one virtual without writing
		// the keyword - so whether this declaration may write it is a question
		// only the complete class can answer.
		function.pure_virtual = true;
	}
}
