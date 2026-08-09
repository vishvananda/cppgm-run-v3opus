#include "sema_analyzer.h"

#include <cstring>
#include <stdexcept>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"

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
	return at >= list->children.size();
}

const AstNode& InitializerClauses::next() const
{
	return *list->children[at];
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
	Clauses clauses(list);
	if (types_.is_class(type))
	{
		aggregate_members(type, clauses, ctx, node);
	}
	else
	{
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

bool SemaAnalyzer::string_initialized(TypeId array, Clauses& clauses,
                                      const Context& ctx, DumpNode& parent)
{
	if (clauses.spent() || clauses.next().kind != AstKind::Literal)
	{
		return false;
	}
	const TypeId element = types_.strip_cv(types_.target(array));
	if (!types_.is_integral(element) || types_.object_size(element) == 0)
	{
		return false;
	}
	DumpNode scratch;
	const Value literal = probe_expression(clauses.next(), ctx, scratch);
	if (types_.kind(types_.strip_cv(literal.type)) != TypeKind::Array ||
	    literal.node == nullptr || literal.node->fact.spelling.empty())
	{
		return false;
	}
	// 8.5.2p1: the code units of the literal initialize the elements, and the
	// elements past them are zero as any other unreached element is.
	const std::string& data = literal.node->fact.spelling;
	const unsigned long long width = types_.object_size(element);
	const unsigned long long bound =
		types_.bounded(array) ? types_.bound(array) : 0;
	const unsigned long long written = data.size() / width;
	if (written > bound)
	{
		throw std::runtime_error("a string literal initializes an array that is "
		                         "too short to hold it");
	}
	for (unsigned long long index = 0; index < bound; ++index)
	{
		DumpNode& node = open_subobject(parent, element, nullptr, index);
		if (index >= written)
		{
			continue;
		}
		unsigned long long bits = 0;
		for (unsigned long long byte = 0; byte < width; ++byte)
		{
			bits |= static_cast<unsigned long long>(
				static_cast<unsigned char>(data[index * width + byte]))
				<< (8 * byte);
		}
		Value unit;
		unit.type = element;
		unit.spelled = element;
		unit.category = ValueCategory::PRValue;
		unit.constant = true;
		unit.value = bits;
		unit.what = "literal";
		unit.payload = spell_value(element, bits);
		unit.node = &model_.open_node(
			node, spell(unit.what, unit.category, unit.type, unit.payload));
		record(unit);
	}
	++clauses.at;
	return true;
}

void SemaAnalyzer::aggregate_subobject(TypeId type, Clauses& clauses,
                                       const Context& ctx, DumpNode& node)
{
	const TypeId bare = types_.strip_cv(type);
	const bool braced = !clauses.spent() &&
		clauses.next().kind == AstKind::BracedInitList;
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
			aggregate_from_list(bare, clauses.next(), ctx, node);
			++clauses.at;
			return;
		}
		const AstNode* const elided = clauses.spent()
			? nullptr
			: braced_prvalue_of(clauses.next(), bare, ctx);
		if (elided != nullptr && owner->aggregate)
		{
			// 12.8p31 and 5.2.3p3: the clause is `T{...}` for the subobject's
			// own class, so it creates the subobject and its braces are the
			// ones 8.5.1p2 reads into the subaggregate.
			aggregate_from_list(bare, *elided, ctx, node);
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
		construct_subobject(bare, written, ctx, node, written == nullptr);
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
			aggregate_from_list(bare, clauses.next(), ctx, node);
			++clauses.at;
			return;
		}
		if (string_initialized(bare, clauses, ctx, node))
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
		initialize(clauses.next(), type, ctx, node, true);
		++clauses.at;
		return;
	}
	initialize(clauses.next(), type, ctx, node, true);
	++clauses.at;
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
		if (types_.bounded(wanted) && node.children.size() > types_.bound(wanted))
		{
			throw std::runtime_error("an array initializer has more clauses "
			                         "than the array has elements");
		}
		const TypeId element = types_.target(wanted);
		SemaEntity* const from_members =
			types_.is_class(types_.strip_cv(element))
				? member_constructor(element)
				: nullptr;
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			const AstNode& clause = *node.children[index];
			const bool braced = clause.kind == AstKind::BracedInitList;
			if (types_.is_class(types_.strip_cv(element)))
			{
				if (braced && aggregate_type(element))
				{
					if (from_members != nullptr && !image)
					{
						// 8.5.1p2 and 13.3.1.7: the class declared no
						// constructor the clauses could reach, so the element
						// is one object built by the one its members give it.
						construct_from_members(*from_members, clause, ctx, line);
						continue;
					}
					// 8.5.1p1: the clauses initialize the element's subobjects
					// where they are - which is what 3.6.2 gives an object with
					// static storage duration, and what a class no by-value
					// parameter list describes asks for.
					list_initialize(clause, element, ctx, line, image);
					continue;
				}
				// 12.6p1 and 8.5.1p2: the element is an object of its class, so
				// what initializes it is what would initialize a declared one -
				// a constructor its class declared, or 12.8p31's copy of a value
				// of its own type.  3.6.2p2 folds that call where the object
				// holds what it writes before the program runs.
				construct_subobject(element, &clause, ctx, line, false);
				continue;
			}
			if (braced &&
			    types_.kind(types_.strip_cv(element)) == TypeKind::Array)
			{
				// 8.5.1p3: an element that is itself an array takes the list
				// written for it, and what 3.6.2 says about the whole object
				// says the same about every element of it.
				list_initialize(clause, element, ctx, line, image);
				continue;
			}
			initialize(clause, element, ctx, line, true);
		}
		if (types_.is_class(types_.strip_cv(element)) && types_.bounded(wanted))
		{
			// 8.5.1p7: an element no clause reached is value-initialized, which
			// for one of class type is the constructor 8.5p8 gives it and not a
			// span of zero bytes - the object's lifetime begins with that call,
			// and 12.4p8 ends the lifetime of every element whether a clause
			// reached it or not.  A class with no default constructor is what
			// this refuses, where writing the zero would have accepted it.
			const unsigned long long bound = types_.bound(wanted);
			for (unsigned long long index = node.children.size(); index < bound;
			     ++index)
			{
				const unsigned long long rest = bound - index;
				if (rest > kArrayLoopLimit)
				{
					// Every one of them is that same one call, so past the count
					// a reader wants to see written out they are one action and
					// the bound is what says how many - which is what the source
					// wrote, one number.
					construct_subobject(element, nullptr, ctx, line, true, rest);
					break;
				}
				construct_subobject(element, nullptr, ctx, line, true);
			}
		}
	}
	else if (node.children.size() > 1)
	{
		throw std::runtime_error("a braced-init-list initializes a scalar with "
		                         "more than one value");
	}
	else if (!node.children.empty())
	{
		initialize(*node.children[0], wanted, ctx, line, true);
	}
	Value value;
	value.type = wanted;
	value.spelled = target;
	value.category = ValueCategory::PRValue;
	value.node = &line;
	return value;
}

