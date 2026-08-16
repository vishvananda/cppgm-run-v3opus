#include "sema_constexpr.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_argument_lookup.h"
#include "sema_pack.h"
#include "sema_reading.h"
#include "sema_scope.h"

namespace
{

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

bool add_overflows(long long left, long long right, long long& out)
{
	const unsigned long long sum = static_cast<unsigned long long>(left) +
		static_cast<unsigned long long>(right);
	out = static_cast<long long>(sum);
	return ((left ^ out) & (right ^ out)) < 0;
}

bool subtract_overflows(long long left, long long right, long long& out)
{
	const unsigned long long difference = static_cast<unsigned long long>(left) -
		static_cast<unsigned long long>(right);
	out = static_cast<long long>(difference);
	return ((left ^ right) & (left ^ out)) < 0;
}

bool multiply_overflows(long long left, long long right, long long& out)
{
	out = static_cast<long long>(static_cast<unsigned long long>(left) *
	                             static_cast<unsigned long long>(right));
	if (left == 0 || right == 0)
	{
		out = 0;
		return false;
	}
	const long long lowest = -1 - 0x7FFFFFFFFFFFFFFFLL;
	if (left == -1)
	{
		return right == lowest;
	}
	if (right == -1)
	{
		return left == lowest;
	}
	return out / left != right;
}
// 7.1.5p3: the declarations a constexpr function's body may write around its
// one return statement.  They declare no object and run nothing, so a fold
// reads them for the names they introduce and for nothing else.
bool is_body_declaration(AstKind kind)
{
	return kind == AstKind::SimpleDeclaration ||
		kind == AstKind::AliasDeclaration ||
		kind == AstKind::UsingDeclaration ||
		kind == AstKind::UsingDirective ||
		kind == AstKind::StaticAssertDeclaration ||
		kind == AstKind::EmptyDeclaration;
}

// 8.3p1: the declarator-id a declarator was written around, which is the
// identifier under whatever ptr-operators and suffixes stand between - so
// `int x`, `const T& x` and `T (*x)[4]` are all read for the one name.  A
// nested declarator holds it, and 8.3.5p10's unnamed place holds none.
const AstNode* written_name(const AstNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::Identifier)
		{
			return &child;
		}
		if (child.kind == AstKind::NestedDeclarator && !child.children.empty())
		{
			const AstNode* const found = written_name(*child.children[0]);
			if (found != nullptr)
			{
				return found;
			}
		}
	}
	return nullptr;
}

// 8.3.5p10: the names the definition's own declarator gave the entries of its
// parameter-declaration-clause, in the order it wrote them, with `packed`
// saying which of them 14.5.3p4 wrote `pattern...`.  A name is no part of the
// function's type, so it is read from the definition that wrote the body rather
// than from any other declaration of the function; a place the definition left
// unnamed binds nothing, and the body cannot have named it either.
void parameter_names(const AstNode& definition, std::vector<std::string>& out,
                     std::vector<char>& packed)
{
	const AstNode* declarator = nullptr;
	for (std::size_t index = 0; index < definition.children.size(); ++index)
	{
		if (definition.children[index]->kind == AstKind::Declarator)
		{
			declarator = definition.children[index];
			break;
		}
	}
	if (declarator == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < declarator->children.size(); ++index)
	{
		const AstNode& part = *declarator->children[index];
		if (part.kind != AstKind::ParameterClause)
		{
			continue;
		}
		for (std::size_t at = 0; at < part.children.size(); ++at)
		{
			const AstNode& place = *part.children[at];
			if (place.kind != AstKind::ParameterDeclaration)
			{
				return;
			}
			std::string named;
			bool dots = false;
			for (std::size_t which = 0; which < place.children.size(); ++which)
			{
				const AstNode& held = *place.children[which];
				if (held.kind != AstKind::Declarator)
				{
					continue;
				}
				const AstNode* const id = written_name(held);
				if (id != nullptr)
				{
					named = id->text;
				}
				// 14.5.3p4: the `...` stands in the declarator, between the
				// pattern's specifiers and the name the places are given.
				dots = dots ||
					child_kind(held, AstKind::ParameterPack) != nullptr;
			}
			out.push_back(named);
			packed.push_back(dots ? 1 : 0);
		}
		return;
	}
}

}

ConstexprReading::ConstexprReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

bool ConstexprReading::is_object(const SemaConstant& value) const
{
	return value.type != kNoType &&
		analyzer_.types_.is_class(analyzer_.types_.strip_cv(value.type));
}

bool ConstexprReading::holds_list(const SemaConstant& value) const
{
	return is_object(value) ||
		(value.type != kNoType &&
		 analyzer_.types_.kind(analyzer_.types_.strip_cv(value.type)) ==
			 TypeKind::Array);
}

// The entry a constant interns as.  3.9.1p8's two kinds of arithmetic value are
// held differently and interned the same way: an entry is a type and a number,
// and for a floating value that number is the pool index the table gives it,
// because no `unsigned long long` holds an x87 `long double`.
TypeId ConstexprReading::entry_of(const SemaConstant& value) const
{
	const TypeId arithmetic = analyzer_.arithmetic_type(value.type);
	if (arithmetic != kNoType && analyzer_.types_.is_floating(arithmetic))
	{
		return analyzer_.types_.real_type(value.type, value.real);
	}
	return analyzer_.types_.value_type(value.type, value.bits);
}

SemaConstant ConstexprReading::constant_of(TypeId entry, const TypeTable& types)
{
	SemaConstant out;
	out.type = types.target(entry);
	if (types.is_floating(out.type))
	{
		out.real = types.value_real(entry);
		return out;
	}
	out.bits = types.value_bits(entry);
	return out;
}

void ConstexprReading::data_members(TypeId type,
                                    std::vector<SemaEntity*>& out) const
{
	SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(type));
	if (owner == nullptr || owner->scope == nullptr)
	{
		return;
	}
	const Scope& region = *owner->scope;
	for (std::size_t index = 0; index < region.declarations.size(); ++index)
	{
		SemaEntity& member = *region.declarations[index];
		if (declares_subobject(member, region))
		{
			out.push_back(&member);
		}
	}
}

