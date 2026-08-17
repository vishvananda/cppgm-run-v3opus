#include "sema_constexpr.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_derivation.h"
#include "sema_reading.h"
#include "sema_scope.h"
#include "sema_string_init.h"

namespace
{

// 8.3.4p3: an array whose declaration wrote no bound takes as many elements as
// the initializer gives it, so the walk of its elements is bounded by the
// clauses left rather than by the type.  By the time a fold reads a declaration
// the deduced bound is already on the type, so this stands for a subobject the
// walk reaches with none - which the standard leaves no way to declare.
const unsigned long long kUnboundedElements = ~0ull;

const AstNode* child_kind(const AstNode& node, AstKind kind)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index]->kind == kind)
		{
			return node.children[index];
		}
	}
	return nullptr;
}

// 7.1.5p3: the declarations a constexpr constructor's body may write beside its
// ctor-initializer.  They declare no object and run nothing, so a fold reads
// them for the names they introduce and for nothing else.
bool is_body_declaration(AstKind kind)
{
	return kind == AstKind::SimpleDeclaration ||
		kind == AstKind::AliasDeclaration ||
		kind == AstKind::UsingDeclaration ||
		kind == AstKind::UsingDirective ||
		kind == AstKind::StaticAssertDeclaration ||
		kind == AstKind::EmptyDeclaration;
}

}

// The object a constant expression holds, and what reads a subobject back out
// of it.
//
// 3.9p10 makes a class of literal type and an array of one constants of the
// same standing as an arithmetic value, and what one of them is worth is the
// interned list of what its subobjects hold.  This file owns both directions of
// that list: `subobjects` says which subobjects there are and in what order,
// `object_of`, `object_from_constructor`, `clause_of` and `array_of` build the
// list from an initializer, and `member_value` and `element_value` read one
// entry back out.  Everything else about a fold - the operators, the calls, the
// conversions, the declaration - is `sema_constexpr.cpp`'s, and the addresses
// the entries are designated through are `sema_address.cpp`'s.

// 12.6.2p10: what an object of this class is made of, in the order the standard
// initializes it in - the direct base class subobjects in base-specifier order,
// then 9.2p13's non-static data members in declaration order.
//
// That order is the one 9.2p13 lays the storage out in too, so an entry's index
// in the interned list is the one number a member access, a base conversion and
// an address path all index by, and no reader needs a second answer.
void ConstexprReading::subobjects(TypeId type,
                                  std::vector<Subobject>& out) const
{
	SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(type));
	if (owner == nullptr || owner->scope == nullptr)
	{
		return;
	}
	out.reserve(out.size() + owner->bases.size());
	for (std::size_t index = 0; index < owner->bases.size(); ++index)
	{
		Subobject held;
		held.entity = owner->bases[index].entity;
		held.type = held.entity->type;
		held.base = true;
		out.push_back(held);
	}
	const Scope& region = *owner->scope;
	for (std::size_t index = 0; index < region.declarations.size(); ++index)
	{
		SemaEntity& member = *region.declarations[index];
		if (declares_subobject(member, region))
		{
			Subobject held;
			held.entity = &member;
			held.type = member.type;
			held.base = false;
			out.push_back(held);
		}
	}
}

// 5.2.3p2 and p3: an object of literal class type built where a value belongs.
//
// The clauses initialize the non-static data members in declaration order,
// which is 8.5.1p2 exactly, and 8.5.1p7 value-initializes every member none of
// them reached.  What the object comes to is the interned list of what its
// subobjects hold, so the class and that list together are the object: two
// namings of `S{5}` are one entry, and `S{5}` and `S{6}` are two.  8.5.1p1
// leaves no class with a base an aggregate, so the walk here reaches 9.2p1's
// members alone and 10p1's base subobjects are `object_from_constructor`'s.
SemaConstant ConstexprReading::object_of(TypeId type,
                                         const std::vector<SemaConstant>& written)
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	ask_for_definition(bare);
	SemaEntity* const owner = analyzer_.model_.type_owner(bare);
	if (owner == nullptr || owner->scope == nullptr)
	{
		throw NotConstant(analyzer_.types_.description(bare) +
		                  " is not a class a constant expression builds an "
		                  "object of", false);
	}
	if (written.empty())
	{
		// 8.5p7: every subobject of the object is value-initialized, so the
		// object is one value per class and not one per place that asks for it.
		// A class two of whose members are themselves such classes would
		// otherwise be walked once per *path* down to its scalars, which is
		// 2^depth walks of a program a few hundred bytes long.
		const TypeId held = analyzer_.model_.value_initialized(bare);
		if (held != kNoType)
		{
			return constant_of(held, analyzer_.types_);
		}
	}
	if (!analyzer_.aggregate_type(bare))
	{
		// 8.5.1p1: a class that declares a constructor of its own - or that
		// hides a member behind 11p1's access - is no aggregate, so what stands
		// between the clauses and the members is 12.1's constructor and not
		// 8.5.1p2's one-clause-per-member.
		return object_from_constructor(bare, *owner, written);
	}
	std::vector<Subobject> members;
	subobjects(bare, members);
	if (written.size() > members.size())
	{
		throw NotConstant("a constant expression writes more initializers than " +
		                  analyzer_.types_.description(bare) + " has members");
	}
	// 8.5.1p15 and 9.5p1: a union holds one of its members at a time, so the
	// entries its list has are the ones an initialization began the lifetime of
	// - the first member, which is the only one a clause reaches and the one
	// 8.5p7 value-initializes where no clause does.  8.5.1p7's tail is no part
	// of this: a member no initialization named holds nothing, and a list padded
	// with the zeroes it gives an aggregate's tail would answer every one of
	// them, which is the reading `aggregate_constant` stops for.
	const std::size_t reached =
		analyzer_.one_storage(bare) && !members.empty() ? 1 : members.size();
	std::vector<TypeId> held;
	held.reserve(reached);
	for (std::size_t index = 0; index < reached; ++index)
	{
		const TypeId member = analyzer_.types_.strip_cv(members[index].type);
		// 8.5.1p2 and p7: the clause written for the member, or the zero
		// 8.5p7's value-initialization gives one no clause reached.
		SemaConstant value;
		value.type = member;
		if (index < written.size())
		{
			value = written[index];
			if (analyzer_.types_.strip_cv(value.type) != member)
			{
				value = analyzer_.convert(value, member);
			}
			value.type = member;
		}
		else if (analyzer_.types_.is_class(member))
		{
			// 8.5p7: a member of class type none of the clauses reached is
			// value-initialized, which for one of these is the object every one
			// of *its* members is value-initialized in.
			const std::vector<SemaConstant> none;
			value = object_of(member, none);
		}
		else if (analyzer_.types_.kind(member) == TypeKind::Array)
		{
			// 3.9p10 and 8.5p7: an array of literal type is a literal type too,
			// so a member that is one holds a list exactly as a member of class
			// type does, and one no clause reached holds the list 8.5.1p7
			// value-initializes every element in.
			const std::vector<SemaConstant> none;
			value = array_of(member, none);
		}
		if (!valued_subobject(member))
		{
			throw NotConstant("a member of " +
			                  analyzer_.types_.description(bare) +
			                  " is outside the values a constant expression "
			                  "holds", false);
		}
		held.push_back(entry_of(value));
	}
	SemaConstant out;
	out.type = bare;
	out.bits = analyzer_.types_.type_list(held);
	if (written.empty())
	{
		analyzer_.model_.hold_value_initialized(bare, entry_of(out));
	}
	return out;
}

