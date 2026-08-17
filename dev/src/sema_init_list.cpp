#include "sema_analyzer.h"

#include <cmath>
#include <stdexcept>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"
#include "sema_derivation.h"
#include "sema_string_init.h"

namespace
{

// 8.5.1p7: how many bytes of value-initialized array elements are still
// described one element at a time.  Past this the elements stop being what
// describes the initialization and the storage starts being it - and a bound
// the source wrote as one number would otherwise cost one node per element.
const unsigned long long kZeroFillLimit = 64;

// 8.5.1p6: the count that stands for "as many clauses as the program can
// write", which is what an array with no bound takes and what a count that
// would overflow saturates to.
const unsigned long long kUnboundedClauses = ~0ull / 2;

const AstNode* child_of(const AstNode& node, AstKind kind)
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

// The argument list of a call, in either of the two spellings PA10 writes one
// as - which is where 5.2.3p3's `T{...}` writes its one braced clause.
const AstNode* call_arguments(const AstNode& node)
{
	const AstNode* list = child_of(node, AstKind::ArgumentList);
	return list != nullptr ? list : child_of(node, AstKind::ParenArgumentList);
}

std::string decimal(unsigned long long value)
{
	std::string digits;
	unsigned long long rest = value;
	while (rest != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
		rest /= 10;
	}
	return digits.empty() ? std::string("0") : digits;
}

}

// 8.5.1's aggregate initialization and 8.5.4's list-initialization.
//
// The two are one layer: a braced-init-list is not an expression, so what a
// clause means is a question about the object it initializes rather than about
// the clause, and the walk that answers it is the one walk of the subobjects
// 8.5.1p2 puts them in order against.  13.3.3.1.5's ranking of a list against a
// parameter and 8.5.4p7's narrowing check are the two questions 13.3 and 5.17
// ask of that same walk, so they stand here beside it rather than beside the
// candidate set that asks.
//
// The walk carries `Clauses`, which is the list and how far into it the walk
// has come: a subobject whose braces were left out takes clauses from the
// enclosing list, so how many are left is the state and not the recursion.

bool InitializerClauses::spent() const
{
	return at >= list.size();
}

const AstNode& InitializerClauses::next() const
{
	return list.node(at);
}

SemaContext InitializerClauses::in(const SemaContext& outer) const
{
	Scope* const region = spent() ? nullptr : list.region(at);
	if (region == nullptr)
	{
		return outer;
	}
	SemaContext inner = outer;
	inner.scope = region;
	return inner;
}

DumpNode& SemaAnalyzer::open_subobject(DumpNode& parent, TypeId type,
                                       const SemaEntity* member,
                                       unsigned long long index)
{
	DumpNode& node = model_.open_node(
		parent, "subobject-initialization " + types_.description(type) + " " +
		(member != nullptr ? member->name : "[" + decimal(index) + "]"));
	node.fact.kind = FactKind::SubobjectInitialization;
	node.fact.type = type;
	node.fact.spelled = type;
	node.fact.category = ValueCategory::LValue;
	node.fact.entity = const_cast<SemaEntity*>(member);
	node.fact.value = index;
	return node;
}

void SemaAnalyzer::aggregate_from_list(TypeId type, const AstNode& list,
                                       const Context& ctx, DumpNode& node)
{
	Clauses clauses(&list, *this, ctx);
	if (types_.is_class(type))
	{
		aggregate_members(type, clauses, ctx, node);
	}
	else if (!StringInitialization(*this).as_subobject(type, clauses, ctx, node))
	{
		// 8.5.2p1: an array of character type whose braces hold a string
		// literal takes the literal's own code units, and 8.5.1's walk of the
		// clauses is what every other array takes.
		aggregate_elements(type, clauses, ctx, node);
	}
	if (!clauses.spent())
	{
		// 8.5.1p6: a clause that reached no subobject initializes nothing.
		throw std::runtime_error("an initializer list has more clauses than the "
		                         "aggregate has subobjects");
	}
}

void SemaAnalyzer::aggregate_members(TypeId type, Clauses& clauses,
                                     const Context& ctx, DumpNode& parent)
{
	SemaEntity& owner = *model_.type_owner(type);
	Scope& region = *owner.scope;
	const bool is_union = one_storage(type);
	for (std::size_t index = 0; index < region.declarations.size(); ++index)
	{
		SemaEntity& member = *region.declarations[index];
		if (!declares_subobject(member, region))
		{
			continue;
		}
		DumpNode& node = open_subobject(parent, member.type, &member, 0);
		aggregate_subobject(member.type, clauses, ctx, node);
		if (is_union)
		{
			// 8.5.1p15: a union is initialized by its first member alone.
			break;
		}
	}
}

void SemaAnalyzer::aggregate_elements(TypeId array, Clauses& clauses,
                                      const Context& ctx, DumpNode& parent)
{
	const TypeId element = types_.target(array);
	const unsigned long long bound =
		types_.bounded(array) ? types_.bound(array) : 0;
	for (unsigned long long index = 0; index < bound; ++index)
	{
		if (clauses.spent() &&
		    (bound - index) * types_.object_size(element) > kZeroFillLimit)
		{
			// 8.5.1p7: every element from here on is value-initialized, and a
			// bound the source wrote as one number would otherwise describe one
			// element at a time.  The rest of the array is one fact.
			DumpNode& rest = open_subobject(parent, element, nullptr, index);
			rest.fact.op = 1;
			// The array these are elements of, which with the index this one
			// starts at is what says how many of them there are.
			rest.fact.spelled = array;
			return;
		}
		DumpNode& node = open_subobject(parent, element, nullptr, index);
		aggregate_subobject(element, clauses, ctx, node);
	}
}