// 5.2.3p2 and p3: an object of literal class type built where a value belongs.
//
// The clauses initialize the non-static data members in declaration order,
// which is 8.5.1p2 exactly, and 8.5.1p7 value-initializes every member none of
// them reached.  What the object comes to is the interned list of what its
// members hold, so the class and that list together are the object: two
// namings of `S{5}` are one entry, and `S{5}` and `S{6}` are two.
SemaConstant ConstexprReading::object_of(TypeId type,
                                         const std::vector<SemaConstant>& written)
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	SemaEntity* owner = analyzer_.model_.type_owner(bare);
	if (owner != nullptr && owner->primary != nullptr && !owner->defined)
	{
		// 14.7.1p1 and 3.9p5: building an object of the class is a use that
		// requires it completely defined, and a spelling met while 5.4p2's
		// ambiguity was being probed made the declaration without asking for
		// one - so the ask is made here, where the use stands.
		analyzer_.asked_specialization(*owner);
	}
	analyzer_.require_complete_type(bare);
	owner = analyzer_.model_.type_owner(bare);
	if (owner == nullptr || owner->scope == nullptr || !owner->bases.empty())
	{
		// 10p1's base subobject is one this milestone's object does not hold:
		// nothing names it, so nothing would read it back.
		throw NotConstant(analyzer_.types_.description(bare) +
		                  " is not a class a constant expression builds an "
		                  "object of");
	}
	if (!analyzer_.aggregate_type(bare))
	{
		// 8.5.1p1: a class that declares a constructor of its own - or that
		// hides a member behind 11p1's access - is no aggregate, so what stands
		// between the clauses and the members is 12.1's constructor and not
		// 8.5.1p2's one-clause-per-member.
		return object_from_constructor(bare, *owner, written);
	}
	std::vector<SemaEntity*> members;
	data_members(bare, members);
	if (written.size() > members.size())
	{
		throw NotConstant("a constant expression writes more initializers than " +
		                  analyzer_.types_.description(bare) + " has members");
	}
	std::vector<TypeId> held;
	held.reserve(members.size());
	for (std::size_t index = 0; index < members.size(); ++index)
	{
		const TypeId member = analyzer_.types_.strip_cv(members[index]->type);
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
		if (!analyzer_.types_.is_class(member) &&
		    analyzer_.arithmetic_type(member) == kNoType)
		{
			throw NotConstant("a member of " +
			                  analyzer_.types_.description(bare) +
			                  " is outside the values a constant expression "
			                  "holds");
		}
		held.push_back(entry_of(value));
	}
	SemaConstant out;
	out.type = bare;
	out.bits = analyzer_.types_.type_list(held);
	return out;
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
		                  "elements than this implementation evaluates");
	}
	const bool built = analyzer_.types_.is_class(element);
	if (!built && analyzer_.arithmetic_type(element) == kNoType)
	{
		throw NotConstant("an element of " +
		                  analyzer_.types_.description(bare) +
		                  " is outside the values a constant expression holds");
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
				zero = entry_of(built ? object_of(element, none) : value);
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
	if (!holds_list(array) || is_object(array))
	{
		throw NotConstant("a constant expression subscripts something that is "
		                  "not an array or a string literal");
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
	return constant_of(held[static_cast<std::size_t>(index)],
	                   analyzer_.types_);
}

void ConstexprReading::mem_initializers(
	const AstNode* initializers,
	std::unordered_map<std::string, const AstNode*>& out)
{
	for (std::size_t at = 0;
	     initializers != nullptr && at < initializers->children.size(); ++at)
	{
		const AstNode& one = *initializers->children[at];
		const AstNode* const id = child_kind(one, AstKind::MemInitializerId);
		// 14.5.3p4: a mem-initializer written as a pattern stands for one per
		// element of a run, which 10p1 leaves to a base subobject - and this
		// object holds none.
		if (id == nullptr || one.children.size() < 2 ||
		    child_kind(one, AstKind::ParameterPack) != nullptr)
		{
			continue;
		}
		// 12.6.2p3 leaves one mem-initializer per member, so a second entry of
		// one name is one the class already refused.
		out.insert(std::make_pair(id->text, one.children[1]));
	}
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
		                  "builds an object with");
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
	if (one->constexpr_region == nullptr || one->constexpr_body == nullptr)
	{
		throw NotConstant(analyzer_.types_.description(bare) +
		                  " declares no one constexpr constructor a constant "
		                  "expression builds an object with");
	}
	// 7.1.5p4: a constexpr constructor's function-body shall be a
	// compound-statement holding what 7.1.5p3 leaves any other one - so what
	// the object comes to is the mem-initializers and nothing the body runs.
	const AstNode* const body =
		child_kind(*one->constexpr_body, AstKind::CompoundStatement);
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
		                  "than this implementation reads");
	}
	const ReadingDepth building(depth);
	SemaContext inner;
	inner.scope = &analyzer_.model_.open(ScopeKind::Block, *one->constexpr_region,
	                                     nullptr, one->constexpr_region->dump);
	inner.dump = one->constexpr_region->dump;
	bind_arguments(*one, nullptr, passed, inner);
	std::unordered_map<std::string, const AstNode*> written_for;
	mem_initializers(child_kind(*one->constexpr_body, AstKind::CtorInitializer),
	                 written_for);
	std::vector<SemaEntity*> members;
	data_members(bare, members);
	std::vector<TypeId> holds;
	holds.reserve(members.size());
	for (std::size_t index = 0; index < members.size(); ++index)
	{
		const TypeId member = analyzer_.types_.strip_cv(members[index]->type);
		const std::unordered_map<std::string, const AstNode*>::const_iterator
			found = written_for.find(members[index]->name);
		const AstNode* const wrote =
			found == written_for.end() ? nullptr : found->second;
		SemaConstant value;
		value.type = member;
		if (wrote != nullptr && analyzer_.types_.is_class(member))
		{
			std::vector<SemaConstant> clauses;
			clauses.reserve(wrote->children.size());
			for (std::size_t at = 0; at < wrote->children.size(); ++at)
			{
				clauses.push_back(analyzer_.evaluate(*wrote->children[at], inner));
			}
			value = object_of(member, clauses);
		}
		else if (wrote != nullptr)
		{
			if (wrote->children.size() > 1)
			{
				throw NotConstant("a mem-initializer of " +
				                  analyzer_.types_.description(bare) +
				                  " writes more than one value for a member");
			}
			// 8.5p7: `m()` and `m{}` are the value-initialization the member's
			// own type zeroes, which is what an empty list already stands for.
			value.bits = 0;
			if (!wrote->children.empty())
			{
				value = analyzer_.convert(
					analyzer_.evaluate(*wrote->children[0], inner), member);
			}
			value.type = member;
		}
		else if (analyzer_.types_.is_class(member))
		{
			// 12.6.2p8: a member no mem-initializer names is default-initialized,
			// which for one of class type is the object its own default
			// constructor - or 8.5.1p7's zeroes - builds.
			const std::vector<SemaConstant> none;
			value = object_of(member, none);
		}
		else
		{
			// 7.1.5p4: every non-static data member of a class a constexpr
			// constructor builds shall be initialized, and one left
			// default-initialized holds no value a constant expression reads.
			throw NotConstant("a constexpr constructor of " +
			                  analyzer_.types_.description(bare) +
			                  " initializes no value for " +
			                  members[index]->name);
		}
		if (!analyzer_.types_.is_class(member) &&
		    analyzer_.arithmetic_type(member) == kNoType)
		{
			throw NotConstant("a member of " +
			                  analyzer_.types_.description(bare) +
			                  " is outside the values a constant expression "
			                  "holds");
		}
		holds.push_back(entry_of(value));
		if (inner.scope->names.count(members[index]->name) == 0)
		{
			// 12.6.2p10: a member already initialized is one a later
			// mem-initializer reads, and what the object holds for it is
			// settled by that initialization rather than written again.
			bind_constant(members[index]->name, value, inner, false);
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
	if (node.token != OP_DOT || node.children.size() < 2)
	{
		throw NotConstant("a constant expression reads a member through "
		                  "something it holds no object of");
	}
	const SemaConstant object = analyzer_.evaluate(*node.children[0], ctx);
	if (!is_object(object))
	{
		throw NotConstant("a constant expression reads a member of what is not "
		                  "an object of class type");
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
		                  "an object of class type");
	}
	SemaEntity* const named = accessed_member(object, name, ctx);
	std::vector<SemaEntity*> members;
	data_members(object.type, members);
	const std::vector<TypeId>& held =
		analyzer_.types_.type_list_at(static_cast<std::uint32_t>(object.bits));
	for (std::size_t index = 0;
	     named != nullptr && index < members.size() && index < held.size();
	     ++index)
	{
		if (members[index] == named ||
		    members[index] == &SemaAnalyzer::declared_member(*named))
		{
			return constant_of(held[index], analyzer_.types_);
		}
	}
	throw NotConstant(name +
	                  " names no subobject a constant expression reads of " +
	                  analyzer_.types_.description(object.type));
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
		                  "an object of class type");
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