// 8.5.1p2: one initializer-clause read for the subobject it initializes.
//
// A clause that is itself a braced-init-list is no expression: what it comes to
// is the object its own clauses build, and which of 8.5's initializations that
// is comes from the subobject's type.  So the walk goes down the object rather
// than down the syntax, and an element or a member of class or array type is
// read here exactly as the whole object is.
SemaConstant ConstexprReading::clause_of(const AstNode& clause, TypeId target,
                                         const SemaContext& ctx)
{
	const TypeId bare = analyzer_.types_.strip_cv(target);
	if (clause.kind != AstKind::BracedInitList)
	{
		SemaConstant units;
		if (analyzer_.types_.kind(bare) == TypeKind::Array &&
		    string_constant(bare, clause, ctx, units))
		{
			// 8.5.2p1: the clause is a string literal for an array of character
			// type, so what the elements hold is its code units and the clause
			// is no expression this reading converts.
			return units;
		}
		SemaConstant value = operand_constant(clause, ctx);
		if (value.valued && analyzer_.types_.strip_cv(value.type) == bare)
		{
			// 8.5p14: the clause is a prvalue of the subobject's own type, so
			// the subobject *is* that value and no constructor stands between.
			value.type = bare;
			return value;
		}
		if (analyzer_.types_.is_class(bare))
		{
			// 8.5p16 with 13.3.1.4: a clause of another type copy-initializes
			// the subobject, which is the one reading every place of class type
			// filled from one value asks - a converting constructor of the
			// subobject's class, 4.10p3's base class subobject, or a conversion
			// function of the clause's own class.
			return at_class_place(value, bare);
		}
		value = analyzer_.convert(value, bare);
		value.type = bare;
		return value;
	}
	const bool array = analyzer_.types_.kind(bare) == TypeKind::Array;
	// 8.5.1p1: a class that declares a constructor of its own takes the clauses
	// as 13.3.1.7's arguments, whose types are the ones the chosen constructor's
	// places have and not the members' - so those are read as expressions and
	// `object_of` is what puts them where they go.  Which of the two a class
	// template's specialization is, its definition says, and a fold reaching one
	// may be the first use there has been of it: without the ask below every
	// specialization answers "no aggregate", and the nested clause written for
	// a member of class or array type is then read as an expression.
	if (analyzer_.types_.is_class(bare))
	{
		ask_for_definition(bare);
	}
	const bool aggregate =
		analyzer_.types_.is_class(bare) && analyzer_.aggregate_type(bare);
	if (array || aggregate)
	{
		return list_constant(bare, clause, ctx);
	}
	std::vector<SemaConstant> written;
	InitializerClauses clauses(&clause, analyzer_, ctx);
	if (clauses.list.unsettled())
	{
		// 14.6p8: a run no argument list has settled says neither how many
		// clauses there are nor what they are worth.
		throw NotConstant("a constant expression writes a list of clauses an "
		                  "argument list has yet to settle", false);
	}
	while (!clauses.spent())
	{
		// 8.5.1p1 and 13.3.1.7: what the clauses of a class that is no aggregate
		// are, are the arguments of one of its constructors, whose types the
		// chosen declaration gives and the members do not.
		const SemaContext inner = clauses.in(ctx);
		const AstNode& one = clauses.next();
		++clauses.at;
		written.push_back(operand_constant(one, inner));
	}
	if (analyzer_.types_.is_class(bare))
	{
		if (written.size() == 1 &&
		    analyzer_.types_.strip_cv(written[0].type) == bare)
		{
			// 8.5p14 again, written with braces around it.
			SemaConstant value = written[0];
			value.type = bare;
			return value;
		}
		return object_of(bare, written);
	}
	// 8.5.1p2: a scalar initialized from a list holds what its one clause says,
	// or zero where the list is empty.
	if (written.size() > 1)
	{
		throw NotConstant("a constant expression writes more than one clause for "
		                  + analyzer_.types_.description(bare));
	}
	SemaConstant value;
	value.type = bare;
	if (!written.empty())
	{
		value = analyzer_.convert(written[0], bare);
		value.type = bare;
	}
	return value;
}