void SemaAnalyzer::aggregate_subobject(TypeId type, Clauses& clauses,
                                       const Context& ctx, DumpNode& node)
{
	const TypeId bare = types_.strip_cv(type);
	const bool braced = !clauses.spent() &&
		clauses.next().kind == AstKind::BracedInitList;
	// The clause standing here is read where 14.5.3p4 put it, which for one an
	// expansion stands for is the region of its own element.  The walks that
	// take more than one clause each ask again for their own.
	const Context inner = clauses.in(ctx);
	if (types_.is_class(bare))
	{
		SemaEntity* const owner = model_.type_owner(bare);
		if (owner == nullptr || owner->scope == nullptr)
		{
			throw std::runtime_error("a subobject of the incomplete class type " +
			                         types_.description(bare) +
			                         " is initialized by a clause of an "
			                         "aggregate initializer");
		}
		if (braced && owner->aggregate)
		{
			aggregate_from_list(bare, clauses.next(), inner, node);
			++clauses.at;
			return;
		}
		const AstNode* const elided = clauses.spent()
			? nullptr
			: braced_prvalue_of(clauses.next(), bare, inner);
		if (elided != nullptr && owner->aggregate)
		{
			// 12.8p31 and 5.2.3p3: the clause is `T{...}` for the subobject's
			// own class, so it creates the subobject and its braces are the
			// ones 8.5.1p2 reads into the subaggregate.
			aggregate_from_list(bare, *elided, inner, node);
			++clauses.at;
			return;
		}
		if (elides_its_braces(bare, clauses, ctx))
		{
			// 8.5.1p11: the braces around the member's own clauses may be left
			// out, and then the clauses of the enclosing list initialize it.
			// Which clause is one of those is one question, asked here where
			// the subobject stands where it is and asked again where a by-value
			// parameter carries it - so it is one answer and not two.
			aggregate_members(bare, clauses, ctx, node);
			return;
		}
		if (owner->aggregate && clauses.spent())
		{
			// 8.5.1p7: no clause reached the subaggregate, so every member of
			// it is value-initialized - which is what the walk of its own
			// members with nothing left to take writes.
			aggregate_members(bare, clauses, ctx, node);
			return;
		}
		// 8.5.1p2 and 8.5.1p7: the subobject is one object of its class, and
		// what the clause initializes it with is what a declaration of it would
		// be initialized with.
		const AstNode* const written = clauses.spent() ? nullptr : &clauses.next();
		construct_subobject(bare, written, inner, node, written == nullptr);
		if (written != nullptr)
		{
			++clauses.at;
		}
		return;
	}
	if (types_.kind(bare) == TypeKind::Array)
	{
		if (braced)
		{
			aggregate_from_list(bare, clauses.next(), inner, node);
			++clauses.at;
			return;
		}
		if (StringInitialization(*this).as_subobject(bare, clauses, ctx, node))
		{
			return;
		}
		aggregate_elements(bare, clauses, ctx, node);
		return;
	}
	if (clauses.spent())
	{
		// 8.5.1p7: a member no clause reached is value-initialized, which the
		// node with nothing under it is.
		return;
	}
	if (braced)
	{
		// 8.5.1p2: a scalar written with braces takes the one value in them.
		initialize(clauses.next(), type, inner, node, true);
		++clauses.at;
		return;
	}
	initialize(clauses.next(), type, inner, node, true);
	++clauses.at;
}

// 8.5.1p2 over an array: the clauses initialize its elements in order, each
// where it stands rather than under a subobject step of its own - which is what
// lets the lowering name the storage once and step through it by whole
// elements.  The clauses are a cursor rather than a list because 8.5.1p11 lets
// an array *member* take them out of the enclosing list, so how many this array
// takes is its own bound and not the length of anything written.
void SemaAnalyzer::array_from_clauses(TypeId array, Clauses& clauses,
                                      const Context& ctx, DumpNode& line,
                                      bool image)
{
	const TypeId element = types_.target(array);
	const bool of_class = types_.is_class(types_.strip_cv(element));
	SemaEntity* const from_members =
		of_class ? member_constructor(element) : nullptr;
	const unsigned long long bound =
		types_.bounded(array) ? types_.bound(array) : kUnboundedClauses;
	unsigned long long taken = 0;
	for (; taken < bound && !clauses.spent(); ++taken, ++clauses.at)
	{
		const AstNode& clause = clauses.next();
		const Context inner = clauses.in(ctx);
		const bool braced = clause.kind == AstKind::BracedInitList;
		if (of_class)
		{
			if (braced && aggregate_type(element))
			{
				if (from_members != nullptr && !image)
				{
					// 8.5.1p2 and 13.3.1.7: the class declared no constructor
					// the clauses could reach, so the element is one object
					// built by the one its members give it.
					construct_from_members(*from_members, clause, inner, line);
					continue;
				}
				// 8.5.1p1: the clauses initialize the element's subobjects
				// where they are - which is what 3.6.2 gives an object with
				// static storage duration, and what a class no by-value
				// parameter list describes asks for.
				list_initialize(clause, element, inner, line, image);
				continue;
			}
			// 12.6p1 and 8.5.1p2: the element is an object of its class, so
			// what initializes it is what would initialize a declared one - a
			// constructor its class declared, or 12.8p31's copy of a value of
			// its own type.  3.6.2p2 folds that call where the object holds
			// what it writes before the program runs.
			construct_subobject(element, &clause, inner, line, false);
			continue;
		}
		if (braced && types_.kind(types_.strip_cv(element)) == TypeKind::Array)
		{
			// 8.5.1p3: an element that is itself an array takes the list
			// written for it, and what 3.6.2 says about the whole object says
			// the same about every element of it.
			list_initialize(clause, element, inner, line, image);
			continue;
		}
		if (types_.kind(types_.strip_cv(element)) == TypeKind::Array &&
		    StringInitialization(*this).as_object(element, clause, inner, line))
		{
			// 8.5.2p1: an element that is an array of character type takes the
			// code units of the string literal written for it, whether or not
			// braces were written around that literal - `{"x"}` for an element
			// and `"x"` for one are the same initialization, and the one place
			// the braces would have been read is this clause.
			continue;
		}
		initialize(clause, element, inner, line, true);
	}
	for (; of_class && types_.bounded(array) && taken < bound; ++taken)
	{
		// 8.5.1p7: an element no clause reached is value-initialized, which for
		// one of class type is the constructor 8.5p8 gives it and not a span of
		// zero bytes - the object's lifetime begins with that call, and 12.4p8
		// ends the lifetime of every element whether a clause reached it or
		// not.  A class with no default constructor is what this refuses, where
		// writing the zero would have accepted it.
		const unsigned long long rest = bound - taken;
		if (rest > kArrayLoopLimit)
		{
			// Every one of them is that same one call, so past the count a
			// reader wants to see written out they are one action and the bound
			// is what says how many - which is what the source wrote, one
			// number.
			construct_subobject(element, nullptr, ctx, line, true, rest);
			break;
		}
		construct_subobject(element, nullptr, ctx, line, true);
	}
}