SemaConstant ConstexprReading::called(const AstNode& callee,
                                      const std::vector<SemaConstant>& arguments,
                                      const SemaContext& ctx)
{
	if (callee.kind == AstKind::MemberExpression)
	{
		// 5.2.2p1: the postfix-expression is 5.2.5p1's member access, which is
		// the one shape of a call that runs a body on an object.
		return member_called(callee, arguments, ctx);
	}
	if (callee.kind != AstKind::IdExpression)
	{
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate");
	}
	std::vector<AnalyzedValue> written;
	argument_values(arguments, written);
	std::vector<SemaEntity*> candidates;
	std::size_t singles = 0;
	SemaEntity* const named =
		callee_candidates(callee, ctx, written, candidates, singles);
	if (named == nullptr)
	{
		throw NotConstant(callee.text +
		                  " is written where a constant expression calls and "
		                  "names no function");
	}
	if (named->kind != SemaKind::Function)
	{
		// 13.5.4p1: what the parentheses were written on is an object, so the
		// call is a call of a member `operator()` of its class - which is a
		// member call on the object that name is worth and no further reading
		// of its own.
		return member_call(id_constant(callee, ctx), "operator()", arguments,
		                   ctx);
	}
	SemaEntity& one = selected(callee.text, candidates, written, nullptr,
	                           singles);
	// 9.3.1p3: a call written with no object expression is one on the object
	// the function being read stands on, which 5.19p2 has no value for - so a
	// fold reaches only the declarations no object is needed to call.
	if (one.object_member)
	{
		throw NotConstant(callee.text +
		                  " is called on an object a constant expression does "
		                  "not name");
	}
	return call(one, nullptr, arguments);
}

SemaConstant ConstexprReading::called_entity(
	SemaEntity& named, const std::vector<SemaConstant>& arguments)
{
	if (named.kind != SemaKind::Function)
	{
		throw NotConstant(named.name +
		                  " is written where a constant expression calls and "
		                  "names no function");
	}
	std::vector<AnalyzedValue> written;
	argument_values(arguments, written);
	const std::vector<SemaEntity*> candidates(1, &named);
	SemaEntity& one = selected(named.name, candidates, written, nullptr, 0);
	if (one.object_member)
	{
		throw NotConstant(named.name +
		                  " is called on an object a constant expression does "
		                  "not name");
	}
	return call(one, nullptr, arguments);
}

// 13.3.3.1: what a constant a fold holds is worth to a ranking of candidates.
//
// A constant is 5.19's *value*, and 3.10p1 makes a value a prvalue however the
// expression that reached it was written: the fold holds what the operand came
// to and no object the program named.  So a candidate declared over `T&` is one
// nothing here reaches and one declared over `T const&` is reached by every
// constant of its type - which is 8.5.3p5 as it stands for any other prvalue,
// and the whole of what tells `read(T&)` from `read(T const&)` apart.  The bits
// travel with the type because 4.10p1's null pointer constant is a fact of the
// *value* and 13.3.3.2p4 ranks by it.
void ConstexprReading::argument_values(
	const std::vector<SemaConstant>& arguments,
	std::vector<AnalyzedValue>& out) const
{
	out.reserve(arguments.size());
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		out.push_back(argument_value(arguments[index]));
	}
}

AnalyzedValue ConstexprReading::argument_value(const SemaConstant& value) const
{
	AnalyzedValue out;
	out.type = out.spelled = value.type;
	out.category = ValueCategory::PRValue;
	out.constant = true;
	out.value = value.bits;
	out.real = value.real;
	out.null_constant = value.bits == 0 && !holds_list(value) &&
		analyzer_.integral_type(value.type) != kNoType;
	return out;
}

// 13.3.1p3: the implicit object argument of a call written on a constant
// object, which 9.3.1p3 holds as a pointer to it.
//
// The cv-qualification the constant's own type carries is the one a candidate's
// object parameter has to accept, which is what leaves a member function that
// is not `const` no candidate for a call on a `constexpr` object - the fact the
// arity that used to rank these could not see.
AnalyzedValue ConstexprReading::object_value(const SemaConstant& object) const
{
	AnalyzedValue out;
	out.type = out.spelled = analyzer_.types_.pointer_to(object.type);
	out.category = ValueCategory::PRValue;
	// 8.3.5p1: a ref-qualifier binds by the category the object expression had,
	// and a constant object a declaration named is an lvalue.
	out.object_category = ValueCategory::LValue;
	out.nonnull = true;
	return out;
}

