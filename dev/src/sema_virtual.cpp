#include "sema_analyzer.h"

#include <stdexcept>
#include <unordered_map>

#include "ast_model.h"
#include "sema_derivation.h"

// 10.3 and 10.4: what a class dispatches.
//
// Every fact here is a fact of one class, settled once where 9.2p2 completes
// it and read by name afterwards.  Nothing below walks a base class's members:
// the direct base has already answered the same questions about itself, so the
// derived class starts from the base's answer and changes the entries its own
// declarations override.

namespace
{

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

// 9.4.1p2: a declaration of this class's own that declares a static member
// function.  It is the one declaration that dispatches on nothing and yet
// declares 13.1's name and signature, which is what 10.3p2 matches an
// overriding declaration by - so the settlement has to ask about it even
// though it can take no slot.
bool static_member_function(const SemaEntity& member)
{
	return member.kind == SemaKind::Function && !member.object_member &&
		member.special == kOrdinaryFunction && member.shadowed == nullptr &&
		member.surrogate_for == nullptr && member.inherited == nullptr;
}

// 9.2's member-declarator: whether this declarator wrote a virt-specifier at
// all, which is what 9.2p8 allows only where the declaration is of a virtual
// member function.
bool writes_virt_specifier(const AstNode& declarator)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		if (declarator.children[index]->kind == AstKind::VirtSpecifier)
		{
			return true;
		}
	}
	return false;
}

// 10.3p1 and 10.3p10: the base subobject a class dispatches through, which is
// the one base of it that carries a vpointer.  Null for a class that carries
// none and for one that adds its own, and never a second base, because a class
// holding two that dispatch is refused where it is completed - so the table this
// class owns is that one base's table with its own overriders written in.
SemaEntity* dispatching_base(const SemaEntity& entity)
{
	for (std::size_t index = 0; index < entity.bases.size(); ++index)
	{
		if (entity.bases[index].entity->polymorphic)
		{
			return entity.bases[index].entity;
		}
	}
	return nullptr;
}

void note_shared_entry_points(SemaEntity& entity)
{
	for (SemaEntity* made = entity.constructor; made != nullptr;
	     made = made->next)
	{
		made->complete_object_entry = made->complete_object_entry ||
			(made->user_provided && made->own_source_definition);
	}
	if (entity.destructor != nullptr && entity.destructor->user_provided &&
	    entity.destructor->own_source_definition)
	{
		entity.destructor->complete_object_entry = true;
	}
	for (std::size_t index = 0; index < entity.bases.size(); ++index)
	{
		note_shared_entry_points(*entity.bases[index].entity);
	}
}

// 12.1, 12.4 and the ABI: which of a class's own special members the object
// file owes both of the ABI's entry points for.
//
// The classes a polymorphic object is built out of that this unit's own source
// wrote are the one the vpointer starts at and the non-polymorphic classes
// under it - the derivation below the vpointer.  A complete object of any of
// them can be created wherever its definition is seen, and this unit's source
// is what wrote the definition every unit shares.  A class whose base already
// dispatches adds none of them: the vpointer it carries is one its base's own
// completion already asked this of, and every unit that can create a complete
// object of it can define its members for itself.
//
// A member the standard gave the class is not one a program named, and 2.2p1's
// included file is one every unit that includes it holds a definition of too -
// so each of those stands under the one entry this unit's own code asked for.
// A definition written outside its class without `inline` is this unit's alone
// whichever file it stands in, and `writes_base_entry` already owes both of the
// ABI's names for it.
void settle_shared_entry_points(SemaEntity& entity)
{
	if (!entity.introduces_vptr)
	{
		return;
	}
	// 10p1: a class with no polymorphic base has none above it either, so this
	// walk is the whole derivation below the vpointer and costs each class in it
	// once for the program rather than once per class derived from it.
	note_shared_entry_points(entity);
}