SemaAnalyzer::Value SemaAnalyzer::list_initialize(const AstNode& node,
                                                  TypeId target,
                                                  const Context& ctx,
                                                  DumpNode& parent, bool image)
{
	return list_initialize_into(node, target, ctx,
	                            model_.open_node(parent, std::string()), image);
}

SemaAnalyzer::Value SemaAnalyzer::list_initialize_into(const AstNode& node,
                                                       TypeId target,
                                                       const Context& ctx,
                                                       DumpNode& line,
                                                       bool image)
{
	const TypeId wanted = types_.is_reference(target)
		? types_.target(target)
		: types_.strip_cv(target);
	line.text = spell("braced-init-list", ValueCategory::LValue, target,
	                  std::string());
	line.fact.kind = FactKind::BracedInitList;
	line.fact.type = target;
	line.fact.spelled = target;
	line.fact.category = ValueCategory::LValue;
	if (types_.kind(wanted) == TypeKind::Array && node.children.size() == 1 &&
	    StringInitialization(*this).units_into(wanted, *node.children[0], ctx,
	                                           line))
	{
		// 8.5.2p1: the braces hold a string literal and the array is of
		// character type, so what initializes the elements is the literal's own
		// code units - the same initialization 8.5p14 gives the array where the
		// braces were left out, and not 8.5.1's walk of a list of clauses.  The
		// braces the program wrote are the list, so the elements stand under
		// this node rather than under one opened inside it: a reader of a list
		// has one shape to know, whether the clauses were units or expressions.
		return value_of_list(line, wanted, target);
	}
	if (types_.is_class(wanted))
	{
		// 8.5.1p1: the clauses initialize the members of the aggregate in
		// declaration order, and the analysis says which clause reached which
		// subobject so that nothing below has to work it out again.
		SemaEntity* const owner = model_.type_owner(wanted);
		if (owner == nullptr || owner->scope == nullptr || !owner->aggregate)
		{
			throw std::runtime_error(types_.description(wanted) + " is not an "
			                         "aggregate and is not initialized by a "
			                         "braced-init-list in this milestone");
		}
		line.text = spell("aggregate-initialization", ValueCategory::PRValue,
		                  target, std::string());
		line.fact.kind = FactKind::AggregateInitialization;
		line.fact.category = ValueCategory::PRValue;
		aggregate_from_list(wanted, node, ctx, line);
	}
	else if (types_.kind(wanted) == TypeKind::Array)
	{
		// 8.5.1p2 and 8.5.1p3: the clauses initialize the elements in order,
		// and an element that is itself an aggregate takes the list written
		// for it.  8.5.1p6 leaves no element for a clause beyond the last.
		Clauses clauses(&node, *this, ctx);
		if (types_.bounded(wanted) && clauses.list.size() > types_.bound(wanted))
		{
			throw std::runtime_error("an array initializer has more clauses "
			                         "than the array has elements");
		}
		array_from_clauses(wanted, clauses, ctx, line, image);
	}
	else
	{
		Clauses clauses(&node, *this, ctx);
		if (clauses.list.size() > 1)
		{
			throw std::runtime_error("a braced-init-list initializes a scalar "
			                         "with more than one value");
		}
		if (!clauses.spent())
		{
			initialize(clauses.next(), wanted, clauses.in(ctx), line, true);
		}
	}
	return value_of_list(line, wanted, target);
}

// The value a braced-init-list standing where an expression does comes to,
// which is the object its clauses initialized.
SemaAnalyzer::Value SemaAnalyzer::value_of_list(DumpNode& line, TypeId wanted,
                                                TypeId target)
{
	Value value;
	value.type = wanted;
	value.spelled = target;
	value.category = ValueCategory::PRValue;
	value.node = &line;
	return value;
}

// 8.5.1p2 and 13.3.1.7: the object an aggregate class holds where it is an
// object of its own rather than a subobject an enclosing list reaches into -
// an element of an array of the class, a temporary, an argument.  The clauses
// are still 8.5.1's, so the walk above is what says which clause reaches which
// member; what differs is that the class's own storage is written by a call
// rather than in place, and the constructor making that call is one the
// standard gives the class from its members.