// 3.4, 3.4.2 and 14.2: the declarations the name of a callee reaches, as the
// set 13.3 chooses from.
//
// This is the lookup `call_expression` writes for a call the program's own
// semantics reads, asked here of a fold - a template-id names the
// specializations 14.8.1 makes of every template of that name rather than
// anything an ordinary lookup finds, and an unqualified name also names what
// 3.4.2p2's associated namespaces declare.  What a *fold* adds to that is
// nothing at all, which is the point: a call is one construct, and a constant
// expression is not a dialect of it.
SemaEntity* ConstexprReading::callee_candidates(
	const AstNode& callee, const SemaContext& ctx,
	const std::vector<AnalyzedValue>& written,
	std::vector<SemaEntity*>& candidates, std::size_t& singles)
{
	SemaEntity* named = nullptr;
	if (child_kind(callee, AstKind::CarriedExpression) != nullptr)
	{
		// 7.1.6.2p1: the nested-name-specifier begins with a decltype-specifier,
		// so the region the name is looked up in is the one that type names.
		named = analyzer_.decltype_qualified_name(callee, ctx, LookupKind::Any,
		                                          &candidates);
	}
	else
	{
		named = analyzer_.template_specializations(callee.text, ctx, candidates);
	}
	if (named == nullptr)
	{
		candidates.clear();
		named = analyzer_.resolve(callee.text, ctx, LookupKind::Any,
		                          &candidates);
	}
	if (named != nullptr && named->kind != SemaKind::Function)
	{
		// 13.5.4p1: the name reaches an object rather than a function, which is
		// a callee only its class's `operator()` answers.
		candidates.clear();
		return named;
	}
	if (named != nullptr && candidates.empty())
	{
		candidates.push_back(named);
	}
	if (!QualifiedName(callee.text).qualified() &&
	    ArgumentLookup(analyzer_).allowed(named))
	{
		// 3.4.2p1: an unqualified callee also names what the types of the
		// arguments reach, and p3 leaves that search out where the ordinary
		// lookup found a member, a block-scope declaration or a non-function.
		// A name the ordinary lookup found nothing of is one this search is the
		// whole of, which is what reaches a function declared beside its own
		// argument's class and nowhere else.
		singles = ArgumentLookup(analyzer_).candidates(callee.text, written,
		                                              candidates);
	}
	if (named == nullptr)
	{
		return candidates.empty() ? nullptr : candidates[0];
	}
	return named;
}

SemaEntity& ConstexprReading::selected(
	const std::string& name, const std::vector<SemaEntity*>& candidates,
	const std::vector<AnalyzedValue>& written, const AnalyzedValue* object,
	std::size_t singles)
{
	SemaEntity* one = nullptr;
	try
	{
		one = analyzer_.select_overload(candidates, written, name, object,
		                                false, singles);
	}
	catch (const std::runtime_error& refused)
	{
		// 13.3 refuses the *call*, and a fold is asked speculatively - a
		// template argument, a `static_assert`, an initializer the analysis
		// will read again as a dynamic one.  So a set with nothing viable in it
		// says here what every other unreadable operand says: this expression
		// is no constant expression.
		throw NotConstant(std::string(refused.what()));
	}
	if (one == nullptr)
	{
		throw NotConstant(name +
		                  " names no one constexpr function a constant "
		                  "expression may call with these arguments");
	}
	// 3.2p2 and 14.7.1p1: choosing a declaration is naming it, and what a fold
	// then reads is the *body* - which for a specialization is a body only this
	// asks the template for, and for a member of an instantiated class one the
	// instantiation put aside until a use arrived.  So a fold names what it
	// chose exactly as the expression layer does; 7.3.3p1's using-declaration
	// is followed to the base's own declaration there and here alike.
	return analyzer_.named_function(*one);
}

// 12.3.2p1 with 14.3.2p5: an object of class type brought to an integral type.
//
// A conversion function is a member function of no parameters whose name is the
// type it converts to, so what the fold does is call it on the object.  The
// class's own conversions are what 13.3.1.5p1 offers first; a class that
// declares one to the very type the place asked for is chosen over one that
// reaches it by a further standard conversion, which is the whole of the
// ranking a set of constant answers can bear.
SemaConstant ConstexprReading::at_arithmetic_place(const SemaConstant& value,
                                                   TypeId place)
{
	// A constant of class type is the identifier of an interned list and not a
	// number of the object's own width, so reading its bits where a number was
	// asked for is reading the identifier.  5.19p3 leaves a converted constant
	// expression its user-defined conversions, which is the one reading that
	// turns such a constant into one.
	return is_object(value) ? converted(value, place) : value;
}

SemaConstant ConstexprReading::converted(const SemaConstant& value,
                                         TypeId place)
{
	SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(value.type));
	if (owner == nullptr)
	{
		throw NotConstant("a constant expression converts an object of a type "
		                  "it does not know");
	}
	SemaEntity* found = nullptr;
	bool exact = false;
	unsigned reaching = 0;
	// 3.9.3p1: the cv-qualifiers of the place say nothing about which
	// conversion reaches it - `constexpr int n = d;` asks for a `const int` and
	// `operator int` is the very conversion 13.3.3p1 calls the best one.
	const TypeId wanted =
		place == kNoType ? kNoType : analyzer_.types_.strip_cv(place);
	for (std::size_t index = 0; index < owner->conversions.size(); ++index)
	{
		SemaEntity& each = *owner->conversions[index];
		if (!each.constexpr_function || each.constexpr_body == nullptr)
		{
			continue;
		}
		const TypeId to = analyzer_.types_.target(each.type);
		if (analyzer_.arithmetic_type(to) == kNoType)
		{
			continue;
		}
		if (wanted != kNoType && analyzer_.types_.strip_cv(to) == wanted)
		{
			found = &each;
			exact = true;
			break;
		}
		++reaching;
		if (found == nullptr)
		{
			found = &each;
		}
	}
	if (found == nullptr)
	{
		throw NotConstant(analyzer_.types_.description(value.type) +
		                  " declares no constexpr conversion function a "
		                  "constant expression reaches a value through");
	}
	if (!exact && reaching > 1)
	{
		// 13.3.3p1: which of two conversions is the better one is a ranking of
		// what each answer then converts by, which a fold has no typed
		// expression to make - so a class offering two that reach the place
		// only through a further conversion is refused rather than picked from.
		throw NotConstant(analyzer_.types_.description(value.type) +
		                  " declares more than one constexpr conversion "
		                  "function a constant expression reaches this place "
		                  "through");
	}
	const std::vector<SemaConstant> none;
	return call(*found, &value, none);
}

SemaEntity& ConstexprReading::bind_constant(const std::string& name,
                                            const SemaConstant& value,
                                            const SemaContext& inner,
                                            bool written)
{
	SemaEntity& bound =
		analyzer_.model_.create(SemaKind::Variable, name, value.type);
	bound.constant = true;
	bound.fold_local = written;
	bound.value = value.bits;
	bound.real = value.real;
	bound.region = inner.scope;
	analyzer_.model_.bind(*inner.scope, name, bound);
	analyzer_.model_.declare_in(*inner.scope, bound);
	return bound;
}