// 8.5.1p2 and 8.5.2p1: the whole of one list, read for the aggregate it
// initializes.
//
// This is where the list's own braces stand, so the two questions the walk of a
// cursor cannot ask are asked here: whether the one clause in them is 8.5.2p1's
// string literal for the whole array, and whether every clause reached a
// subobject.  It stands wherever a list does - a declaration's initializer, a
// clause of an enclosing one, a mem-initializer, a brace-or-equal-initializer -
// so all four get one answer.
SemaConstant ConstexprReading::list_constant(TypeId bare, const AstNode& list,
                                             const SemaContext& ctx)
{
	InitializerClauses clauses(&list, analyzer_, ctx);
	if (clauses.list.unsettled())
	{
		// 14.6p8: a run no argument list has settled says neither how many
		// clauses there are nor what they are worth.
		throw NotConstant("a constant expression writes a list of clauses an "
		                  "argument list has yet to settle", false);
	}
	SemaConstant units;
	if (clauses.list.size() == 1 &&
	    analyzer_.types_.kind(bare) == TypeKind::Array &&
	    string_constant(bare, clauses.next(), clauses.in(ctx), units))
	{
		// 8.5.2p1: the braces hold a string literal for this array, which is the
		// same initialization the array gets where they were left out.
		return units;
	}
	const SemaConstant out = aggregate_constant(bare, clauses, ctx);
	if (!clauses.spent())
	{
		// 8.5.1p6: a clause that reached no subobject initializes nothing.
		throw NotConstant("a constant expression writes more initializers than " +
		                  analyzer_.types_.description(bare) + " has subobjects");
	}
	return out;
}

// 8.5.1p2 over one aggregate, against a cursor the caller may still be walking.
//
// The walk is the *subobjects*' and not the clauses': 8.5.1p11 lets the braces
// around a subaggregate's own clauses be left out, and then the clauses standing
// in the enclosing list initialize its subobjects rather than it - so how many
// of them a subobject takes is what its own walk arrives at and no count is
// worked out in front of it.  Which is the same walk `aggregate_subobject`
// makes where a line is being written for each subobject; here nothing is
// written and the entries are interned instead.
SemaConstant ConstexprReading::aggregate_constant(TypeId bare,
                                                  InitializerClauses& clauses,
                                                  const SemaContext& ctx)
{
	std::vector<SemaConstant> written;
	if (analyzer_.types_.kind(bare) == TypeKind::Array)
	{
		const TypeId element = analyzer_.types_.target(bare);
		const unsigned long long bound = analyzer_.types_.bounded(bare)
			? analyzer_.types_.bound(bare)
			: kUnboundedElements;
		for (unsigned long long index = 0; index < bound && !clauses.spent();
		     ++index)
		{
			written.push_back(subobject_constant(element, clauses, ctx));
		}
		return array_of(bare, written);
	}
	std::vector<Subobject> members;
	subobjects(bare, members);
	for (std::size_t index = 0; index < members.size() && !clauses.spent();
	     ++index)
	{
		written.push_back(subobject_constant(members[index].type, clauses, ctx));
		if (analyzer_.one_storage(bare))
		{
			// 8.5.1p15: a union is initialized by its first member alone.
			break;
		}
	}
	return object_of(bare, written);
}

// 8.5.1p2 and p11: one subobject of that walk, and the clauses it takes.
//
// Three readings, and which one it is, is the clause standing here read against
// the subobject's own type.  Braces written around it are the whole of the
// subobject, whatever it is.  A string literal standing for an array of
// character type is 8.5.2p1's initialization and no clause at all.  Anything
// else at a subobject that is itself an aggregate is a clause 8.5.1p11's elided
// braces left standing for that aggregate's *own* first subobject - which is
// the question `elides_its_braces` answers for a class, taking 5.2.3p3's
// `T{...}` and a value of the subobject's own class out of it, and which for an
// array is the whole of the answer, because no expression of array type
// initializes one.
SemaConstant ConstexprReading::subobject_constant(TypeId type,
                                                  InitializerClauses& clauses,
                                                  const SemaContext& ctx)
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	const SemaContext inner = clauses.in(ctx);
	const AstNode& one = clauses.next();
	if (one.kind == AstKind::BracedInitList)
	{
		++clauses.at;
		return clause_of(one, bare, inner);
	}
	if (analyzer_.types_.kind(bare) == TypeKind::Array)
	{
		SemaConstant units;
		if (string_constant(bare, one, inner, units))
		{
			++clauses.at;
			return units;
		}
		return aggregate_constant(bare, clauses, ctx);
	}
	if (analyzer_.types_.is_class(bare))
	{
		ask_for_definition(bare);
		if (analyzer_.elides_its_braces(bare, clauses, ctx))
		{
			return aggregate_constant(bare, clauses, ctx);
		}
	}
	++clauses.at;
	return clause_of(one, bare, inner);
}