// 12.8p31 and 5.2.3p3: an initializer written `T{...}` for an object that is
// itself of `T` creates that very object - the prvalue and what it initializes
// are one, so nothing stands between the braces and the object.  What is left
// initializing it is the braced-init-list, which is the same list a declarator
// could have carried: 8.5.1 gives its clauses to the members of an aggregate
// and 13.3.1.7 gives them to a constructor of any other class.  Told from
// `T(...)` by the one braced clause standing where the arguments do, which is
// exactly what 5.2.3p3 wrote.
const AstNode* SemaAnalyzer::braced_prvalue_of(const AstNode& written,
                                               TypeId type, const Context& ctx)
{
	if (written.kind != AstKind::CallExpression || written.children.empty() ||
	    written.children[0]->kind != AstKind::IdExpression ||
	    !types_.is_class(types_.strip_cv(type)))
	{
		return nullptr;
	}
	const AstNode* const list = call_arguments(written);
	if (list == nullptr || !list->braced)
	{
		return nullptr;
	}
	SemaEntity* const named =
		resolve(written.children[0]->text, ctx, LookupKind::Type);
	if (named == nullptr || !names_a_type(*named) ||
	    types_.strip_cv(named->type) != types_.strip_cv(type))
	{
		return nullptr;
	}
	return list->children[0];
}

// 8.5.1p2 and 13.3.1.7: the constructor an object of an aggregate class is
// built by where it is an object of its own - an element of an array of the
// class, a temporary, an argument - rather than a subobject the enclosing
// list's clauses reach into.  The class declared no constructor that could take
// the clauses, so the one it is given takes its non-static data members: each
// by value, in the order 9.2p13 laid them out, under the name its declaration
// wrote.  The class owns it and it is declared once, so n elements of an array
// are n calls of one function rather than n copies of the same stores.
SemaEntity* SemaAnalyzer::member_constructor(TypeId type)
{
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	if (owner == nullptr || owner->scope == nullptr || !owner->aggregate)
	{
		return nullptr;
	}
	if (owner->member_constructor != nullptr)
	{
		return owner->member_constructor;
	}
	Scope& scope = *owner->scope;
	// 8.5.1p15: a union is initialized by its first member alone, so that is
	// the one subobject a parameter of this constructor carries.  Every member
	// of a union stands in the same storage, and a parameter list holding all
	// of them would write each of them into it in turn.
	const bool is_union = one_storage(type);
	std::vector<TypeId> types;
	types.push_back(types_.pointer_to(owner->type));
	std::vector<Parameter> named;
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		const TypeId bare = types_.strip_cv(member.type);
		const bool of_array = types_.kind(bare) == TypeKind::Array;
		const TypeId carried = of_array ? types_.element_of(bare) : bare;
		if (types_.is_class(carried) &&
		    selected_transfer(carried, kMoveConstructorTransfer) == nullptr)
		{
			// 12.8p11: a class member the parameter holding it cannot be
			// carried out of is one no such constructor can pass, so the
			// clauses initialize it where it stands instead.  An array of such
			// a class is the same answer: what the parameter would carry is the
			// elements, one transfer each.
			return nullptr;
		}
		Parameter one;
		// 8.3.5p5: the parameter is what a declaration of it with the member's
		// own type would be, so the member's own cv-qualification is dropped.
		// An array is the one place that adjustment cannot be taken at its
		// word: the pointer it leaves carries no elements, and what the member
		// holds is the array - so this place is the array itself, crossing the
		// boundary as the address of the caller's and copied into the member by
		// the callee.  No declaration can write such a parameter, which is why
		// it is the object model rather than 8.3.5 that declares this one.
		one.type = of_array ? bare : types_.adjust_parameter(member.type);
		one.name = member.name;
		named.push_back(one);
		types.push_back(one.type);
		if (is_union)
		{
			break;
		}
	}
	const std::string spelled =
		QualifiedName(types_.user_name(owner->type)).last();
	SemaEntity& constructor = model_.create(
		SemaKind::Function, spelled,
		types_.function_of(types_.fundamental(FT_VOID), types, false));
	name_in_region(constructor, scope, spelled);
	constructor.object_member = true;
	// 7.1.2p3: the definition is one the standard rather than the program
	// writes, so it belongs to every unit that needs one.
	constructor.inline_function = true;
	constructor.tail = &constructor;
	constructor.special = kConstructorFunction;
	constructor.defaulted = true;
	constructor.member_entry = true;
	constructor.region = &scope;
	constructor_parameters_[constructor.id] = named;
	owner->member_constructor = &constructor;
	return &constructor;
}

// 8.5.1p11: whether the clause initializes the whole subaggregate rather than
// the first of its members.  8.5.1p2 copy-initializes the member from the
// clause, so a value of the member's own class type - or of a class derived
// from it, which 12.8p31 and 4.10p3 reach the same object through - is what
// initializes it on its own; anything else is a clause of the enclosing list
// that 8.5.1p11's elided braces left standing for the subaggregate's first
// member.  The clause is read into a line nothing keeps, because which subobject
// it belongs under is exactly what this answers.
bool SemaAnalyzer::clause_initializes_class(TypeId type, const AstNode& clause,
                                            const Context& ctx)
{
	if (clause.kind == AstKind::BracedInitList)
	{
		return true;
	}
	DumpNode scratch;
	Value value;
	try
	{
		value = probe_expression(clause, ctx, scratch);
	}
	catch (const std::runtime_error&)
	{
		// A clause that is no expression of its own here is one the members
		// below read; whatever is wrong with it is reported where it is read.
		return false;
	}
	const TypeId from = types_.strip_cv(value.type);
	return from == types_.strip_cv(type) || Derivation(*this).base_in(from, type) != nullptr;
}