// 10.3p5, 9.2p8 and 10.4p2: what a member function that overrides nothing may
// not have written.  `override` asks the class for an overridden function it
// does not have; the other two are written only where the declaration is of a
// virtual member function - which for one that overrides nothing means its own
// `virtual` keyword.
void require_dispatches(const SemaEntity& member)
{
	if (member.override_written)
	{
		throw std::runtime_error(member.name + " is declared `override` and "
		                         "overrides no virtual function of a base "
		                         "class");
	}
	if (member.virtual_function)
	{
		return;
	}
	if (member.final_virtual)
	{
		throw std::runtime_error(member.name + " is declared `final` and is not "
		                         "a virtual function");
	}
	if (member.pure_virtual)
	{
		throw std::runtime_error(member.name + " is declared with a "
		                         "pure-specifier and is not virtual");
	}
}

// 10.3p1 and 9.3p3: `virtual`, `override` and `final` are written on a
// non-static member function, which a constructor and a static member are not.
void require_no_virtual_specifier(const SemaEntity& member)
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

}  // namespace

// 10.3p1 read before 9.2p13 lays the class out: whether an object of this class
// carries a vpointer, and whether this class is the one that adds it.
//
// Only the declarations say this much.  10.3p2's overriding can make a function
// virtual that no declaration wrote `virtual` on, but a class that has one has
// a polymorphic base and is therefore polymorphic already - so the layout
// question is answered without settling any override.
void SemaAnalyzer::note_polymorphism(SemaEntity& entity, Scope& scope)
{
	std::size_t dispatching = 0;
	const SemaEntity* const primary = dispatching_base(entity);
	for (std::size_t index = 0; index < entity.bases.size(); ++index)
	{
		// 10p1: a class holding a base subobject that carries a vpointer
		// carries one, wherever in the object that subobject stands.
		if (entity.bases[index].entity->polymorphic)
		{
			entity.polymorphic = true;
			++dispatching;
		}
	}
	for (std::size_t index = 0;
	     !entity.polymorphic && index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (member.virtual_function && dispatchable_member(member))
		{
			// 10.3p1: no base carries one, so this class is the one that adds
			// it - the ABI gives it the first eight bytes and lays the bases
			// and the members out after them, all of them dispatching through
			// the one table this class owns.
			entity.polymorphic = true;
			entity.introduces_vptr = true;
		}
	}
	if (dispatching == 0 || checking_ > 0)
	{
		return;
	}
	// 10.3p10 and the ABI: the one table this class owns is the table of the
	// base subobject standing at its first byte, so a *second* base that
	// dispatches, or a first one standing after storage another base already
	// took, owes a secondary table and a thunk that steps a call through it back
	// to the complete object.  Neither is emitted here, so such a class is
	// refused rather than given a table that dispatches wrongly - while a class
	// whose dispatching base does begin where the object does lays out and
	// dispatches through that one table however many plain bases stand after it.
	bool at_start = dispatching == 1;
	for (std::size_t index = 0;
	     at_start && index < entity.bases.size() &&
	     entity.bases[index].entity != primary;
	     ++index)
	{
		at_start = entity.bases[index].entity->empty_class;
	}
	if (!at_start)
	{
		throw std::runtime_error(types_.description(entity.type) +
		                         " holds a base class subobject that dispatches "
		                         "and does not begin where the object does, "
		                         "which this milestone does not lay out");
	}
}

// 10.3p7: whether the return type of an overriding function is the one the
// overridden function wrote, or one covariant with it.
//
// The classes have to be in this milestone's single-inheritance slice for the
// pointer the base's caller holds to be the pointer the override hands back
// with no adjustment, which is what the Assignment Boundary's "no thunks" says;
// the derivation walk `derives_from` does is that same single chain.
bool SemaAnalyzer::covariant_return(TypeId overriding, TypeId overridden,
                                    Scope* where)
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
	if (!derives_from(*derived->scope, *base->scope))
	{
		return false;
	}
	// 10.3p7: the base has to be an *accessible* one of the class the override
	// wrote, and 11.2p1 asks that of the class the overriding declaration is
	// written in - which is what lets a friend of the derived class write a
	// return type a stranger to it may not.
	const Naming naming(*this, where);
	return Derivation(*this).accessible(derived, *base);
}

// 10.3p2's key: the name, and the parameter-type-list, cv-qualifier-seq and
// ref-qualifier that 13.1 already tells two declarations of one name apart by.
//
// The name is the small integer the run interns it as rather than its letters,
// because this is asked of every member of every class and the answer is
// compared and never read.
std::uint64_t SemaAnalyzer::override_key(const SemaEntity& member)
{
	return (static_cast<std::uint64_t>(override_names_.intern(member.name))
	        << 32) |
		member_signature(member);
}