// 8.5.2p1: an array of character type initialized by a string literal holds the
// literal's own code units, and 8.5.1p7 value-initializes every element past
// them.  Which units those are is `StringInitialization`'s one reading, asked
// here for the values rather than for the elements a line is written for.
bool ConstexprReading::string_constant(TypeId array, const AstNode& written,
                                       const SemaContext& ctx,
                                       SemaConstant& out)
{
	std::vector<unsigned long long> units;
	if (!StringInitialization(analyzer_).units_of(array, written, ctx, units))
	{
		return false;
	}
	const TypeId element =
		analyzer_.types_.strip_cv(analyzer_.types_.target(array));
	std::vector<SemaConstant> held;
	held.reserve(units.size());
	for (std::size_t index = 0; index < units.size(); ++index)
	{
		SemaConstant unit;
		unit.type = element;
		unit.bits = units[index];
		held.push_back(unit);
	}
	out = array_of(array, held);
	return true;
}

// 8.5.1p2 and p7 over an array, which 8.5.1p1 makes an aggregate whatever its
// element type is.  The elements stand in one order that no lookup settles, so
// the clause an element takes is the one at its own index and the elements past
// the written clauses are value-initialized - which for an element of class
// type is the object every one of *its* members is value-initialized in.
SemaConstant ConstexprReading::array_of(TypeId type,
                                        const std::vector<SemaConstant>& written)
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	const TypeId element = analyzer_.types_.strip_cv(
		analyzer_.types_.target(bare));
	// 8.3.4p1 and 8.5.1p4: a declaration that wrote no bound takes the one its
	// clauses give it, and by the time a fold reads the declaration the type
	// already carries it.
	const unsigned long long count = analyzer_.types_.bounded(bare)
		? analyzer_.types_.bound(bare)
		: written.size();
	if (written.size() > count)
	{
		throw NotConstant("a constant expression writes more initializers than " +
		                  analyzer_.types_.description(bare) + " has elements");
	}
	if (count > written.size() && count - written.size() > kMaxConstexprSteps)
	{
		// 8.5p7: every element no clause reached is value-initialized, and each
		// is an entry of the list - so an array whose bound names more of them
		// than an evaluation may run is one this reading refuses rather than
		// builds.
		throw NotConstant("a constant expression value-initializes more array "
		                  "elements than this implementation evaluates", false);
	}
	const bool built = analyzer_.types_.is_class(element);
	// 3.9p10 and 8.3.4p6: an array of literal type is a literal type too, so an
	// element that is itself an array holds a list of its own exactly as one of
	// class type does - which is what a second subscript then reads.
	const bool nested = analyzer_.types_.kind(element) == TypeKind::Array;
	if (!valued_subobject(element))
	{
		throw NotConstant("an element of " +
		                  analyzer_.types_.description(bare) +
		                  " is outside the values a constant expression holds", false);
	}
	std::vector<TypeId> held;
	held.reserve(static_cast<std::size_t>(count));
	// 8.5p7 again: the value-initialized elements are all one value, so the
	// entry they intern as is worked out once and repeated.
	TypeId zero = kNoType;
	for (unsigned long long index = 0; index < count; ++index)
	{
		if (index >= written.size())
		{
			if (zero == kNoType)
			{
				const std::vector<SemaConstant> none;
				SemaConstant value;
				value.type = element;
				if (built)
				{
					value = object_of(element, none);
				}
				else if (nested)
				{
					// 8.5.1p7: an element that is an array is
					// value-initialized by value-initializing each of *its*
					// elements, which is this same reading one level down.
					value = array_of(element, none);
				}
				zero = entry_of(value);
			}
			held.push_back(zero);
			continue;
		}
		SemaConstant value = written[static_cast<std::size_t>(index)];
		if (analyzer_.types_.strip_cv(value.type) != element)
		{
			value = analyzer_.convert(value, element);
		}
		value.type = element;
		held.push_back(entry_of(value));
	}
	SemaConstant out;
	out.type = bare;
	out.bits = analyzer_.types_.type_list(held);
	return out;
}

SemaConstant ConstexprReading::element_value(const SemaConstant& array,
                                             unsigned long long index)
{
	if (!array.valued && array.object != 0)
	{
		// 2.14.5p8's literal and every other array a fold designates without
		// holding a list for it: which element the subscript names is the same
		// question, and the answer is read out of the object.
		return loaded(subobject_address(array.object, index));
	}
	if (!holds_list(array) || is_object(array))
	{
		throw NotConstant("a constant expression subscripts something that is "
		                  "not an array or a string literal", false);
	}
	const std::vector<TypeId>& held =
		analyzer_.types_.type_list_at(static_cast<std::uint32_t>(array.bits));
	if (index >= held.size())
	{
		// 5.19p2: reading outside the array is undefined behaviour, which no
		// constant expression holds.
		throw NotConstant("a constant expression subscripts an array outside "
		                  "its bounds");
	}
	SemaConstant out =
		constant_of(held[static_cast<std::size_t>(index)], analyzer_.types_);
	if (array.object != 0)
	{
		// 3.10p1: the element of an array an expression designated is an object
		// the expression designates too, which is what `&a[1]` then reads.
		out.object = subobject_address(array.object, index);
	}
	return out;
}