// 8.3.5p10 with 14.5.3p4: the names of the places `callee`'s declarator opened,
// one per place of the function's type.
//
// The declarator wrote the *entries* of a parameter-declaration-clause, and an
// entry written `pattern... name` is not one place but as many as the run its
// packs were bound to holds.  What says how many is the type: 14.8.2 settled
// the run when it made this declaration, and the places the type has past the
// ones the other entries wrote are the run's.  8.3.5p10 names them after the
// pack - `pack_element_name`'s spellings, whose first is the pack's own name -
// which is exactly what a `name...` written in the body looks up, so the
// bindings a fold makes for them are found by 14.5.3p4's own reading.
// `runs` takes that length at the place a run begins and zero everywhere else.
void ConstexprReading::declared_places(SemaEntity& callee,
                                       std::vector<std::string>& out,
                                       std::vector<unsigned>& runs,
                                       std::string& empty) const
{
	if (callee.constexpr_body == nullptr)
	{
		return;
	}
	std::vector<std::string> entries;
	std::vector<char> packed;
	parameter_names(*callee.constexpr_body, entries, packed);
	const std::size_t implicit = callee.object_member ? 1u : 0u;
	const std::size_t held = analyzer_.types_.parameters(callee.type).size();
	const std::size_t places = held > implicit ? held - implicit : 0;
	std::size_t fixed = 0;
	for (std::size_t index = 0; index < packed.size(); ++index)
	{
		fixed += packed[index] != 0 ? 0u : 1u;
	}
	// 14.5.3p4: a pack expanded into no place at all is still declared, so the
	// run is what the type has over the entries that are one place each and
	// never a negative count.
	const std::size_t run = places > fixed ? places - fixed : 0;
	for (std::size_t index = 0; index < entries.size(); ++index)
	{
		if (packed[index] == 0)
		{
			out.push_back(entries[index]);
			runs.push_back(0);
			continue;
		}
		if (run == 0)
		{
			// 14.5.3p4: the run holds no element, so the expansion made no
			// place at all - and the pack is still declared, because
			// `sizeof...` and a `name...` written in the body are asked about
			// it and have to come to nothing rather than to nothing found.
			empty = entries[index];
		}
		for (std::size_t element = 0; element < run; ++element)
		{
			out.push_back(pack_element_name(entries[index], element));
			runs.push_back(element == 0 ? static_cast<unsigned>(run) : 0u);
		}
	}
}

void ConstexprReading::bind_arguments(SemaEntity& callee,
                                      const SemaConstant* object,
                                      const std::vector<SemaConstant>& arguments,
                                      const SemaContext& inner)
{
	if (object != nullptr)
	{
		// 9.3.1p3 and 9.2p1: what a member named with no object expression
		// denotes is the object the function was called on, so the object's
		// own subobjects are bindings of this region - one constant each,
		// under the name the member was declared with.
		std::vector<SemaEntity*> members;
		data_members(object->type, members);
		const std::vector<TypeId>& held =
			analyzer_.types_.type_list_at(static_cast<std::uint32_t>(object->bits));
		for (std::size_t index = 0;
		     index < members.size() && index < held.size(); ++index)
		{
			// 5.19p2: the object the call was written on is one whose lifetime
			// began before this evaluation, so its subobjects are read here
			// and are no binding the body may write.
			bind_constant(members[index]->name,
			              constant_of(held[index], analyzer_.types_), inner,
			              false);
		}
	}
	// 8.3.5p10 and 5.2.2p4: the places the declarator wrote, each bound to what
	// the argument written for it came to after 8.5's conversion to its type.
	std::vector<std::string> places;
	std::vector<unsigned> runs;
	std::string empty_run;
	declared_places(callee, places, runs, empty_run);
	if (!empty_run.empty())
	{
		// 14.5.3p4: the pack the expansion was over is bound to a run of no
		// elements, which is a declaration and no place - the one the reading
		// of a `pattern...` in the body finds to learn it stands for nothing.
		SemaEntity& none = analyzer_.model_.create(
			SemaKind::Typedef, empty_run,
			analyzer_.types_.pack_type(std::vector<TypeId>()));
		analyzer_.model_.bind(*inner.scope, none.name, none);
	}
	// 14.5.3p4: the run one expansion of a function parameter pack was bound to
	// is counted on the first of the places it made, and every place after it
	// carries that first one back - which is what a `pattern...` written in the
	// body reads to learn how many readings it stands for.  The bindings the
	// fold makes stand in for those places, so they carry the same facts.
	SemaEntity* run_of = nullptr;
	unsigned run_left = 0;
	for (std::size_t index = 0;
	     index < arguments.size() && index < places.size(); ++index)
	{
		const std::string& name = places[index];
		if (name.empty())
		{
			// 8.3.5p10: 3.3.4 ends a declaration's parameter names at its own
			// declarator, so a place the definition left unnamed binds nothing
			// and the body cannot have named it either.
			run_left = run_left != 0 ? run_left - 1 : 0;
			continue;
		}
		// 5.19p2 with 12.2p1: a place the call filled is an object created by
		// this evaluation, which is the one standing an assignment inside the
		// body may write.
		SemaEntity& bound =
			bind_constant(name, arguments[index], inner, true);
		bound.pack_run = runs[index];
		if (bound.pack_run != 0)
		{
			run_of = &bound;
			run_left = bound.pack_run - 1;
		}
		else if (run_left != 0)
		{
			bound.pack_element_of = run_of;
			--run_left;
		}
	}
}