// 10.3p2: the slot a declaration of this key already has in the table of
// `from`, or `kNoVtableIndex` where no class in the derivation gave it one.
//
// Each class records only the slots it *introduces*: a slot's index is fixed
// where the name first took one and every class below copies the table with
// that index, so the walk down the derivation asks the class that introduced it
// and no class rebuilds an index over what it inherited.  That is what keeps a
// chain of n classes linear in the records it holds rather than quadratic.
unsigned SemaAnalyzer::inherited_slot(const SemaEntity* from,
                                      std::uint64_t key) const
{
	if (from == nullptr)
	{
		return kNoVtableIndex;
	}
	const std::unordered_map<std::uint32_t, SlotIndex>::const_iterator records =
		introduced_slots_.find(from->id);
	if (records != introduced_slots_.end())
	{
		const SlotIndex::const_iterator found = records->second.find(key);
		if (found != records->second.end())
		{
			return found->second;
		}
	}
	for (std::size_t index = 0; index < from->bases.size(); ++index)
	{
		const unsigned above = inherited_slot(from->bases[index].entity, key);
		if (above != kNoVtableIndex)
		{
			return above;
		}
	}
	return kNoVtableIndex;
}

// 10.3p2 and 10.3p10, settled where 9.2p2 completes the class and after 12.1p5
// and 12.4p3 have given it the members no declaration wrote: which of this
// class's member functions are virtual, which slot each has, and what the
// class's table holds.
void SemaAnalyzer::settle_virtual_members(SemaEntity& entity, Scope& scope)
{
	// 10.3p10: the slots a class inherits are the ones the base it dispatches
	// through holds, and no other base of it can hold any - a class that
	// declares a virtual function is polymorphic and would be that one.
	SemaEntity* const base = dispatching_base(entity);
	if (base != nullptr && base->polymorphic)
	{
		// 10.3p10: the derived class's table is the base's, with the entries its
		// own declarations override replaced in place.  The copy is what the ABI
		// emits for this class, so it is the table and not a view of the base's.
		entity.vtable = base->vtable;
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		if (!dispatchable_member(member))
		{
			require_no_virtual_specifier(member);
			// 10.3p2 and 9.4.1p2: a declaration of this class with the name and
			// the signature of an inherited virtual function overrides it and
			// is virtual whether or not it says so - and a static member
			// function shall not be virtual.  It is the one declaration that
			// takes no slot and still has to ask.
			if (static_member_function(member) &&
			    inherited_slot(base, override_key(member)) != kNoVtableIndex)
			{
				throw std::runtime_error(member.name + " is a static member "
				                         "function and declares the name and "
				                         "signature of a virtual function of a "
				                         "base class");
			}
			continue;
		}
		if (member.special == kDestructorFunction)
		{
			settle_virtual_destructor(entity, member);
			continue;
		}
		const std::uint64_t key = override_key(member);
		const unsigned found = inherited_slot(base, key);
		if (found != kNoVtableIndex)
		{
			SemaEntity& over = *entity.vtable[found].overrider;
			// 10.3p2: a function that overrides a virtual function is itself
			// virtual, whether or not its declaration wrote `virtual`.
			member.virtual_function = true;
			member.overridden = &over;
			member.vtable_index = found;
			entity.vtable[found].overrider = &member;
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
		// 10.3p2: this class is where the name and signature first took a slot,
		// and the slot a name has never moves - a class derived from this one
		// copies the table with its indices - so the record belongs here and a
		// class below reads it rather than building one of its own.
		introduced_slots_[entity.id][key] = member.vtable_index;
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
	settle_vtable_ownership(entity, scope);
}

// The ABI's two questions about the *emitted* table, asked here because both
// are questions about the class as its own body left it.
//
// A table is data, and 3.2p3 gives the program one definition of it however
// many units need one.  The ABI answers that with the key function: the first
// virtual member the class declares that is neither pure nor inline is one
// exactly one unit defines, so that unit is the one that holds the table and
// every other unit names it.  A class with no such member - every virtual
// member of it defined in its own body - has no such unit, so every unit that
// needs the table holds a copy of it and the linker keeps one.
//
// The second question is the deleting entry's.  5.3.5p9 looks the deallocation
// function up in the class of the object being destroyed, and the entry the ABI
// asks for is a definition the translation writes with no delete-expression
// under it - so the lookup happens here, where the class is complete, rather
// than where the entry is lowered.
void SemaAnalyzer::settle_vtable_ownership(SemaEntity& entity, Scope& scope)
{
	if (entity.vtable.empty())
	{
		return;
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		if (!dispatchable_member(member) || !member.virtual_function ||
		    member.pure_virtual || member.inline_function)
		{
			continue;
		}
		// 7.1.2p2 and 9.3p2: what makes the declaration inline is the class
		// body holding its definition or the specifier written on it, both of
		// which stand where the class is completed - so the answer this reads
		// is the one the ABI asks for and no later definition changes it.
		entity.key_function = &member;
		break;
	}
	settle_shared_entry_points(entity);
	SemaEntity* const destructor = entity.destructor;
	if (destructor != nullptr && destructor->virtual_function)
	{
		// 12.4, 3.2p2 and the ABI: the table's own entry is the complete-object
		// one, because a `delete` of a base pointer reaches it on an object that
		// is no subobject of anything - so the table naming the slot is a use of
		// that entry however little else in the program names it, and 12.4p6
		// gives the destructor a definition here as any other use would.
		note_destruction_entry(*destructor, false);
		std::vector<SemaEntity*> found;
		destructor->deleting_release =
			deallocation_function(false, entity.type, false, found);
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
	// 10.3p10 again: a destructor overrides the one the base this class
	// dispatches through declared, which is the only base holding a slot.
	SemaEntity* const base = dispatching_base(entity);
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
	                      types_.target(overridden.type), member.region))
	{
		throw std::runtime_error(member.name + " overrides a virtual function "
		                         "whose return type it is not covariant with");
	}
}

// 7.1.2p1 and 9.2p8: `virtual` and the virt-specifiers beside it are written
// only on the declaration a class body makes.  A member function defined
// outside its class repeats neither, and a function that is no member of a
// class dispatches on nothing at all - so both halves are the one question,
// asked wherever a declarator that could carry either is read, and which
// *member* may carry it is asked once the class is complete.
void SemaAnalyzer::require_virtual_placement(bool wrote_virtual,
                                             const AstNode* declarator,
                                             const Scope& where,
                                             bool qualified,
                                             const std::string& name)
{
	if (where.kind == ScopeKind::Class && !qualified)
	{
		return;
	}
	if (wrote_virtual)
	{
		throw std::runtime_error(name + " is declared `virtual` outside the "
		                         "body of a class");
	}
	if (declarator != nullptr && writes_virt_specifier(*declarator))
	{
		// 8.4p2: a virt-specifier-seq can be part of a function-definition only
		// where the definition is a member-declaration, which the one written
		// outside the class is not.
		throw std::runtime_error(name + " writes `override` or `final` outside "
		                         "the body of a class");
	}
}

// The same reading over the form a constructor, a destructor or a conversion
// function is written in: the keyword stands in the member-specifiers ahead of
// the declarator-id rather than in a decl-specifier-seq, and the
// virt-specifiers stand on the declarator as they do everywhere else - so the
// declaration is handed to the one question above rather than asking a second
// one of its own.
void SemaAnalyzer::require_special_virtual_placement(const AstNode& node,
                                                     const Scope& where,
                                                     bool qualified,
                                                     const std::string& name)
{
	bool wrote_virtual = false;
	const AstNode* declarator = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& part = *node.children[index];
		if (part.kind == AstKind::Declarator)
		{
			declarator = &part;
			continue;
		}
		if (part.kind != AstKind::MemberSpecifiers)
		{
			continue;
		}
		for (std::size_t at = 0; at < part.children.size(); ++at)
		{
			wrote_virtual =
				wrote_virtual || part.children[at]->token == KW_VIRTUAL;
		}
	}
	require_virtual_placement(wrote_virtual, declarator, where, qualified, name);
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
	if (abstract_class_type(types_.element_of(type)))
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