void ConstexprReading::mem_initializers(
	const AstNode* initializers, SemaEntity& owner, const SemaContext& inner,
	std::unordered_map<std::string, WrittenMemInitializer>& out)
{
	PackReading packs(analyzer_);
	for (std::size_t at = 0;
	     initializers != nullptr && at < initializers->children.size(); ++at)
	{
		const AstNode& one = *initializers->children[at];
		const AstNode* const id = child_kind(one, AstKind::MemInitializerId);
		if (id == nullptr || one.children.size() < 2)
		{
			continue;
		}
		WrittenMemInitializer wrote;
		wrote.written = one.children[1];
		wrote.region = nullptr;
		if (child_kind(one, AstKind::ParameterPack) == nullptr)
		{
			// 12.6.2p3 leaves one mem-initializer per subobject, so a second
			// entry of one name is one the class already refused.
			out.insert(std::make_pair(
				analyzer_.base_key(id->text, inner, &owner), wrote));
			continue;
		}
		// 14.5.3p4: the mem-initializer is a pattern read once per element of
		// the run its packs are bound to, and which base each element names is
		// the id read in that element's own region - so the index gains one
		// entry per element, each carrying the region its clauses are read in.
		const PackReading::Run run = packs.run_of_node(one, inner);
		if (!run.found || !run.settled)
		{
			throw NotConstant("a mem-initializer of " +
			                  analyzer_.types_.description(owner.type) +
			                  " is expanded over a run no argument list has "
			                  "settled", false);
		}
		for (std::size_t element = 0; element < run.length; ++element)
		{
			SemaContext where = inner;
			where.scope = &packs.element_region(run, element, inner);
			wrote.region = where.scope;
			out.insert(std::make_pair(
				analyzer_.base_key(id->text, where, &owner), wrote));
		}
	}
}

// 12.6.2p10 over one subobject of the class being built.
//
// A base class subobject and a member of class type are read the same way, and
// they are the two things a mem-initializer's clauses are 13.3.1.3's arguments
// of rather than one value: `Payload(p)` and `held(1, 2)` are each an object
// built from what the parentheses hold.  What no mem-initializer names is
// 12.6.2p8's brace-or-equal-initializer where the declaration wrote one and
// 8.5p6's default-initialization where it did not - which for a class is its
// own default constructor and for anything else is no value 5.19 reads.
//
// 3.9p10 and 8.3.4p6 make a member of *array* type a subobject holding a list
// of its own exactly as one of class type does, so it is neither of the two: a
// mem-initializer's clauses are 8.5.1p2's elements and not 13.3.1.3's
// arguments, and none of them is one value `convert` reaches.  Both directions
// of that are `array_of`'s, which is the same reading `object_of`'s aggregate
// arm and `clause_of` already give an array written anywhere else.
SemaConstant ConstexprReading::subobject_initialized(
	TypeId bare, const Subobject& held, const WrittenMemInitializer* wrote,
	const SemaContext& inner)
{
	const TypeId type = analyzer_.types_.strip_cv(held.type);
	const bool built = held.base || analyzer_.types_.is_class(type);
	const bool listed =
		!held.base && analyzer_.types_.kind(type) == TypeKind::Array;
	SemaConstant value;
	value.type = type;
	if (wrote != nullptr)
	{
		SemaContext where = inner;
		if (wrote->region != nullptr)
		{
			// 14.5.3p4: the clauses of one element of an expansion are read
			// where that element's own reading of the pattern stands.
			where.scope = wrote->region;
		}
		const AstNode& list = *wrote->written;
		if (built)
		{
			ask_for_definition(type);
			if (list.kind == AstKind::BracedInitList &&
			    analyzer_.aggregate_type(type))
			{
				// 12.6.2p2 with 8.5.4p3: braces around the clauses
				// list-initialize the subobject, so an aggregate takes them as
				// 8.5.1p2's clauses down its *own* subobjects - 8.5.1p11's
				// elided braces among them - and not as 13.3.1.3's arguments of
				// a constructor it has not got.
				return list_constant(type, list, where);
			}
			std::vector<SemaConstant> clauses;
			clauses.reserve(list.children.size());
			for (std::size_t at = 0; at < list.children.size(); ++at)
			{
				clauses.push_back(operand_constant(*list.children[at], where));
			}
			return object_of(type, clauses);
		}
		if (listed)
		{
			// 12.6.2p2 with 8.5.1p2: the clauses a mem-initializer writes for a
			// member of array type initialize its *elements* and are no
			// constructor's arguments - so what stands between them and the
			// member is the one list reading, 8.5.1p11's elided braces and
			// 8.5.2p1's string literal among it, and 8.5p7's value-initialization
			// where `arr()` or `arr{}` wrote no clause at all.
			return list_constant(type, list, where);
		}
		if (list.children.size() > 1)
		{
			throw NotConstant("a mem-initializer of " +
			                  analyzer_.types_.description(bare) +
			                  " writes more than one value for a member");
		}
		// 8.5p7: `m()` and `m{}` are the value-initialization the member's own
		// type zeroes, which is what an empty list already stands for.
		value.bits = 0;
		if (!list.children.empty())
		{
			value = analyzer_.convert(
				operand_constant(*list.children[0], where), type);
		}
		value.type = type;
		return value;
	}
	if (!held.base && held.entity->default_initializer &&
	    analyzer_.member_initializers_.count(held.entity->id) != 0)
	{
		// 12.6.2p8 and 9.2p2: a member no mem-initializer names is initialized
		// by the brace-or-equal-initializer its own declaration wrote, read in
		// the class it was written in rather than in the constructor's region.
		// It is the same reading a declaration's own initializer gets, because
		// 8.5 makes it the same initialization - and it is the whole of what
		// 12.1p5's implicitly-defined default constructor does.
		const HeldInitializer& initializer =
			analyzer_.member_initializers_[held.entity->id];
		SemaContext where = inner;
		where.scope = initializer.scope;
		where.dump =
			initializer.scope == nullptr ? inner.dump : initializer.scope->dump;
		return clause_of(*initializer.written, type, where);
	}
	if (built)
	{
		// 12.6.2p4 and p8: a base class subobject and a member of class type no
		// mem-initializer names are default-initialized, which is the object
		// their own default constructor - or 8.5.1p7's zeroes - builds.
		const std::vector<SemaConstant> none;
		return object_of(type, none);
	}
	if (listed && analyzer_.types_.is_class(analyzer_.types_.strip_cv(
		              analyzer_.types_.element_of(type))))
	{
		// 12.6p1 with 12.6.2p4: an array of class type no mem-initializer names
		// is default-initialized, which default-initializes each of its
		// elements - the same constructor once per element, and one answer
		// because the elements are all the same object.  An array of anything
		// else is initialized with nothing at all, which is the refusal below.
		const std::vector<SemaConstant> none;
		return array_of(type, none);
	}
	// 7.1.5p4: every non-static data member of a class a constexpr constructor
	// builds shall be initialized, and one left default-initialized holds no
	// value a constant expression reads.
	throw NotConstant("a constexpr constructor of " +
	                  analyzer_.types_.description(bare) +
	                  " initializes no value for " + held.entity->name);
}