// 5.2.2p4 with 8.3.6p1: the list the places of `callee` are filled with.
//
// 8.3.5p10 converts each argument to the type of the place it reached before
// the body reads it, which is what makes the fold a fact of the converted list
// and not of the spellings that wrote it - and 8.3.6p1 reads a call that stops
// short of a place as if the default-argument stood where the argument is
// missing, so those values are part of that same list.  8.3.6p9 leaves such an
// expression to the region the declaration that introduced it was written in,
// which is where it is read here.
std::uint32_t ConstexprReading::passed_arguments(
	SemaEntity& callee, const SemaConstant* object,
	const std::vector<SemaConstant>& arguments,
	std::vector<SemaConstant>& passed)
{
	const std::vector<TypeId>& places = analyzer_.types_.parameters(callee.type);
	const std::size_t implicit = callee.object_member ? 1 : 0;
	const std::unordered_map<std::uint32_t,
	                         std::vector<ParameterRecord> >::const_iterator
		wrote = analyzer_.defaults_.find(
			SemaAnalyzer::wrote_defaults(callee).id);
	passed.reserve(places.size());
	std::vector<TypeId> key;
	key.reserve(places.size() + 1);
	key.push_back(object == nullptr ? kNoType : entry_of(*object));
	for (std::size_t at = implicit; at < places.size(); ++at)
	{
		const std::size_t index = at - implicit;
		SemaConstant given;
		if (index < arguments.size())
		{
			given = arguments[index];
		}
		else
		{
			const AstNode* const written =
				wrote == analyzer_.defaults_.end() || at >= wrote->second.size()
					? nullptr
					: wrote->second[at].initializer.written;
			SemaContext where;
			where.scope = written == nullptr
				? nullptr : wrote->second[at].initializer.scope;
			if (written == nullptr || where.scope == nullptr ||
			    written->children.empty() ||
			    written->children[0]->children.empty())
			{
				throw NotConstant(callee.name +
				                  " is called with no argument for a place its "
				                  "declaration gives no default-argument");
			}
			where.dump = where.scope->dump;
			given = analyzer_.evaluate(*written->children[0]->children[0], where);
		}
		if (analyzer_.arithmetic_type(places[at]) != kNoType)
		{
			given = analyzer_.convert(given, places[at]);
			given.type = places[at];
		}
		passed.push_back(given);
		key.push_back(entry_of(given));
	}
	// 5.2.2p7: an argument the ellipsis matched fills no place, and what it is
	// worth is still what tells one fold of this callee from another.
	for (std::size_t index = passed.size(); index < arguments.size(); ++index)
	{
		passed.push_back(arguments[index]);
		key.push_back(entry_of(arguments[index]));
	}
	return analyzer_.types_.type_list(key);
}

SemaConstant ConstexprReading::call(SemaEntity& callee,
                                    const SemaConstant* object,
                                    const std::vector<SemaConstant>& arguments)
{
	if (!callee.constexpr_function || callee.constexpr_body == nullptr ||
	    callee.constexpr_region == nullptr)
	{
		throw NotConstant(callee.name +
		                  " is not a constexpr function this unit has defined");
	}
	const TypeId result = analyzer_.types_.target(callee.type);
	std::vector<SemaConstant> passed;
	const std::uint32_t list = passed_arguments(callee, object, arguments, passed);
	const TypeId held = analyzer_.model_.folded_call(callee, list);
	if (held != kNoType)
	{
		return constant_of(held, analyzer_.types_);
	}
	unsigned& depth = analyzer_.model_.folding_depth();
	if (depth >= kMaxConstexprDepth)
	{
		throw NotConstant("a constant expression calls constexpr functions "
		                  "more deeply than this implementation reads");
	}
	const ReadingDepth folding(depth);
	// 14.6.1p1 and 3.3.3: a region of the fold's own, so the bindings one call
	// makes are not the ones another sees and the body's own names are looked
	// up over the region its declarator opened.
	SemaContext inner;
	inner.scope = &analyzer_.model_.open(ScopeKind::Block,
	                                     *callee.constexpr_region, nullptr,
	                                     callee.constexpr_region->dump);
	inner.dump = callee.constexpr_region->dump;
	bind_arguments(callee, object, passed, inner);
	const AstNode& body = *callee.constexpr_body;
	const AstNode* compound = nullptr;
	for (std::size_t index = 0; index < body.children.size(); ++index)
	{
		if (body.children[index]->kind == AstKind::CompoundStatement)
		{
			compound = body.children[index];
			break;
		}
	}
	if (compound == nullptr)
	{
		throw NotConstant(callee.name +
		                  " has no function-body a constant expression reads");
	}
	// 6.1-6.6: the body is run rather than pattern-matched.  7.1.5p3 leaves a
	// C++11 constexpr body one `return` statement, and this milestone's own
	// boundary allows the evaluation a strict superset of that - so what says
	// the call is not a constant expression is a statement the walk cannot run
	// or a value it cannot read, not the shape of the body.
	ConstexprFrame frame;
	statement(*compound, inner, frame);
	if (!frame.returned)
	{
		throw NotConstant(callee.name +
		                  " is a constexpr function whose body reaches no "
		                  "return statement 6.6.3p2 gives the call a value by");
	}
	SemaConstant answer = frame.result;
	if (analyzer_.arithmetic_type(result) != kNoType)
	{
		// 6.6.3p2: the value the return statement's expression is converted to
		// the return type, which is what the caller reads.
		answer = analyzer_.convert(answer, result);
		answer.type = result;
	}
	analyzer_.model_.hold_folded_call(callee, list, entry_of(answer));
	return answer;
}

SemaConstant ConstexprReading::id_constant(const AstNode& node,
                                           const SemaContext& ctx)
{
	// 7.1.6.2p1: a nested-name-specifier that begins with a decltype-specifier
	// reaches its region through the expression the parser kept beside the
	// name, which no spelling holds - so 5.19's reading asks the same question
	// of an id-expression that every other reader of one asks.
	SemaEntity* const named =
		child_kind(node, AstKind::CarriedExpression) == nullptr
		? analyzer_.resolve(node.text, ctx, LookupKind::Any)
		: analyzer_.decltype_qualified_name(node, ctx, LookupKind::Any);
	SemaEntity& entity = analyzer_.require(named, node.text);
	if (!entity.constant)
	{
		if (analyzer_.checking_ > 0 && analyzer_.types_.is_dependent(entity.type))
		{
			// 14.6p8: what a name that depends on a template parameter is
			// worth, an argument list is what says.  The reading stands one
			// value in its place, as it does for the size of a dependent type.
			++analyzer_.stood_in_;
			SemaConstant stood;
			stood.type = analyzer_.types_.fundamental(FT_INT);
			stood.bits = 1;
			return stood;
		}
		throw NotConstant(node.text + " is not a constant expression");
	}
	SemaConstant out;
	out.type = entity.type;
	out.bits = entity.value;
	out.real = entity.real;
	return out;
}