// 8.5.1p2: the object an aggregate class's own constructor builds, whose
// arguments are what the clauses initialize its members with.  Which clause
// reaches which member is 8.5.1's walk of the list rather than 13.3's matching,
// and there is one candidate, so the call is written here rather than chosen.
// 8.5.1p7 gives a member no clause reached the value-initialization every other
// subobject of an aggregate gets.
void SemaAnalyzer::construct_from_members(SemaEntity& constructor,
                                          const AstNode& list,
                                          const Context& ctx, DumpNode& parent)
{
	Clauses clauses(&list, *this, ctx);
	construct_from_clauses(constructor, clauses, ctx, parent);
	if (!clauses.spent())
	{
		// 8.5.1p6: a clause that reached no member initializes nothing.
		throw std::runtime_error("an initializer list has more clauses than the "
		                         "aggregate has subobjects");
	}
}

// The same call, taking what it needs out of a list the caller is still
// walking: 8.5.1p11 lets a subaggregate's braces be left out, and then the
// clauses of the enclosing list initialize its members, so how many of them
// this one takes is what its own walk arrives at rather than a count anything
// worked out in front of it.
void SemaAnalyzer::construct_from_clauses(SemaEntity& constructor,
                                          Clauses& clauses, const Context& ctx,
                                          DumpNode& parent)
{
	Scope& scope = *constructor.region;
	const std::vector<TypeId>& parameters = types_.parameters(constructor.type);
	DumpNode& action = model_.open_node(
		parent, "constructor-action " + constructor.dump_name);
	action.fact.kind = FactKind::ConstructorAction;
	action.fact.type = types_.target(parameters[0]);
	action.fact.entity = &constructor;
	DumpNode& call = model_.open_node(
		action,
		spell("call-expression", ValueCategory::PRValue,
		      types_.target(constructor.type), std::string()));
	set_fact(call, FactKind::Call, types_.target(constructor.type),
	         ValueCategory::PRValue);
	DumpNode& callee = model_.open_node(
		call, "callee " + constructor.dump_name + " " +
		types_.description(constructor.type));
	set_fact(callee, FactKind::Callee, constructor.type, ValueCategory::LValue);
	callee.fact.entity = &constructor;
	// The object the call runs on is named by the place asking for it - the
	// element of the array, the storage a temporary took - so the call holds
	// the place that argument stands in and nothing else.
	model_.open_node(call, std::string());
	std::size_t at = 1;
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		if (at >= parameters.size())
		{
			// 8.5.1p15: the constructor holds one parameter per member it
			// carries, and a union carries its first member alone - every other
			// member of it stands in that same storage and no clause reaches
			// one.  The parameter list is what says how far the walk goes.
			break;
		}
		if (types_.kind(types_.strip_cv(parameters[at])) == TypeKind::Array)
		{
			// 8.3.5p5 and 8.5.1p11: the member is an array, so what the
			// parameter carries is an array object of the caller's - built out
			// of the run of clauses the member's own elements take, whether or
			// not the braces around them were written, and 8.5.1p7's
			// value-initialization where none is left.
			array_argument(parameters[at], clauses, ctx, call);
			++at;
			continue;
		}
		if (clauses.spent())
		{
			// 8.5.1p7: no clause reached the member, so what the constructor is
			// passed for it is the value-initialization of its own type.
			DumpNode& zero = model_.open_node(
				call, spell("literal", ValueCategory::PRValue, parameters[at],
				            "0"));
			set_fact(zero, FactKind::Literal, parameters[at],
			         ValueCategory::PRValue);
			zero.fact.constant = true;
			++at;
			continue;
		}
		if (elides_its_braces(member.type, clauses, ctx))
		{
			// 8.5.1p11: the braces the member's own clauses would have stood in
			// were left out, so what initializes it is the run of clauses its
			// subobjects take out of this list - and the parameter carrying it
			// is one object of its class, built where the argument stands by
			// the constructor 8.5.1 gives that class from its members.
			elided_subaggregate(member.type, clauses, ctx, call);
			++at;
			continue;
		}
		initialize(clauses.next(), parameters[at], clauses.in(ctx), call, true,
		           Requested::Argument);
		++clauses.at;
		++at;
	}
	// 12.1 and the ABI: what the action creates is a complete object, so it
	// runs the complete-object entry of the constructor.
	constructor.complete_object_entry = true;
	demand_constructor_definition(constructor);
}

// 8.5.1p2 and 8.3.5p5: the argument the parameter carrying an array member is
// passed, which is an array object of the caller's own.  It is an object rather
// than a subobject of the aggregate being built: the clauses reach its elements
// where *it* stands, so the lowering names the storage once and steps through
// it by whole elements, and the boundary carries its address.
//
// Which clauses reach it is the same question 8.5.1p11 asks of an array
// standing where it was declared - the braces around them written, left out, or
// 8.5.2p1's string literal in their place - so this is that one reading, asked
// of the cursor the enclosing list is being walked with.
void SemaAnalyzer::array_argument(TypeId array, Clauses& clauses,
                                  const Context& ctx, DumpNode& call)
{
	DumpNode& line = model_.open_node(call, std::string());
	if (!clauses.spent() && clauses.next().kind == AstKind::BracedInitList)
	{
		// 8.5.1p11: the braces the elements stand in were written, so the one
		// clause is the whole of the array.
		list_initialize_into(clauses.next(), array, clauses.in(ctx), line, false);
		++clauses.at;
		return;
	}
	line.text = spell("braced-init-list", ValueCategory::LValue, array,
	                  std::string());
	set_fact(line, FactKind::BracedInitList, array, ValueCategory::LValue);
	if (!clauses.spent() &&
	    StringInitialization(*this).as_object(array, clauses.next(),
	                                          clauses.in(ctx), line))
	{
		// 8.5.2p1: the clause is a string literal for this array, so what the
		// elements hold is the code units it was made of.
		++clauses.at;
		return;
	}
	array_from_clauses(array, clauses, ctx, line, false);
}