// 2.14.4 and 8.5.4p7: whether the value the program spelled is the value it
// still has once an object of `to` holds it.  A floating value is not one this
// translation carries - every other place it reaches writes the digits the
// program wrote and lets the object file hold them - so this one question is
// answered by decoding those digits the way phase 7 would, and only where
// 8.5.4p7's exception asks it.  A clause that is not a literal the program
// wrote is no constant expression here, and narrows.
bool SemaAnalyzer::floating_round_trips(const Value& value, TypeId to)
{
	if (value.what == nullptr || std::strcmp(value.what, "literal") != 0 ||
	    value.payload.empty())
	{
		return false;
	}
	const long double held = std::strtold(value.payload.c_str(), 0);
	switch (types_.fundamental_type(types_.strip_cv(to)))
	{
	case FT_FLOAT:
		return static_cast<long double>(static_cast<float>(held)) == held;
	case FT_DOUBLE:
		return static_cast<long double>(static_cast<double>(held)) == held;
	default:
		return true;
	}
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
	if (!known && !types_.is_floating(from))
	{
		try
		{
			known = true;
			bits = convert(evaluate(written, ctx), from).bits;
		}
		catch (const NotConstant&)
		{
			known = false;
		}
	}
	const bool from_float = types_.is_floating(from);
	const bool to_float = types_.is_floating(to);
	bool narrows = false;
	if (from_float && !to_float)
	{
		// 8.5.4p7 first bullet: no floating type converts to an integer here.
		narrows = true;
	}
	else if (from_float && to_float)
	{
		// 8.5.4p7 second bullet: a wider floating type narrows a narrower one,
		// unless the source is a constant expression whose value after the
		// conversion is the value it had.  2.14.4's value is not one this
		// translation carries in an integer, so the question is asked of the
		// spelling the analysis kept - which is the same decode phase 7 would
		// do to write the value, and the only place the object model needs one.
		narrows = types_.object_size(to) < types_.object_size(from) &&
			!floating_round_trips(value, to);
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

bool SemaAnalyzer::list_initializes(TypeId type, const AstNode& list)
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
		return list.children.size() <= clause_capacity(bare_class);
	}
	// 13.3.1.7p1: the constructors of the class are the candidates and the
	// clauses are the arguments, so a class with none that takes this many
	// takes the list through none of them.  8.5.4p3 leaves an empty list to
	// the default constructor, which is the one that takes none.
	SemaEntity* const head = class_constructors(types_.strip_cv(type));
	// 9.3.1p3 put the object parameter in front of the ones a clause reaches.
	const std::size_t given = list.children.size() + 1;
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

SemaAnalyzer::Match SemaAnalyzer::match_list(const AstNode& list,
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
		if (!list_initializes(bare, list))
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
	if (list.children.size() > 1)
	{
		return match;
	}
	match.viable = true;
	match.rank = list.children.empty() ? kExactMatch : kConversion;
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