SemaConstant ConstexprReading::unary_constant(const AstNode& node,
                                              const SemaContext& ctx)
{
	if (node.token == OP_INC || node.token == OP_DEC)
	{
		// 5.3.2p1: the operand is written and the value is the object as it now
		// stands, so the operand is not read as a value first.
		return increment_constant(node, ctx, true);
	}
	const SemaConstant operand = analyzer_.promote(analyzer_.evaluate(*node.children[0], ctx));
	SemaConstant out;
	out.type = operand.type;
	if (analyzer_.types_.is_floating(analyzer_.arithmetic_type(operand.type)))
	{
		// 5.3.1p7 and p8: `+` and `-` take a floating operand as it stands and
		// hand back its value and its negation.  5.3.1p10's `~` asks for an
		// integral operand and 5.3.1p9's `!` is 4p3's reading of the value.
		switch (node.token)
		{
		case OP_PLUS: out.real = operand.real; return out;
		case OP_MINUS: out.real = -operand.real; return out;
		case OP_LNOT:
			out.type = analyzer_.types_.fundamental(FT_BOOL);
			out.bits = operand.real == 0 ? 1 : 0;
			return out;
		default:
			throw NotConstant("a constant expression writes an operator that "
			                  "asks for an integral operand on a floating one");
		}
	}
	switch (node.token)
	{
	case OP_PLUS:
		out.bits = operand.bits;
		break;

	case OP_MINUS:
	{
		if (analyzer_.is_signed(out.type) && analyzer_.width_of(out.type) == 64 &&
		    operand.bits == (1ULL << 63))
		{
			throw NotConstant("a constant expression overflows");
		}
		out.bits = 0ULL - operand.bits;
		break;
	}

	case OP_COMPL:
		out.bits = ~operand.bits;
		break;

	case OP_LNOT:
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = operand.bits == 0 ? 1 : 0;
		return out;

	default:
		throw NotConstant("a constant expression holds an operator PA11 "
		                         "does not evaluate");
	}
	return analyzer_.convert(out, out.type);
}

SemaConstant ConstexprReading::binary_constant(const AstNode& node,
                                               const SemaContext& ctx)
{
	// 5.14p1 and 5.15p1: the right operand of `&&` and `||` is evaluated only
	// when the left one does not decide the answer.
	if (node.token == OP_LAND || node.token == OP_LOR)
	{
		const bool left = truth(analyzer_.evaluate(*node.children[0], ctx));
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		if (left == (node.token == OP_LOR))
		{
			out.bits = left ? 1 : 0;
			return out;
		}
		out.bits = truth(analyzer_.evaluate(*node.children[1], ctx)) ? 1 : 0;
		return out;
	}
	// 5.18p1: the left operand of a comma is evaluated and its value discarded,
	// and the result is the right one - which is what a for-statement's head
	// writes to advance more than one object per pass.
	if (node.token == OP_COMMA)
	{
		analyzer_.evaluate(*node.children[0], ctx);
		return analyzer_.evaluate(*node.children[1], ctx);
	}

	return binary_value(node.token, analyzer_.evaluate(*node.children[0], ctx),
	                    analyzer_.evaluate(*node.children[1], ctx));
}

// 5.6, 5.7 and 5.9 over two operands 5p10 has already brought to one floating
// type: the arithmetic is the machine's own at that width, and the operators an
// integral operand is what the clause asks for - the remainder, the shifts and
// the bitwise ones - reach no floating value at all.
SemaConstant ConstexprReading::real_value(unsigned token, long double left,
                                          long double right, TypeId type)
{
	SemaConstant out;
	switch (token)
	{
	case OP_LT: case OP_GT: case OP_LE: case OP_GE: case OP_EQ: case OP_NE:
	{
		bool answer = false;
		switch (token)
		{
		case OP_LT: answer = left < right; break;
		case OP_GT: answer = left > right; break;
		case OP_LE: answer = left <= right; break;
		case OP_GE: answer = left >= right; break;
		case OP_EQ: answer = left == right; break;
		default: answer = left != right; break;
		}
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = answer ? 1 : 0;
		return out;
	}

	case OP_PLUS: out.real = left + right; break;
	case OP_MINUS: out.real = left - right; break;
	case OP_STAR: out.real = left * right; break;
	case OP_DIV:
		// 5.6p4: division by zero has undefined behaviour, which 5.19p2 leaves
		// outside a constant expression however the operands are spelled.
		if (right == 0)
		{
			throw NotConstant("a constant expression divides by zero");
		}
		out.real = left / right;
		break;

	default:
		throw NotConstant("a constant expression writes an operator that asks "
		                  "for integral operands on floating ones");
	}
	out.type = type;
	// 4.8p1 again: the answer is held at the width the operands were brought
	// to, which for `float` is narrower than the machine computed in.
	return analyzer_.convert(out, type);
}

SemaConstant ConstexprReading::binary_value(unsigned token,
                                            const SemaConstant& given_left,
                                            const SemaConstant& given_right)
{
	const SemaConstant left = analyzer_.promote(given_left);
	const SemaConstant right = analyzer_.promote(given_right);
	const bool comparison = token == OP_LT || token == OP_GT ||
		token == OP_LE || token == OP_GE || token == OP_EQ ||
		token == OP_NE;
	// 5.8p1: a shift takes 5p10's conversions on neither operand.  Each is
	// promoted on its own and the result has the type of the promoted left one,
	// so an unsigned count does not make the value it shifts unsigned.
	const bool shifted = token == OP_LSHIFT || token == OP_RSHIFT;
	const TypeId type =
		shifted ? left.type : analyzer_.common_type(left.type, right.type);
	if (analyzer_.types_.is_floating(analyzer_.arithmetic_type(type)))
	{
		return real_value(token, analyzer_.convert(left, type).real,
		                  analyzer_.convert(right, type).real, type);
	}
	const unsigned long long lhs = analyzer_.convert(left, type).bits;
	const unsigned long long rhs = shifted ? right.bits : analyzer_.convert(right, type).bits;
	const bool sign = analyzer_.is_signed(type);
	const long long signed_lhs = static_cast<long long>(lhs);
	const long long signed_rhs = static_cast<long long>(rhs);

	if (comparison)
	{
		bool answer = false;
		switch (token)
		{
		case OP_LT: answer = sign ? signed_lhs < signed_rhs : lhs < rhs; break;
		case OP_GT: answer = sign ? signed_lhs > signed_rhs : lhs > rhs; break;
		case OP_LE: answer = sign ? signed_lhs <= signed_rhs : lhs <= rhs; break;
		case OP_GE: answer = sign ? signed_lhs >= signed_rhs : lhs >= rhs; break;
		case OP_EQ: answer = lhs == rhs; break;
		default: answer = lhs != rhs; break;
		}
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = answer ? 1 : 0;
		return out;
	}

	SemaConstant out;
	out.type = type;
	long long result = 0;
	bool overflowed = false;
	switch (token)
	{
	case OP_PLUS:
		overflowed = add_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs + rhs;
		break;

	case OP_MINUS:
		overflowed = subtract_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs - rhs;
		break;

	case OP_STAR:
		overflowed = multiply_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs * rhs;
		break;

	case OP_DIV:
	case OP_MOD:
		if (rhs == 0)
		{
			throw NotConstant("a constant expression divides by zero");
		}
		if (sign)
		{
			overflowed = signed_lhs == (-1 - 0x7FFFFFFFFFFFFFFFLL) && signed_rhs == -1;
			result = token == OP_DIV ? signed_lhs / signed_rhs
			                              : signed_lhs % signed_rhs;
			out.bits = static_cast<unsigned long long>(result);
			break;
		}
		out.bits = token == OP_DIV ? lhs / rhs : lhs % rhs;
		break;

	case OP_LSHIFT:
	case OP_RSHIFT:
	{
		const unsigned long long count =
			analyzer_.is_signed(right.type) && static_cast<long long>(rhs) < 0
				? static_cast<unsigned long long>(analyzer_.width_of(type))
				: rhs;
		if (count >= analyzer_.width_of(type))
		{
			throw NotConstant("a constant expression shifts by more than "
			                         "the width of its type");
		}
		if (token == OP_LSHIFT)
		{
			out.bits = lhs << count;
			result = static_cast<long long>(out.bits);
			// 5.8p2: a signed left operand shall be non-negative, and the value
			// shall be representable in the *unsigned* type of the same width -
			// so `1LL << 63` is the sign bit and not an overflow, which is what
			// the bits shifted past the width say.
			overflowed = sign &&
				(signed_lhs < 0 ||
				 (count != 0 && (lhs >> (analyzer_.width_of(type) - count)) != 0));
			break;
		}
		out.bits = sign ? static_cast<unsigned long long>(signed_lhs >> count)
		                : lhs >> count;
		break;
	}

	case OP_AMP: out.bits = lhs & rhs; break;
	case OP_BOR: out.bits = lhs | rhs; break;
	case OP_XOR: out.bits = lhs ^ rhs; break;

	default:
		throw NotConstant("a constant expression holds an operator PA11 "
		                         "does not evaluate");
	}

	// 5p4: an operation whose result its type cannot represent has undefined
	// behaviour, so it is not a constant expression.  5.8p2's shift is the one
	// operation whose signed result may hold the sign bit and still be the
	// value the clause names, so it answers for itself.
	const SemaConstant narrowed = analyzer_.convert(out, type);
	if (sign && (overflowed || (!shifted && narrowed.bits != out.bits)))
	{
		throw NotConstant("a constant expression overflows");
	}
	return narrowed;
}