// 8.5.1p11: whether this member is one whose braces the program left out, so
// that the clauses standing next initialize *its* members rather than it.  Only
// a subaggregate has braces to leave out, and only a clause that could not have
// been the whole of one is one of the clauses left standing - which is the same
// question `aggregate_subobject` asks where the subobject is written where it
// stands rather than passed as a parameter.
bool SemaAnalyzer::elides_its_braces(TypeId type, Clauses& clauses,
                                     const Context& ctx)
{
	const TypeId bare = types_.strip_cv(type);
	if (clauses.spent() || clauses.next().kind == AstKind::BracedInitList ||
	    !types_.is_class(bare) || !aggregate_type(bare))
	{
		return false;
	}
	if (clause_capacity(bare) == 0)
	{
		// 8.5.1p11: what elided braces leave standing are the clauses the
		// subaggregate's own subobjects take, so a subaggregate that holds
		// nothing has no braces to leave out - the clause standing here reaches
		// past it to nothing, and writing `{}` is the only way to name it.
		return false;
	}
	if (braced_prvalue_of(clauses.next(), bare, clauses.in(ctx)) != nullptr)
	{
		// 5.2.3p3: the clause is `T{...}` for the member's own class, so its
		// braces are written and it is the whole of the subaggregate.
		return false;
	}
	return !clause_initializes_class(bare, clauses.next(), clauses.in(ctx));
}

// 8.5.1p11 and 8.5.1p2: the subaggregate whose braces were left out, standing
// where the parameter carrying it does.  It is an object of its own - the
// clauses initialize *its* members and not the enclosing object's - so it is
// built the way every other prvalue of an aggregate class is, by the one
// constructor 8.5.1 gives the class, taking what it needs out of the same walk.
void SemaAnalyzer::elided_subaggregate(TypeId type, Clauses& clauses,
                                       const Context& ctx, DumpNode& call)
{
	const TypeId object_type = types_.strip_cv(type);
	SemaEntity* const from_members = member_constructor(object_type);
	if (from_members == nullptr)
	{
		throw std::runtime_error("a braced-init-list leaves out the braces of a "
		                         "subobject of " +
		                         types_.description(object_type) +
		                         ", which no by-value parameter list describes");
	}
	DumpNode& line = model_.open_node(call, std::string());
	SemaEntity& object = model_.create(SemaKind::Variable, std::string(),
	                                   object_type);
	object.object_member = false;
	set_fact(line, FactKind::TemporaryObject, object_type,
	         ValueCategory::PRValue);
	line.fact.entity = &object;
	// 8.5.3p5: what asked for the object is the argument, so that is what its
	// storage is named after.
	line.fact.spelling = requested_prefix(Requested::Argument, false,
	                                      object_type);
	construct_from_clauses(*from_members, clauses, ctx, line);
	// 12.2p3: the object stands until the end of the full-expression the list
	// was written in, exactly as a `T{...}` argument of the same class does.
	register_temporary(line, ctx.scope);
	line.text = spell("temporary-object", ValueCategory::PRValue, object_type,
	                  std::string());
}


// 4.8p1 and 8.5.4p7: whether an object of `to` holds `held` at all.  The second
// bullet's exception is "within the range of values that can be represented,
// *even if it cannot be represented exactly*" - so rounding is no part of this
// question and 4.8p1's overflow is the whole of it: `float a{0.1}` is the
// clause it is and `float a{1e300}` is not.
//
// What the clause is worth is the fold's own answer, so a literal, a name the
// translation knows, a call it ran and an operator over any of them are one
// question here; a clause that is no constant expression never reaches this,
// because that half of the exception is what its caller has already asked.
bool SemaAnalyzer::floating_fits(long double held, TypeId to)
{
	long double narrowed = held;
	switch (types_.fundamental_type(types_.strip_cv(to)))
	{
	case FT_FLOAT: narrowed = static_cast<float>(held); break;
	case FT_DOUBLE: narrowed = static_cast<double>(held); break;
	default: break;
	}
	return std::isfinite(narrowed);
}