// 8.5p16 with 12.1 and 12.6.2: the object `T(x)` and `T{x}` come to where the
// class is no aggregate.
//
// 13.3.1.3 chooses the constructor from the class's own declarations, ranked
// over the constants the clauses came to exactly as a call of a function is,
// and what the object holds is what each mem-initializer of that constructor
// comes to - read in a region of its own binding the places to what the
// arguments came to.  12.6.2p10 initializes in declaration order
// whatever order the mem-initializers were written in, so a member already
// settled is a binding of that region too and one written after it may name it;
// 8.3.5p10's place of the same name is the one 3.3.7 leaves standing.  The
// answer is a fact of the constructor and the converted list exactly as a call
// of a function is, so it is held under the same key.
SemaConstant ConstexprReading::object_from_constructor(
	TypeId bare, SemaEntity& owner, const std::vector<SemaConstant>& written)
{
	if (owner.constructor == nullptr)
	{
		throw NotConstant(analyzer_.types_.description(bare) +
		                  " declares no constructor a constant expression "
		                  "builds an object with", false);
	}
	std::vector<AnalyzedValue> clauses;
	argument_values(written, clauses);
	// 13.3.1.3p1: the candidate set is the constructors of the class alone, and
	// 12.1 gives each of them the object it constructs as its first parameter -
	// which 13.3.1p3 offers as the implicit object argument here as it does for
	// any other member.
	SemaConstant constructed;
	constructed.type = bare;
	const AnalyzedValue self = object_value(constructed);
	SemaEntity* const one = &selected(analyzer_.types_.description(bare),
	                                  std::vector<SemaEntity*>(
		                                  1, owner.constructor),
	                                  clauses, &self, 0);
	// 12.1p5 and 8.4.2p1: a constructor the standard defined has no body this
	// reading walks and no ctor-initializer it wrote - what it initializes each
	// member with is 12.6.2p8's brace-or-equal-initializer, and whether doing
	// that leaves a constant is the `constexpr_function` its class settled.
	const bool wrote_body = one->constexpr_region != nullptr &&
		one->constexpr_body != nullptr;
	if (!wrote_body && written.size() == 1 && one->defaulted &&
	    (one->transfer == kCopyConstructorTransfer ||
	     one->transfer == kMoveConstructorTransfer))
	{
		// 12.8p15 and 12.8p26: the copy constructor the standard defines copies
		// each subobject of its argument, and a constant of class type *is* the
		// list of what its subobjects hold - so the object it builds is that one
		// value, and 4.10p3's base class subobject is the entry the argument's
		// own list holds where the argument is of a class derived from this one.
		const TypeId held = analyzer_.types_.strip_cv(written[0].type);
		SemaEntity* const base =
			held == bare ? nullptr : Derivation(analyzer_).base_in(held, bare);
		if (held == bare || base != nullptr)
		{
			SemaConstant out =
				base == nullptr ? written[0] : base_subobject(written[0], *base);
			out.type = bare;
			return out;
		}
	}
	if (!wrote_body && !(one->defaulted && one->constexpr_function))
	{
		// A constructor the program wrote and did not write `constexpr` on is
		// 7.1.5p9's own answer about the declaration.  One the standard defined
		// that 12.1p5 leaves short of a constexpr constructor is this reading's
		// edge instead: 8.5p7 zero-initializes the object before such a
		// constructor runs, and the zeroes it lays down are what this walk has
		// no arm for - so `struct X { private: int v; }; constexpr X x = X();`
		// is a program both oracles build and one no refusal here describes.
		throw NotConstant(analyzer_.types_.description(bare) +
		                  " declares no one constexpr constructor a constant "
		                  "expression builds an object with",
		                  !one->defaulted);
	}
	// 7.1.5p4: a constexpr constructor's function-body shall be a
	// compound-statement holding what 7.1.5p3 leaves any other one - so what
	// the object comes to is the mem-initializers and nothing the body runs.
	const AstNode* const body =
		wrote_body ? child_kind(*one->constexpr_body, AstKind::CompoundStatement)
		           : nullptr;
	for (std::size_t at = 0;
	     body != nullptr && at < body->children.size(); ++at)
	{
		if (!is_body_declaration(body->children[at]->kind))
		{
			throw NotConstant(analyzer_.types_.description(bare) +
			                  " is built by a constexpr constructor whose body "
			                  "is outside what 7.1.5p4 leaves a constant "
			                  "expression to read");
		}
	}
	std::vector<SemaConstant> passed;
	const std::uint32_t list = passed_arguments(*one, nullptr, written, passed);
	const TypeId held = analyzer_.model_.folded_call(*one, list);
	if (held != kNoType)
	{
		return constant_of(held, analyzer_.types_);
	}
	unsigned& depth = analyzer_.model_.folding_depth();
	if (depth >= kMaxConstexprDepth)
	{
		throw NotConstant("a constant expression builds objects more deeply "
		                  "than this implementation reads", false);
	}
	const ReadingDepth building(depth);
	// 9.2p2 and 12.6.2p2: the mem-initializers are read where the constructor's
	// parameters stand.  A constructor the standard defined wrote none, so the
	// region the brace-or-equal-initializers of 12.6.2p8 are read in is the
	// class's own - which is where the program wrote them.
	Scope& around = wrote_body ? *one->constexpr_region : *owner.scope;
	SemaContext inner;
	inner.scope = &analyzer_.model_.open(ScopeKind::Block, around, nullptr,
	                                     around.dump);
	inner.dump = around.dump;
	bind_arguments(*one, nullptr, passed, inner);
	std::unordered_map<std::string, WrittenMemInitializer> written_for;
	if (wrote_body)
	{
		mem_initializers(
			child_kind(*one->constexpr_body, AstKind::CtorInitializer), owner,
			inner, written_for);
	}
	std::vector<Subobject> members;
	subobjects(bare, members);
	std::vector<TypeId> holds;
	holds.reserve(members.size());
	for (std::size_t index = 0; index < members.size(); ++index)
	{
		const Subobject& held = members[index];
		// 12.6.2p2: a mem-initializer-id that names a direct base is held under
		// the whole name of that class and every other under the member's own,
		// which is the keying `base_key` gave the index above.
		const std::unordered_map<std::string, WrittenMemInitializer>::const_iterator
			found = written_for.find(
				held.base ? analyzer_.types_.user_name(held.type)
				          : held.entity->name);
		const SemaConstant value = subobject_initialized(
			bare, held, found == written_for.end() ? nullptr : &found->second,
			inner);
		if (!valued_subobject(held.type))
		{
			throw NotConstant("a subobject of " +
			                  analyzer_.types_.description(bare) +
			                  " is outside the values a constant expression "
			                  "holds", false);
		}
		holds.push_back(entry_of(value));
		if (!held.base && inner.scope->names.count(held.entity->name) == 0)
		{
			// 12.6.2p10: a member already initialized is one a later
			// mem-initializer reads, and what the object holds for it is
			// settled by that initialization rather than written again.
			bind_constant(held.entity->name, value, inner, false);
		}
	}
	SemaConstant out;
	out.type = bare;
	out.bits = analyzer_.types_.type_list(holds);
	analyzer_.model_.hold_folded_call(*one, list, entry_of(out));
	return out;
}