// 5.2.3p1, 5.2.3p2/p3 and 5.2.2p1: the one shape the grammar hands on as a
// call, which is a cast written in functional notation, an object of literal
// class type, or a call of a function - and which of the three it is, is
// settled by the one lookup of the name before the parentheses.
SemaConstant ConstexprReading::call_or_cast(const AstNode& node,
                                           const SemaContext& ctx)
{
	// 5.2.3p1: `T(x)` is the cast `(T)x` written in functional notation,
	// which the grammar hands on as a call because it cannot say whether
	// the name before the parentheses is a type.  A call of a *function*
	// is the other reading of the shape and is 5.19p2's constexpr
	// function, so only the arm whose callee names an arithmetic type
	// folds here.
	const AstNode& callee = *node.children[0];
	TypeId target = kNoType;
	if (callee.kind == AstKind::IdExpression)
	{
		target = analyzer_.keyword_type(callee.text);
	}
	if (target == kNoType && callee.kind == AstKind::IdExpression)
	{
		// 7.1.6.2p1: the simple-type-specifier may be a typedef-name or an
		// enum-name the program declared, which 3.4 answers for - and a
		// name that reaches no region at all is a call and not a cast.
		const unsigned stood = analyzer_.stood_in_;
		try
		{
			SemaEntity* const named =
				analyzer_.resolve(callee.text, ctx, LookupKind::Type);
			target = named == nullptr ? kNoType : named->type;
		}
		catch (const std::exception&)
		{
			// 14.6p8's count is of the stand-ins a *reading* made, and a name
			// this one threw out made none - the same as `probe_type_id` and
			// the pattern reading `sema_specialize.cpp` throws away.  A name
			// that is a template-id reaches an argument list on the way here,
			// so the count can move before the throw.
			analyzer_.stood_in_ = stood;
			target = kNoType;
		}
	}
	// 5.2.3p3: `T{x}` is written where `T(x)` is, and the braces put
	// 8.5.4's one initializer-clause where the operand stands.
	const AstNode* list =
		node.children.size() < 2 ? nullptr : node.children[1];
	if (list != nullptr && list->braced)
	{
		list = list->children.empty() ? nullptr : list->children[0];
	}
	if (list == nullptr)
	{
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate");
	}
	// 14.5.3p4: what the parentheses hold is the run a `pattern...` entry
	// stands for, which is the same reading the analyzer_.lowering of this cast is
	// given - 5.2.3's one rule has one answer here too.  It is read once
	// here, because a cast, an object of class type and 5.2.2's call all
	// take the operands the same list wrote.
	InitializerClauses written(list, analyzer_, ctx);
	if (written.list.unsettled())
	{
		// 14.6p8: a run no argument list has settled says neither how many
		// operands the cast has nor what they are worth, so the reading
		// stands a value in for it exactly as it does for `sizeof...`.
		// The stand-in is 1 rather than 0 because 8.3.4p1 gives an array
		// no bound of zero.
		++analyzer_.stood_in_;
		SemaConstant out;
		out.type = target == kNoType ? analyzer_.types_.fundamental(FT_INT) : target;
		out.bits = 1;
		return out;
	}
	std::vector<SemaConstant> operands;
	operands.reserve(written.list.size());
	while (!written.spent())
	{
		const SemaContext inner = written.in(ctx);
		const AstNode& clause = written.next();
		++written.at;
		operands.push_back(analyzer_.evaluate(clause, inner));
	}
	if (target != kNoType && analyzer_.types_.is_class(analyzer_.types_.strip_cv(target)))
	{
		// 5.2.3p2 and p3: an object of literal class type, which at a value
		// place is what 12.3.2p1's conversion function reads.
		return object_of(target, operands);
	}
	if (target == kNoType)
	{
		// 5.2.2p1 with 7.1.5p2: the other reading of the shape, which is a
		// call of a function - and only a constexpr function whose body
		// this unit has read is one 5.19p2 folds.
		return called(callee, operands, ctx);
	}
	if (analyzer_.arithmetic_type(target) == kNoType || operands.size() > 1)
	{
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate");
	}
	if (operands.empty())
	{
		// 5.2.3p2: `T()` is the value-initialization 8.5p7 writes, which
		// for an arithmetic type is the zero 8.5p6 converts to it.
		SemaConstant out;
		out.type = target;
		out.bits = 0;
		return out;
	}
	SemaConstant out = analyzer_.convert(operands[0], target);
	out.type = target;
	return out;
}