// 8.5.4p7: an implicit conversion that a list-initialization may not make,
// because it cannot be relied on to keep the value the clause wrote.  A
// constant the translation knows is judged by that value rather than by the
// range of the type it was written with.
void SemaAnalyzer::require_no_narrowing(const AstNode& written,
                                        const Value& value, TypeId target,
                                        const Context& ctx)
{
	const TypeId from = types_.strip_cv(
		types_.kind(value.type) == TypeKind::Enum ? types_.target(value.type)
		                                          : value.type);
	const TypeId to = types_.strip_cv(target);
	if (!types_.is_arithmetic(from) || !types_.is_arithmetic(to))
	{
		return;
	}
	// 8.5.4p7: the exception every bullet but the first carries is a source
	// that is a constant expression, which is judged by the value it has rather
	// than by the range of the type it was written with.
	bool known = value.constant;
	unsigned long long bits = value.value;
	// 3.9.1p8: which of the two the clause is worth is what its type says, and
	// both travel together because one fold answers for either.
	long double real = value.real;
	const bool from_float = types_.is_floating(from);
	const bool to_float = types_.is_floating(to);
	// Asking the fold costs a walk of the clause, and two of 8.5.4p7's four
	// bullets settle without it: a floating source reaching an integer narrows
	// however constant it is, and one reaching a floating type at least as wide
	// narrows not at all.  So a list of n clauses pays for the folds its own
	// bullets ask for rather than one per clause.
	const bool asks_the_value = !from_float ||
		(to_float && types_.object_size(to) < types_.object_size(from));
	if (!known && asks_the_value)
	{
		try
		{
			const Constant folded = convert(evaluate(written, ctx), from);
			known = true;
			bits = folded.bits;
			real = folded.real;
		}
		catch (const NotConstant&)
		{
			known = false;
		}
	}
	bool narrows = false;
	if (from_float && !to_float)
	{
		// 8.5.4p7 first bullet: no floating type converts to an integer here.
		narrows = true;
	}
	else if (from_float && to_float)
	{
		// 8.5.4p7 second bullet: a wider floating type narrows a narrower one,
		// unless the source is a constant expression whose value the narrower
		// one has room for.  3.9.1p8's value is one the fold carries, so that
		// exception is asked of what the clause came to and not of the digits
		// some operand of it was written with - which is what makes `float f{d}`
		// off a `constexpr double d = 1.5;` the clause it is.
		narrows = types_.object_size(to) < types_.object_size(from) &&
			!(known && floating_fits(real, to));
	}
	else if (!from_float && to_float)
	{
		narrows = !known;
	}
	else if (!known)
	{
		// 8.5.4p7 fourth bullet: the destination has to hold every value the
		// source type has, which needs its width and, at equal width, its
		// signedness.
		const unsigned long long wide = types_.object_size(from);
		const unsigned long long room = types_.object_size(to);
		narrows = room < wide ||
			(room == wide && is_signed(from) != is_signed(to));
	}
	else
	{
		// The value is known, so the only question is whether the destination
		// holds it, which is whether it survives the round trip.
		Constant held;
		held.type = from;
		held.bits = bits;
		narrows = convert(convert(held, to), from).bits != held.bits;
	}
	if (narrows)
	{
		throw std::runtime_error("a braced-init-list narrows the value of a "
		                         "clause to the type it initialises");
	}
}

// 8.5.1p11: how many clauses of the *enclosing* list a subobject of `type` can
// take.  Its braces may have been left out, and then it takes its own capacity;
// they may equally have been written, and then it takes the one clause they
// are - so a subobject that holds nothing at all still takes a clause, which is
// what makes `{ {}, 7 }` two clauses of a class whose first member is empty.
unsigned long long SemaAnalyzer::clauses_a_subobject_takes(TypeId type)
{
	const unsigned long long own = clause_capacity(type);
	return own == 0 ? 1 : own;
}

// 8.5.1p6 and 8.5.1p11: how many clauses an object of `type` can take at most.
// A subobject that is itself an aggregate or an array may have had its braces
// left out, so what it contributes is its own capacity rather than one clause;
// anything else takes exactly one.  The answer is a fact of the type and is
// held per type, so the walk costs the subobject tree once however many lists
// ask about it.
unsigned long long SemaAnalyzer::clause_capacity(TypeId type)
{
	const TypeId bare = types_.strip_cv(type);
	const std::unordered_map<TypeId, unsigned long long>::const_iterator held =
		clause_capacity_.find(bare);
	if (held != clause_capacity_.end())
	{
		return held->second;
	}
	// A recursive type cannot stand as a subobject of itself, so the guard is
	// only against a walk this translation would never finish.
	clause_capacity_[bare] = 1;
	unsigned long long total = 1;
	if (types_.kind(bare) == TypeKind::Array)
	{
		if (!types_.bounded(bare))
		{
			// 8.3.4p3: the bound is what the list says, so nothing bounds it.
			total = kUnboundedClauses;
		}
		else
		{
			const unsigned long long each =
				clauses_a_subobject_takes(types_.target(bare));
			const unsigned long long bound = types_.bound(bare);
			total = bound > kUnboundedClauses / each ? kUnboundedClauses
			                                         : bound * each;
		}
	}
	else if (types_.is_class(bare))
	{
		SemaEntity* const owner = model_.type_owner(bare);
		if (owner != nullptr && owner->scope != nullptr && owner->aggregate)
		{
			Scope& region = *owner->scope;
			const bool is_union = one_storage(bare);
			total = 0;
			for (std::size_t index = 0; index < region.declarations.size();
			     ++index)
			{
				SemaEntity& member = *region.declarations[index];
				if (!declares_subobject(member, region))
				{
					continue;
				}
				const unsigned long long each =
					clauses_a_subobject_takes(member.type);
				total = total > kUnboundedClauses - each ? kUnboundedClauses
				                                         : total + each;
				if (is_union)
				{
					// 8.5.1p15: a union is initialized by its first member.
					break;
				}
			}
		}
	}
	clause_capacity_[bare] = total;
	return total;
}