// 5.2.5p1: the object a member access is written on.
//
// A constant expression holds an object only as 5.2.3p2/p3's object of literal
// class type, so what may stand to the left of the `.` is one of those - and
// 5.2.5p1's other form, `->`, has a pointer there, which 5.19p2 leaves a
// constant expression none of.
SemaConstant ConstexprReading::accessed_object(const AstNode& node,
                                               const SemaContext& ctx)
{
	if (node.children.size() < 2 ||
	    (node.token != OP_DOT && node.token != OP_ARROW))
	{
		throw NotConstant("a constant expression reads a member through "
		                  "something it holds no object of", false);
	}
	if (node.token == OP_ARROW)
	{
		// 5.2.5p2: `E->m` is `(*E).m`, so the object is the one the left
		// operand points to - which 5.19p2 now holds an address of.  13.5.6p1's
		// `operator->` is reached the same way every other operator is, from an
		// operand of class type, and what it hands back is that pointer.
		SemaConstant pointer = analyzer_.evaluate(*node.children[0], ctx);
		while (overloadable_operand(pointer))
		{
			const std::vector<SemaConstant> one(1, pointer);
			SemaConstant called;
			if (!operator_constant(OP_ARROW, one, ctx, called))
			{
				break;
			}
			pointer = called;
		}
		return loaded(pointed_object(pointer));
	}
	const SemaConstant object = analyzer_.evaluate(*node.children[0], ctx);
	if (!is_object(object))
	{
		throw NotConstant("a constant expression reads a member of what is not "
		                  "an object of class type", false);
	}
	return object;
}

SemaEntity* ConstexprReading::accessed_member(const SemaConstant& object,
                                              const std::string& name,
                                              const SemaContext& ctx,
                                              std::vector<SemaEntity*>* found)
{
	SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(object.type));
	if (owner == nullptr || owner->scope == nullptr)
	{
		return nullptr;
	}
	// 5.2.5p1 is one lookup, and the expression layer already writes it - so a
	// name found through 10.2's chain, a using-declaration or a qualified-id is
	// found here the same way it is anywhere else.
	std::vector<SemaEntity*> reached;
	return analyzer_.member_named(*owner->scope, name, ctx,
	                              found == nullptr ? reached : *found);
}

// 5.2.5p1 over 9.2p1's non-static data member: the subobject the object's own
// interned list holds for it, which is the entry standing where 9.2p13 declared
// the member.
SemaConstant ConstexprReading::member_value(const SemaConstant& object,
                                            const std::string& name,
                                            const SemaContext& ctx)
{
	if (!is_object(object))
	{
		throw NotConstant("a constant expression reads a member of what is not "
		                  "an object of class type", false);
	}
	SemaEntity* const named = accessed_member(object, name, ctx);
	if (named != nullptr && !named->object_member &&
	    named->kind != SemaKind::Function)
	{
		// 9.4p2 with 5.2.5p1: a member declared `static` is a member of the
		// class and no subobject of any object of it - the object expression is
		// evaluated and its value discarded, and what the access names is that
		// one object.  So the access is worth what the declaration is worth,
		// which is the reading an id-expression naming it asks; the subobject
		// list below holds 9.2p1's members and never this one.
		return entity_constant(*named, name);
	}
	std::vector<unsigned long long> path;
	if (named == nullptr || !member_path(object.type, *named, path))
	{
		throw NotConstant(name +
		                  " names no subobject a constant expression reads of " +
		                  analyzer_.types_.description(object.type), false);
	}
	// 10.2: a name the lookup found in a base is reached through the entry the
	// base class subobject stands at, so the walk down is one step per class on
	// the path and the last of them is 9.2p13's member.
	SemaConstant out = object;
	for (std::size_t at = 0; at < path.size(); ++at)
	{
		out = subobject_value(out, path[at]);
	}
	return out;
}