bool SemaAnalyzer::list_initializes(TypeId type, std::size_t clauses)
{
	const TypeId bare_class = types_.strip_cv(type);
	SemaEntity* const owner = model_.type_owner(bare_class);
	if (owner == nullptr || owner->scope == nullptr)
	{
		// 3.9p6: an object of an incomplete class is one no initialization
		// reaches.
		return false;
	}
	if (owner->aggregate)
	{
		// 8.5.1p2 and 8.5.1p6: the clauses initialize the subobjects, and which
		// clause reaches which is the walk 8.5.1p11 makes - but a list with
		// more clauses than the class has leaves reaches no object of it
		// whatever that walk would do, and that is what tells `f(One)` from
		// `f(Two)` on `f({1,2})`.
		return clauses <= clause_capacity(bare_class);
	}
	// 13.3.1.7p1: the constructors of the class are the candidates and the
	// clauses are the arguments, so a class with none that takes this many
	// takes the list through none of them.  8.5.4p3 leaves an empty list to
	// the default constructor, which is the one that takes none.
	SemaEntity* const head = class_constructors(types_.strip_cv(type));
	// 9.3.1p3 put the object parameter in front of the ones a clause reaches.
	const std::size_t given = clauses + 1;
	for (SemaEntity* at = head; at != nullptr; at = at->next)
	{
		if (accepts_arity(*at, given) &&
		    (given <= types_.parameters(at->type).size() ||
		     types_.variadic(at->type)))
		{
			return true;
		}
	}
	return false;
}

SemaAnalyzer::Match SemaAnalyzer::match_list(std::size_t clauses,
                                             TypeId parameter,
                                             TypeId listed_class)
{
	Match match;
	const bool reference = types_.is_reference(parameter);
	const TypeId wanted = reference ? types_.target(parameter) : parameter;
	const TypeId bare = types_.strip_cv(wanted);
	if (reference)
	{
		// 13.3.3.1.5p5 and 8.5.3p5: the reference binds the temporary the list
		// initializes, and only an rvalue reference or a non-volatile const
		// lvalue reference binds a temporary at all.  What it binds is an
		// rvalue, which is what 13.3.3.2p3 tells the two apart by.
		const unsigned cv = types_.object_cv(wanted);
		const bool rvalue_ref =
			types_.kind(parameter) == TypeKind::RValueReference;
		if (!rvalue_ref &&
		    !((cv & kCvConst) != 0 && (cv & kCvVolatile) == 0))
		{
			return match;
		}
		match.reference = true;
		match.binds_rvalue_ref = rvalue_ref;
		match.qualified = wanted;
	}
	if (types_.is_class(bare) && listed_class != kNoType &&
	    bare == types_.strip_cv(listed_class))
	{
		// 13.3.3.1p4: the argument is the one element of 13.3.1.7's second
		// phase and is itself a list, and this parameter is one of the class
		// being initialized - so no user-defined conversion sequence is
		// considered for it, which is what keeps the class's own copy and move
		// constructors out of the set the braces were written for.
		return match;
	}
	if (types_.is_class(bare))
	{
		// 13.3.3.1.5p3 and p4: the sequence is a user-defined conversion
		// sequence whose second standard conversion sequence is the identity,
		// whether 13.3.1.7 chose a constructor of the class or 8.5.1 gave the
		// clauses to its members.  The class it initializes is what stands
		// where an ordinary sequence's constructor or conversion function
		// stands, because that is what 13.3.3.2p3 orders two of them by.
		if (!list_initializes(bare, clauses))
		{
			return match;
		}
		match.viable = true;
		match.rank = kUserConversion;
		match.second_rank = kExactMatch;
		match.list_class = bare;
		return match;
	}
	if (types_.kind(bare) == TypeKind::Array)
	{
		// 13.3.3.1.5p2's footnote: an array is only ever the type a reference
		// parameter refers to, and the list initializes it as a declaration of
		// one would - element by element, which is 8.5.1's own walk.
		match.viable = true;
		match.rank = kExactMatch;
		return match;
	}
	// 13.3.3.1.5p6: the parameter is not a class, so an empty list is the
	// identity conversion and a list of one clause converts as that clause
	// does.  More than one clause initializes no such object at all.  What the
	// clause converts *to* is not asked here: reading it would be reading it a
	// second time, with every definition it demands and every temporary it
	// makes asked for twice, so the clause is read once - where the list is
	// read for the type 13.3 chose - and 8.5.4p7 measures it there.
	if (clauses > 1)
	{
		return match;
	}
	match.viable = true;
	match.rank = clauses == 0 ? kExactMatch : kConversion;
	return match;
}

void SemaAnalyzer::initialize_from_list(Value& value, TypeId target,
                                        const Match& match, const Context& ctx,
                                        Requested by)
{
	const AstNode& list = *value.braced;
	DumpNode& line = *value.node;
	const TypeId wanted = types_.is_reference(target)
		? types_.target(target)
		: types_.strip_cv(target);
	if (!types_.is_class(types_.strip_cv(wanted)))
	{
		// 13.3.3.1.5p6: the object takes what the one clause holds, which is
		// the same reading a declarator carrying the list would get.
		value = list_initialize_into(list, target, ctx, line, false);
		return;
	}
	// 8.5.4p3 and 12.2p1: the list initializes an object of the class -
	// 13.3.1.7's constructor, or 8.5.1's clauses through the constructor an
	// aggregate is given - and no declaration named that object, so the
	// function it stands in gives it storage and what asked for it names that
	// storage.  8.5.3p5's reference then binds it.
	const TypeId object_type = types_.strip_cv(wanted);
	value = build_temporary(object_type, line, &list, nullptr, ctx,
	                        requested_prefix(by, match.reference, object_type),
	                        false, by == Requested::Written || match.reference,
	                        false, true);
	if (by == Requested::Argument && !match.reference &&
	    types_.passes_indirectly(object_type) && value.node != nullptr)
	{
		// 5.2.2p4 and 12.4p5: the object standing here *is* the parameter, and
		// the function called is what ends it - at every return and where its
		// body falls off the end.  So this side owes nothing for it on any
		// path.
		value.node->fact.destruction = nullptr;
	}
}