// 12.6.2p10 and 9.2p13: which entries of an object of `type` reach the
// subobject `named` declares.
//
// The class's own members are asked first and its bases after them, which is
// 10.2's hiding: a member of the derived class and one of that name in a base
// are two subobjects, and the lookup that found this declaration already said
// which of the two it is - what is left here is to reach the entry.
bool ConstexprReading::member_path(TypeId type, SemaEntity& named,
                                   std::vector<unsigned long long>& path)
{
	std::vector<Subobject> held;
	subobjects(type, held);
	SemaEntity& declared = SemaAnalyzer::declared_member(named);
	for (std::size_t index = 0; index < held.size(); ++index)
	{
		if (!held[index].base &&
		    (held[index].entity == &named || held[index].entity == &declared))
		{
			path.push_back(index);
			return true;
		}
	}
	for (std::size_t index = 0; index < held.size(); ++index)
	{
		if (!held[index].base)
		{
			continue;
		}
		path.push_back(index);
		if (member_path(held[index].type, named, path))
		{
			return true;
		}
		path.pop_back();
	}
	return false;
}

SemaConstant ConstexprReading::subobject_value(const SemaConstant& object,
                                               unsigned long long index)
{
	if (!holds_list(object))
	{
		throw NotConstant("a constant expression reads a subobject of " +
		                  analyzer_.types_.description(object.type) +
		                  ", which holds none", false);
	}
	const std::vector<TypeId>& held =
		analyzer_.types_.type_list_at(static_cast<std::uint32_t>(object.bits));
	if (index >= held.size())
	{
		// 9.5p1: a union's list holds the member whose lifetime its
		// initialization began and stops there, so every entry past it is a
		// member that holds nothing - reading one is 5.19's own answer about
		// the program and not this reading running out of a value.
		throw NotConstant(
			analyzer_.one_storage(object.type)
				? "a constant expression reads a member of " +
					analyzer_.types_.description(object.type) +
					" other than the one it holds"
				: "a constant expression reads outside " +
					analyzer_.types_.description(object.type));
	}
	SemaConstant out =
		constant_of(held[static_cast<std::size_t>(index)], analyzer_.types_);
	if (object.object != 0)
	{
		// 3.10p1 and 9.2p13: the subobject of an object an expression designated
		// is one it designates too, which is what `&s.m`, a member of array type
		// decaying, and a reference bound to a base subobject each read.
		out.object = subobject_address(object.object, index);
	}
	return out;
}

// 4.10p3: the base class subobject of `value` an object of the class `base`
// names, reached by the steps the derivation says lie between them.
SemaConstant ConstexprReading::base_subobject(const SemaConstant& value,
                                              SemaEntity& base)
{
	std::vector<unsigned long long> path;
	if (!Derivation(analyzer_).subobject_path(value.type, base, path))
	{
		throw NotConstant(analyzer_.types_.description(value.type) +
		                  " holds no subobject of " +
		                  analyzer_.types_.description(base.type), false);
	}
	SemaConstant out = value;
	for (std::size_t at = 0; at < path.size(); ++at)
	{
		out = subobject_value(out, path[at]);
	}
	return out;
}

// 5.2.2p1 with 9.3.1p3: the call `E.f(args)`, whose object is `E` where 9.2p1
// leaves `f` a non-static member and nothing at all where 9.4p1 makes it
// static.
SemaConstant ConstexprReading::member_call(
	const SemaConstant& object, const std::string& name,
	const std::vector<SemaConstant>& arguments, const SemaContext& ctx)
{
	if (!is_object(object))
	{
		throw NotConstant("a constant expression calls a member of what is not "
		                  "an object of class type", false);
	}
	std::vector<SemaEntity*> candidates;
	SemaEntity* const named = accessed_member(object, name, ctx, &candidates);
	if (named == nullptr || named->kind != SemaKind::Function)
	{
		throw NotConstant(name +
		                  " names no member function a constant expression "
		                  "calls of " +
		                  analyzer_.types_.description(object.type));
	}
	if (candidates.empty())
	{
		candidates.push_back(named);
	}
	std::vector<AnalyzedValue> written;
	argument_values(arguments, written);
	// 13.3.1p3: the object is an argument of the call like any other, and the
	// declaration 13.3 chooses is what says whether it is passed - 9.4p1's
	// static member takes no object, and 13.3.1p4 makes every other candidate
	// none at all where there is none.
	const AnalyzedValue self = object_value(object);
	SemaEntity& one = selected(name, candidates, written, &self, 0);
	return call(one, one.object_member ? &object : nullptr, arguments);
}

SemaConstant ConstexprReading::member_constant(const AstNode& node,
                                               const SemaContext& ctx)
{
	return member_value(accessed_object(node, ctx), node.children[1]->text, ctx);
}

SemaConstant ConstexprReading::member_called(
	const AstNode& callee, const std::vector<SemaConstant>& arguments,
	const SemaContext& ctx)
{
	return member_call(accessed_object(callee, ctx), callee.children[1]->text,
	                   arguments, ctx);
}
