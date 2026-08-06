#include "sema_analyzer.h"

#include <stdexcept>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"

// Calls, the PA12 standard-conversion subset, and overload resolution.
//
// The three belong together: 13.3.3.1 ranks an argument against a parameter,
// 13.3.3 picks the candidate no other beats, and 5.2.2 is what asks.  Keeping
// them in one place keeps one answer to "does this argument go here", which the
// four contexts that initialise an object - a variable, a condition, a return
// statement and an argument - all ask in the same words.
//
// Candidates are the declaration chain of one name, so collecting them is a
// walk of what has been declared rather than a search, and resolution costs one
// pass per candidate over the arguments already analysed.

namespace
{

// 8.5.1p7: how many bytes of value-initialized array elements are still
// described one element at a time.  Past this the elements stop being what
// describes the initialization and the storage starts being it - and a bound
// the source wrote as one number would otherwise cost one node per element.
const unsigned long long kZeroFillLimit = 64;

// The ranks of 13.3.3.1.1 Table 12.
const int kExactMatch = 0;
const int kPromotion = 1;
const int kConversion = 2;
// 13.3.3.2p2: a user-defined conversion sequence is worse than every standard
// conversion sequence and better than the ellipsis, so it stands between them.
const int kUserConversion = 3;
const int kEllipsis = 4;

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

const AstNode* arguments_of(const AstNode& node)
{
	const AstNode* list = child_kind(node, AstKind::ArgumentList);
	return list != nullptr ? list : child_kind(node, AstKind::ParenArgumentList);
}

}

// 4.4p4: a pointer to `cv1 T` converts to a pointer to `cv2 T` when every level
// of `cv2` holds every qualifier of the matching level of `cv1`, and `const`
// appears at every shallower level of `cv2` above the first that differs.
bool SemaAnalyzer::qualification_convertible(TypeId from, TypeId to)
{
	TypeId source = from;
	TypeId target = to;
	// Whether every level above this one is const in the target, which is what
	// 4.4p4's second condition asks about a level whose qualifiers differ.
	bool above_all_const = true;
	for (;;)
	{
		const bool descend = types_.kind(source) == TypeKind::Pointer &&
			types_.kind(target) == TypeKind::Pointer;
		if (!descend)
		{
			return types_.strip_cv(source) == types_.strip_cv(target) &&
				(types_.cv(source) & ~types_.cv(target)) == 0 &&
				(types_.cv(source) == types_.cv(target) || above_all_const);
		}
		source = types_.target(source);
		target = types_.target(target);
		if ((types_.cv(source) & ~types_.cv(target)) != 0)
		{
			return false;
		}
		if (types_.cv(source) != types_.cv(target) && !above_all_const)
		{
			return false;
		}
		above_all_const = above_all_const && (types_.cv(target) & kCvConst) != 0;
	}
}

// 10p1: the class `derived` derives from that `base` names, or null when it
// derives from no such class.  Single inheritance makes the answer one walk of
// the chain, whose length is the depth of the hierarchy and not its size.
SemaEntity* SemaAnalyzer::derived_from(TypeId derived, TypeId base)
{
	const TypeId wanted = types_.strip_cv(base);
	if (!types_.is_class(wanted) || !types_.is_class(types_.strip_cv(derived)))
	{
		return nullptr;
	}
	SemaEntity* owner = model_.type_owner(types_.strip_cv(derived));
	if (owner == nullptr || types_.strip_cv(owner->type) == wanted)
	{
		return nullptr;
	}
	for (SemaEntity* at = owner->base; at != nullptr; at = at->base)
	{
		if (types_.strip_cv(at->type) == wanted)
		{
			return at;
		}
	}
	return nullptr;
}

// 4.10 and 4.4: whether a prvalue of pointer type `from` converts to `to`.
bool SemaAnalyzer::pointer_convertible(TypeId from, TypeId to, int& rank,
                                       bool& exact)
{
	if (from == to)
	{
		rank = kExactMatch;
		exact = true;
		return true;
	}
	if (types_.kind(from) != TypeKind::Pointer ||
	    types_.kind(to) != TypeKind::Pointer)
	{
		return false;
	}
	if (qualification_convertible(from, to))
	{
		rank = kExactMatch;
		exact = true;
		return true;
	}
	const TypeId source = types_.target(from);
	const TypeId target = types_.target(to);
	// 4.10p3: a pointer to a derived class converts to a pointer to a base
	// class of it, as long as the conversion adds no qualifier the target does
	// not already carry.
	if ((types_.cv(source) & ~types_.cv(target)) == 0 &&
	    derived_from(source, target) != nullptr)
	{
		rank = kConversion;
		exact = false;
		return true;
	}
	// 4.10p2: a pointer to a cv-qualified object type converts to a pointer to
	// the same cv-qualified `void`.
	if (!types_.is_void(target) || types_.kind(source) == TypeKind::Function)
	{
		return false;
	}
	if ((types_.cv(source) & ~types_.cv(target)) != 0)
	{
		return false;
	}
	rank = kConversion;
	exact = false;
	return true;
}

SemaAnalyzer::Match SemaAnalyzer::match_by_value(const Value& argument,
                                                 TypeId parameter)
{
	Match match;
	const TypeId target = types_.strip_cv(parameter);
	const TypeId source = decayed(argument);
	if (source == target)
	{
		match.viable = true;
		match.rank = kExactMatch;
		// 13.3.3.2p3: the identity is the sequence that qualified the argument
		// least, which is what it is ordered against another sequence by.
		match.qualified = types_.kind(target) == TypeKind::Pointer
			? target
			: kNoType;
		return match;
	}
	if (types_.is_class(target))
	{
		// 13.3.3.1.2p1: the argument reaches a parameter of class type through
		// a converting constructor of that class, and the sequence that calls
		// one is worse than every sequence that calls none.
		SemaEntity* const constructor = converting_constructor(argument, target);
		if (constructor != nullptr)
		{
			match.viable = true;
			match.rank = kUserConversion;
			match.converting = constructor;
		}
		return match;
	}
	// 4.10p1: a null pointer constant converts to any pointer type and to
	// `std::nullptr_t`.
	const bool source_null = argument.null_constant ||
		types_.fundamental_type(source) == FT_NULLPTR_T;
	if (source_null && (types_.kind(target) == TypeKind::Pointer ||
	                    types_.fundamental_type(target) == FT_NULLPTR_T))
	{
		match.viable = true;
		match.rank = kConversion;
		return match;
	}
	if (types_.kind(source) == TypeKind::Pointer)
	{
		if (types_.fundamental_type(target) == FT_BOOL)
		{
			// 4.12: a pointer converts to bool, which 13.3.3.2p4 ranks below a
			// conversion that keeps the pointer.
			match.viable = true;
			match.rank = kConversion;
			match.to_bool = true;
			return match;
		}
		int rank = kConversion;
		bool exact = false;
		if (pointer_convertible(source, target, rank, exact))
		{
			match.viable = true;
			match.rank = rank;
			// 4.10p3: the pointer's value is the base subobject's address, so
			// the sequence has to say which base it reached.
			match.to_base = exact ? nullptr
			                      : derived_from(types_.target(source),
			                                     types_.target(target));
			// 4.4 and 13.3.3.2p3: a qualification conversion changes nothing
			// but the qualifiers, so the pointer it produced is what orders it
			// against another sequence that did the same.
			match.qualified = exact ? target : kNoType;
			return match;
		}
		return match;
	}
	if (!types_.is_arithmetic(source) && types_.kind(source) != TypeKind::Enum)
	{
		return match;
	}
	if (types_.fundamental_type(target) == FT_BOOL)
	{
		match.viable = true;
		match.rank = kConversion;
		return match;
	}
	if (!types_.is_arithmetic(target))
	{
		return match;
	}
	// 4.13 and 13.3.3.1.1: a promotion is a better conversion than any of the
	// conversions that change the value.
	if (promoted(source) == target && !types_.is_scoped_enum(source))
	{
		match.viable = true;
		match.rank = kPromotion;
		return match;
	}
	if (types_.is_scoped_enum(source))
	{
		// 7.2p9: a scoped enumeration has no implicit conversion to an
		// integral type.
		return match;
	}
	match.viable = true;
	match.rank = kConversion;
	return match;
}

// 13.3.3.1.2p1: the converting constructor of `target` a user-defined conversion
// sequence would call on `argument`, or null where no one constructor answers.
//
// 12.3.1p2 makes a constructor declared `explicit` no converting constructor, so
// it is not a candidate here at all.  13.3.3.1.2p1 also stops the sequence at
// one user-defined conversion, which is what leaves the constructor's own
// parameter to be reached by a standard conversion sequence alone - so a
// candidate whose parameter is itself of class type is left out unless the
// argument already is that class.  The search is one pass over the declarations
// of the name, so a class with n constructors costs n.
SemaEntity* SemaAnalyzer::converting_constructor(const Value& argument,
                                                 TypeId target)
{
	SemaEntity* const head = class_constructors(target);
	if (head == nullptr)
	{
		return nullptr;
	}
	SemaEntity* best = nullptr;
	Match chosen;
	bool tied = false;
	for (SemaEntity* at = head; at != nullptr; at = at->next)
	{
		const std::vector<TypeId>& parameters = types_.parameters(at->type);
		// 9.3.1p3 put the object 12.1 constructs in the type, so a constructor
		// that converts takes exactly one argument beside it.
		if (at->explicit_function || at->deleted || parameters.size() < 2 ||
		    !accepts_arity(*at, 2))
		{
			continue;
		}
		const TypeId wanted = parameters[1];
		const TypeId bare = types_.strip_cv(
			types_.is_reference(wanted) ? types_.target(wanted) : wanted);
		if (types_.is_class(bare) && types_.strip_cv(argument.type) != bare &&
		    derived_from(argument.type, bare) == nullptr)
		{
			// 13.3.3.1.2p1: the argument reaches this parameter only through a
			// second user-defined conversion, and one sequence holds one.
			continue;
		}
		const Match match = match_argument(argument, wanted);
		if (!match.viable || match.converting != nullptr)
		{
			continue;
		}
		if (best == nullptr || compare_matches(match, chosen) > 0)
		{
			best = at;
			chosen = match;
			tied = false;
			continue;
		}
		if (compare_matches(match, chosen) == 0)
		{
			tied = true;
		}
	}
	// 13.3.3p1: two constructors neither of which is better than the other
	// leave the conversion ambiguous, which is no viable sequence at all.
	return tied ? nullptr : best;
}

// 3.9.3p5 and 8.3.4p1: the type with every top level qualifier removed, which
// for an array is the array of unqualified elements, because an array is as
// cv-qualified as its elements are.
TypeId SemaAnalyzer::bare_type(TypeId type)
{
	if (types_.kind(type) != TypeKind::Array)
	{
		return types_.strip_cv(type);
	}
	return types_.array_of(bare_type(types_.target(type)), types_.bounded(type),
	                       types_.bound(type));
}

// 8.5.3 and 13.3.3.1.4: binding a reference parameter to an argument.
SemaAnalyzer::Match SemaAnalyzer::match_reference(const Value& argument,
                                                 TypeId parameter)
{
	Match match;
	match.reference = true;
	const bool rvalue_ref = types_.kind(parameter) == TypeKind::RValueReference;
	const TypeId referenced = types_.target(parameter);
	const TypeId source = argument.type;
	const bool is_lvalue = argument.category == ValueCategory::LValue;

	// 8.5.3p4: reference-compatible is reference-related plus a referenced type
	// at least as cv-qualified, and a compatible reference binds what it was
	// given rather than a conversion of it.
	// 8.5.3p4: reference-related also holds where the referenced type is a base
	// class of the argument's, and 13.3.3.1.4p1 makes binding such a reference
	// a derived-to-base conversion rather than an identity.
	SemaEntity* const to_base = derived_from(source, referenced);
	const bool related =
		bare_type(source) == bare_type(referenced) || to_base != nullptr;
	const bool const_lvalue_ref = (types_.cv(referenced) & kCvConst) != 0 &&
		(types_.cv(referenced) & kCvVolatile) == 0;
	if (related &&
	    (types_.object_cv(source) & ~types_.object_cv(referenced)) == 0)
	{
		// 8.5.3p5: an rvalue reference binds no lvalue directly, and an lvalue
		// reference binds an rvalue only through a non-volatile const.
		if (rvalue_ref ? is_lvalue : (!is_lvalue && !const_lvalue_ref))
		{
			return match;
		}
		match.viable = true;
		match.rank = to_base != nullptr ? kConversion : kExactMatch;
		match.binds_lvalue = is_lvalue;
		match.binds_rvalue_ref = rvalue_ref;
		match.to_base = to_base;
		// 13.3.3.2p3: two references that bound the same object differ only in
		// how qualified they made it, which is what orders them.
		match.qualified = to_base != nullptr ? kNoType : referenced;
		return match;
	}
	if (related)
	{
		// 8.5.3p5: the temporary a reference-related initializer would be
		// converted into has to be at least as cv-qualified, so dropping a
		// qualifier binds nothing.
		return match;
	}
	if (!rvalue_ref && !const_lvalue_ref)
	{
		return match;
	}

	// The argument is converted to the referenced type and the reference binds
	// the temporary that holds it.
	const Match converted = match_by_value(argument, referenced);
	if (!converted.viable)
	{
		return match;
	}
	match = converted;
	match.reference = true;
	match.binds_lvalue = false;
	match.binds_rvalue_ref = rvalue_ref;
	// 4.4p4: a qualification conversion is between two pointers to the same
	// type, so the temporary 8.5.3p5 binds holds the value the argument already
	// had.  The output writes the conversions that change what an operand is,
	// and this one changes nothing an operand of it could tell apart.
	// 13.3.3.1.2p1's temporary is the object a constructor made rather than a
	// value a cast produced, so it is written where the conversion is applied
	// and not as one more cast around the argument.
	if (match.converting == nullptr &&
	    !(types_.kind(source) == TypeKind::Pointer &&
	      qualification_convertible(source, types_.strip_cv(referenced))))
	{
		match.materialized = referenced;
	}
	return match;
}

// 8.5.3p5: whether a reference of `parameter` binds `argument` itself, rather
// than a temporary a conversion of it made or nothing at all.
bool SemaAnalyzer::binds_reference(const Value& argument, TypeId parameter)
{
	const Match match = match_reference(argument, parameter);
	return match.viable && match.materialized == kNoType && match.binds_lvalue;
}

SemaAnalyzer::Match SemaAnalyzer::match_argument(const Value& argument,
                                                 TypeId parameter)
{
	if (argument.type == kNoType && argument.functions != nullptr)
	{
		// 13.4p1: the target type chooses one of the declarations, and doing so
		// is an exact match.
		Match match;
		match.viable = resolve_target(argument, parameter) != nullptr;
		match.rank = kExactMatch;
		return match;
	}
	if (types_.is_reference(parameter))
	{
		return match_reference(argument, parameter);
	}
	return match_by_value(argument, parameter);
}

TypeId SemaAnalyzer::member_pointer_of(const SemaEntity& function)
{
	if (!function.object_member || function.region == nullptr ||
	    function.region->owner == nullptr)
	{
		return kNoType;
	}
	const std::vector<TypeId>& parameters = types_.parameters(function.type);
	const TypeId object = types_.target(parameters[0]);
	const std::vector<TypeId> written(parameters.begin() + 1, parameters.end());
	const TypeId declared = types_.qualified_function(
		types_.function_of(types_.target(function.type), written,
		                   types_.variadic(function.type)),
		types_.cv(object));
	return types_.member_pointer_to(types_.strip_cv(object), declared);
}

SemaEntity* SemaAnalyzer::resolve_target(const Value& value, TypeId target)
{
	TypeId wanted = target;
	if (types_.is_reference(wanted))
	{
		wanted = types_.target(wanted);
	}
	if (types_.kind(types_.strip_cv(wanted)) == TypeKind::MemberPointer)
	{
		// 13.4p1 lists a pointer to member among the targets that choose one
		// declaration, and the declaration it chooses is the member whose
		// pointer type is the one asked for.
		wanted = types_.strip_cv(wanted);
		for (std::size_t index = 0; index < value.functions->size(); ++index)
		{
			for (SemaEntity* at = (*value.functions)[index]; at != nullptr;
			     at = at->next)
			{
				if (member_pointer_of(*at) == wanted)
				{
					return at;
				}
			}
		}
		return nullptr;
	}
	if (types_.kind(wanted) == TypeKind::Pointer)
	{
		wanted = types_.target(wanted);
	}
	wanted = types_.strip_cv(wanted);
	if (types_.kind(wanted) != TypeKind::Function)
	{
		return nullptr;
	}
	for (std::size_t index = 0; index < value.functions->size(); ++index)
	{
		for (SemaEntity* at = (*value.functions)[index]; at != nullptr;
		     at = at->next)
		{
			if (at->type == wanted)
			{
				return at;
			}
		}
	}
	return nullptr;
}

// 13.3.3p1: one candidate is better than another when its conversion for every
// argument is at least as good and for one is better.
SemaEntity* SemaAnalyzer::select_overload(
	const std::vector<SemaEntity*>& candidates,
	const std::vector<Value>& arguments, const std::string& name,
	const Value* object, bool converting, std::size_t singles,
	const Value* operand, bool* unviable)
{
	std::vector<SemaEntity*> viable;
	// 3.4.2p2 lets one declaration be reached by more than one of the searches
	// that gathered these candidates, and 13.3p1 puts each declaration in the
	// set once.  A single chain cannot repeat itself, so the question is asked
	// only where several were gathered.
	std::unordered_set<const SemaEntity*> gathered;
	// 13.3.3p1: which of the viable candidates is a specialization of a
	// template, which is what tells two apart whose conversions tie.
	std::vector<char> templated;
	std::vector<Match> matches;
	// 13.3.1p3: every candidate is compared over the same argument list, so the
	// implicit object argument holds a place in it whether or not a candidate is
	// a non-static member.  13.3.1p4 makes that place an exact match for a
	// candidate with no implicit object parameter, so it never decides between
	// two candidates on its own.
	const std::size_t implicit = object != nullptr ? 1u : 0u;
	for (std::size_t chain = 0; chain < candidates.size(); ++chain)
	{
	// 3.4.2p2: the friend declarations an associated class makes visible are
	// gathered one at a time, because the chain each stands in is the region's
	// and holds the friend declarations of every class in it.  They are
	// gathered last, so how many of the entries are such declarations is all
	// the set has to say.
	const bool alone = chain + singles >= candidates.size();
	for (SemaEntity* candidate = candidates[chain]; candidate != nullptr;
	     candidate = alone ? nullptr : candidate->next)
	{
		if (candidates.size() > 1 && !gathered.insert(candidate).second)
		{
			continue;
		}
		SemaEntity* at = candidate;
		if (candidate->template_parameters != nullptr)
		{
			// 14.8.3p1: a template is a candidate through the specialization
			// the arguments deduce, and no candidate at all when they deduce
			// none.
			at = deduce_specialization(*candidate, arguments);
			if (at == nullptr)
			{
				continue;
			}
		}
		if (converting && at->explicit_function)
		{
			// 13.3.1.4p1: copy-initialization chooses among the converting
			// constructors alone, which the ones declared `explicit` are not.
			continue;
		}
		if (at->object_member && object == nullptr)
		{
			// 13.3.1p4: a non-static member function called with no object has
			// nothing for its implicit object parameter to bind.
			continue;
		}
		const std::vector<TypeId>& parameters = types_.parameters(at->type);
		// 9.3.1p3 put the object parameter first, so the parameters a written
		// argument may reach begin after it.  13.3.1.2p4 gives an operator's
		// non-member candidate no such parameter and hands it the same first
		// operand as its own first argument, so for either kind the parameter a
		// written argument may reach is the second.
		const std::size_t written =
			at->object_member || operand != nullptr ? 1u : 0u;
		if (parameters.size() < written)
		{
			continue;
		}
		const std::size_t declared = parameters.size() - written;
		if (!accepts_arity(*at, arguments.size() + written) ||
		    (arguments.size() > declared && !types_.variadic(at->type)))
		{
			continue;
		}
		bool ok = true;
		const std::size_t base = matches.size();
		if (implicit != 0)
		{
			Match match;
			match.viable = true;
			match.rank = kExactMatch;
			if (at->object_member)
			{
				match = match_argument(*object, parameters[0]);
			}
			else if (operand != nullptr)
			{
				match = match_argument(*operand, parameters[0]);
			}
			ok = match.viable;
			matches.push_back(match);
		}
		for (std::size_t index = 0; ok && index < arguments.size(); ++index)
		{
			Match match;
			if (index >= declared)
			{
				// 13.3.3.1.3: an argument matched by the ellipsis converts with
				// the worst rank there is.
				match.viable = true;
				match.rank = kEllipsis;
			}
			else
			{
				match = match_argument(arguments[index],
				                       parameters[index + written]);
			}
			ok = match.viable;
			matches.push_back(match);
		}
		if (!ok)
		{
			matches.resize(base);
			continue;
		}
		viable.push_back(at);
		templated.push_back(at->primary != nullptr ? 1 : 0);
	}
	}
	if (viable.empty())
	{
		if (unviable != nullptr)
		{
			// 13.3.1.2p2: an operator whose candidates are none of them viable
			// is not ill formed - what is left is the built-in operator - so
			// the caller is told rather than the program refused.
			*unviable = true;
			return nullptr;
		}
		throw std::runtime_error("no declaration of " + name +
		                         " accepts the arguments of a call");
	}

	std::size_t best = 0;
	const std::size_t count = arguments.size() + implicit;
	// One row of `count` matches per viable candidate, which for a call with no
	// arguments is no row at all, so the rows are addressed from the buffer
	// rather than from an element of it.
	const Match* const rows = matches.data();
	for (std::size_t index = 1; index < viable.size(); ++index)
	{
		if (better_candidate(rows + index * count, rows + best * count, count,
		                     templated[index] == 0, templated[best] != 0))
		{
			best = index;
		}
	}
	for (std::size_t index = 0; index < viable.size(); ++index)
	{
		if (index != best &&
		    !better_candidate(rows + best * count, rows + index * count, count,
		                      templated[best] == 0, templated[index] != 0))
		{
			throw std::runtime_error("a call of " + name +
			                         " has no best declaration");
		}
	}
	return viable[best];
}

// 13.3.3.2: whether one argument's conversion is better than another's.
int SemaAnalyzer::compare_matches(const Match& left, const Match& right)
{
	if (left.rank != right.rank)
	{
		return left.rank < right.rank ? 1 : -1;
	}
	// 13.3.3.2p4: a conversion that keeps a pointer beats one to bool.
	if (left.to_bool != right.to_bool)
	{
		return left.to_bool ? -1 : 1;
	}
	// 13.3.3.2p4: converting a pointer to a base of its class beats converting
	// it to `void*`, and reaching a nearer base beats reaching a further one -
	// the class between them is derived from the further and not from the
	// nearer.
	if (left.to_base != right.to_base)
	{
		if (left.to_base == nullptr || right.to_base == nullptr)
		{
			return left.to_base != nullptr ? 1 : -1;
		}
		if (derived_from(left.to_base->type, right.to_base->type) != nullptr)
		{
			return 1;
		}
		if (derived_from(right.to_base->type, left.to_base->type) != nullptr)
		{
			return -1;
		}
	}
	// 13.3.3.2p3: where two sequences differ only in the qualifiers they gave
	// the argument, the one whose qualifiers are a proper subset of the other's
	// is better - which is what 13.3.1.1.1 orders `f()` above `f() const` by on
	// an object that is not const, and what orders `f(T&)` above `f(const T&)`.
	if (left.qualified != kNoType && right.qualified != kNoType &&
	    left.qualified != right.qualified)
	{
		if (qualification_convertible(left.qualified, right.qualified))
		{
			return 1;
		}
		if (qualification_convertible(right.qualified, left.qualified))
		{
			return -1;
		}
	}
	if (!left.reference || !right.reference)
	{
		return 0;
	}
	// 13.3.3.2p3: an rvalue reference binding an rvalue beats an lvalue
	// reference binding it, and an lvalue reference binding an lvalue beats an
	// rvalue reference that could not.
	if (left.binds_rvalue_ref != right.binds_rvalue_ref &&
	    !left.binds_lvalue && !right.binds_lvalue)
	{
		return left.binds_rvalue_ref ? 1 : -1;
	}
	return 0;
}

bool SemaAnalyzer::better_candidate(const Match* left, const Match* right,
                                    std::size_t count, bool left_written,
                                    bool right_deduced)
{
	bool strictly_better = false;
	for (std::size_t index = 0; index < count; ++index)
	{
		const int order = compare_matches(left[index], right[index]);
		if (order < 0)
		{
			return false;
		}
		strictly_better = strictly_better || order > 0;
	}
	// 13.3.3p1: a function the program declared beats a specialization of a
	// template whose conversions are no better than its own.
	return strictly_better || (left_written && right_deduced);
}

// 12.2p1: the storage a temporary is given is named after what asked for it,
// and only a call's argument and a return's value ask.  Everywhere else the
// prvalue is read as the object the expression already wrote, so the name it
// was given where it was written is the one it keeps: a reference bound to a
// temporary by a declaration names no argument, and neither does one an
// aggregate clause or an assignment reads.
const char* SemaAnalyzer::requested_prefix(Requested by, bool reference)
{
	switch (by)
	{
	case Requested::Argument:
		// 8.5.3p5 and 5.2.2p4: a reference parameter binds the temporary the
		// argument made, and a parameter of class type is passed the object
		// itself, which 12.8p31 lets the temporary be.
		return reference ? "arg" : "argobj";
	case Requested::Returned:
		return "retobj";
	case Requested::Written:
		break;
	}
	return "tmpobj";
}

// The one place a conversion is visible in the dump: a null pointer constant
// takes the pointer type it is used as, and a reference that binds a converted
// temporary writes the cast that made it.  `by` is what asked for the
// conversion, which is what names any storage it has to give a temporary.
void SemaAnalyzer::apply_conversion(Value& value, TypeId target,
                                    const Match& match, const Context& ctx,
                                    Requested by)
{
	if (value.type == kNoType && value.functions != nullptr)
	{
		SemaEntity* chosen = resolve_target(value, target);
		if (chosen == nullptr)
		{
			throw std::runtime_error("no declaration of an overloaded function "
			                         "name has the type it is used as");
		}
		name_function(value, *chosen, "id-expression");
		return;
	}
	if (match.to_base != nullptr && value.node != nullptr)
	{
		// 4.10p3 and 8.5.3p4: what the argument became is the base class
		// subobject of the object it named, or a pointer to it, and the tree
		// names that subobject rather than leaving the address to be adjusted
		// by whoever reads the call.
		value = base_value(value, *match.to_base);
	}
	if (match.converting != nullptr && value.node != nullptr)
	{
		// 13.3.3.1.2p1: the argument reaches the parameter's class through a
		// converting constructor of it, and what the parameter is given is the
		// temporary that constructor made.  The argument keeps the place it had
		// among the operands of the call, so the temporary is written around it
		// rather than beside it, and the argument becomes what constructs it.
		const TypeId wanted = types_.strip_cv(
			types_.is_reference(target) ? types_.target(target) : target);
		Value source = value;
		DumpNode& line = model_.wrap_node(*value.node, std::string());
		source.node = line.children[0];
		line.children.clear();
		value = build_temporary(wanted, line, nullptr, &source, ctx,
		                        requested_prefix(by, match.reference), false);
		return;
	}
	if (by != Requested::Written && match.reference && !match.binds_lvalue)
	{
		// 8.5.3p5: the reference binds a temporary rather than an object the
		// argument named, and where that temporary is the prvalue itself the
		// argument is what asked for its storage.  A base subobject of it was
		// already read above, which is what leaves that temporary named after
		// the expression that wrote it rather than after this argument.
		name_argument_temporary(value, requested_prefix(by, true));
	}
	if (by != Requested::Written && !match.reference &&
	    types_.is_class(types_.strip_cv(target)) &&
	    types_.strip_cv(value.type) == types_.strip_cv(target))
	{
		// 12.8p31: the argument is a prvalue of the parameter's own class, so
		// the temporary it is may be created in the storage the call passes -
		// and then there is one object rather than an object and a copy of it.
		// A returned prvalue is the same rule read at the other end.
		name_argument_temporary(value, requested_prefix(by, false));
	}
	if (match.materialized != kNoType && value.node != nullptr)
	{
		// The operand's line moves under the cast that converted it, in the
		// place the operand already had: an argument is written where the call
		// passes it however many of the arguments beside it convert.
		value.type = match.materialized;
		value.spelled = match.materialized;
		value.category = ValueCategory::PRValue;
		value.what = "cast-expression";
		value.payload.clear();
		model_.wrap_node(*value.node,
		                 spell(value.what, value.category, value.spelled,
		                       value.payload));
		record(value);
		return;
	}
	const TypeId wanted = types_.is_reference(target)
		? types_.target(target)
		: types_.strip_cv(target);
	const bool wants_pointer = types_.kind(wanted) == TypeKind::Pointer ||
		(types_.kind(wanted) == TypeKind::Fundamental &&
		 types_.fundamental_type(wanted) == FT_NULLPTR_T);
	if (value.null_constant && wants_pointer && value.node != nullptr &&
	    value.type != wanted)
	{
		// 4.10p1: the constant is written as the pointer it stands for, and the
		// value it stands for is the only one it can be.
		value.type = wanted;
		value.spelled = wanted;
		value.category = ValueCategory::PRValue;
		value.what = "literal";
		value.payload = "0";
		respell(value);
	}
}

bool SemaAnalyzer::Clauses::spent() const
{
	return at >= list->children.size();
}

const AstNode& SemaAnalyzer::Clauses::next() const
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
	const bool is_union = types_.class_tag(type) == ClassTag::Union;
	for (std::size_t index = 0; index < region.declarations.size(); ++index)
	{
		SemaEntity& member = *region.declarations[index];
		if (member.kind != SemaKind::Variable || !member.object_member ||
		    member.region != &region)
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
	const Value literal = expression(clauses.next(), ctx, scratch);
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
		if (owner == nullptr || owner->scope == nullptr || !owner->aggregate)
		{
			throw std::runtime_error(types_.description(bare) + " is a member "
			                         "that no clause of an aggregate initializer "
			                         "initializes in this milestone");
		}
		if (braced)
		{
			aggregate_from_list(bare, clauses.next(), ctx, node);
			++clauses.at;
			return;
		}
		// 8.5.1p11: the braces around the member's own clauses may be left out,
		// and then the clauses of the enclosing list initialize it.
		aggregate_members(bare, clauses, ctx, node);
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
                                                  DumpNode& parent)
{
	const TypeId wanted = types_.is_reference(target)
		? types_.target(target)
		: types_.strip_cv(target);
	DumpNode& line = open_fact(
		parent, spell("braced-init-list", ValueCategory::LValue, target,
		              std::string()),
		FactKind::BracedInitList);
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
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			initialize(*node.children[index], element, ctx, line, true);
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
		narrows = types_.object_size(to) < types_.object_size(from) && !known;
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

SemaAnalyzer::Value SemaAnalyzer::initialize(const AstNode& node, TypeId target,
                                             const Context& ctx,
                                             DumpNode& parent, bool listed,
                                             Requested by)
{
	if (node.kind == AstKind::BracedInitList)
	{
		return list_initialize(node, target, ctx, parent);
	}
	Value value = expression(node, ctx, parent);
	const Match match = match_argument(value, target);
	if (!match.viable)
	{
		throw std::runtime_error("an expression has no conversion to the type "
		                         "it initialises");
	}
	if (listed)
	{
		require_no_narrowing(node, value, target, ctx);
	}
	apply_conversion(value, target, match, ctx, by);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::call_expression(const AstNode& node,
                                                  const Context& ctx,
                                                  DumpNode& parent)
{
	const AstNode& callee = *node.children[0];
	SemaEntity* named = nullptr;
	std::vector<SemaEntity*>* found = nullptr;
	if (callee.kind == AstKind::DecltypeSpecifier)
	{
		// 5.2.3 and 7.1.6.2p4: a call written on a decltype-specifier is an
		// explicit type conversion to the type the specifier names.
		return functional_cast(node, ctx, parent, decltype_type(callee, ctx));
	}
	if (callee.kind == AstKind::IdExpression)
	{
		// 5.2.3: a call whose callee names a type is an explicit type
		// conversion, which the grammar cannot tell from a call.
		const TypeId keyword = keyword_type(callee.text);
		if (keyword != kNoType)
		{
			return functional_cast(node, ctx, parent, keyword);
		}
		// 14.2: a callee written as a template-id names the specializations its
		// argument list makes, which 13.3 then chooses among.
		found = &model_.open_overloads();
		named = template_specializations(callee.text, ctx, *found);
		if (named == nullptr)
		{
			named = resolve(callee.text, ctx, LookupKind::Any, found);
			if (named != nullptr && names_a_type(*named))
			{
				return functional_cast(node, ctx, parent, named->type);
			}
			Value builtin;
			if (named == nullptr &&
			    builtin_call(callee.text, node, ctx, parent, builtin))
			{
				return builtin;
			}
		}
	}

	DumpNode& line = model_.open_node(parent, std::string());
	// 13.3.1p3: the object a member function is called on is an argument of the
	// call like any other, and 9.3.1p3 already made it the first parameter of
	// the member's type, so the resolved tree writes it as the first argument.
	// Its node stands before the written arguments and is dropped again where
	// the declaration chosen turns out to be a static member.
	Value object;
	Value target;
	// 5.2.5p1: a call whose callee names a member is a call of that member, so
	// what every question about the call names is the member rather than the
	// access that reached it.
	const std::string& called = callee.kind == AstKind::MemberExpression
		? callee.children[1]->text
		: callee.text;
	// 3.4.2p1: an unqualified callee also names what the types of the arguments
	// reach, and 3.4.2p3 leaves that search out where the ordinary lookup found
	// a member of a class, a function declared in a block, or anything that is
	// not a function at all.
	const bool adl = callee.kind == AstKind::IdExpression &&
		!QualifiedName(callee.text).qualified() && allows_adl(named);
	// Where nothing was found at all, the name is settled once the arguments
	// are known, and the line the callee will be written on holds its place
	// before them until then.
	DumpNode* deferred = nullptr;
	if (callee.kind == AstKind::MemberExpression)
	{
		member_callee(callee, ctx, line, target, object);
	}
	else if (named == nullptr && adl)
	{
		deferred = &model_.open_node(line, std::string());
	}
	else
	{
		// 3.4: what the callee names was looked up above to learn that it is not
		// a type, so the expression layer is handed the answer rather than
		// asking for it again.
		target = callee.kind == AstKind::IdExpression
			? named_value(callee, require(named, callee.text), line, found)
			: expression(callee, ctx, line);
		if (target.functions != nullptr && target.addressed == nullptr)
		{
			implicit_object_argument(*target.functions, line, object);
		}
	}

	const AstNode* list = arguments_of(node);
	std::vector<Value> arguments;
	for (std::size_t index = 0; list != nullptr && index < list->children.size();
	     ++index)
	{
		arguments.push_back(expression(*list->children[index], ctx, line));
	}

	TypeId function = kNoType;
	if (deferred != nullptr || (target.functions != nullptr &&
	                            target.addressed == nullptr))
	{
		// 13.3p1: the candidate set is what the lookups reached, which for an
		// unqualified call is the ordinary lookup and 3.4.2's together.
		std::vector<SemaEntity*> candidates;
		if (target.functions != nullptr)
		{
			candidates = *target.functions;
		}
		const std::size_t singles =
			adl ? argument_candidates(called, arguments, candidates) : 0;
		if (candidates.empty())
		{
			throw std::runtime_error("no declaration of " + called +
			                         " is in scope");
		}
		if (deferred != nullptr)
		{
			target.node = deferred;
		}
		// 13.3: the arguments choose one declaration, which the callee line is
		// then written from.
		SemaEntity& chosen =
			*select_overload(candidates, arguments, called,
			                 object.node != nullptr ? &object : nullptr, false,
			                 singles);
		name_function(target, chosen, "callee");
		function = target.type;
		if (chosen.special != kOrdinaryFunction)
		{
			// 5.2.4 and the ABI: a destructor a call names is named on an object
			// the program wrote, which is a complete object - so this is the
			// complete-object entry rather than the base-object one a base
			// subobject's own action runs.
			chosen.complete_object_entry = true;
		}
		if (object.node != nullptr && chosen.object_member)
		{
			arguments.insert(arguments.begin(), object);
		}
		else if (object.node != nullptr)
		{
			// 9.4p1: a static member is reached without an object, so the one
			// the call named is no argument of it - and 5.2.5p1 evaluates the
			// expression that named it either way.
			require_droppable(*object.node, called);
			line.children.erase(line.children.begin() + 1);
		}
	}
	else if (types_.kind(target.type) == TypeKind::Function)
	{
		function = target.type;
	}
	else
	{
		// 5.2.2p1: a call through a pointer to function calls what it points to.
		// 13.4p1 gives a call no target type of its own, so an overloaded name
		// that reached here through `&` is a callee that means nothing.
		require_complete_value(target);
		// 13.5.4p1: a call written on an object of class type is a call of a
		// member `operator()` of its class, which is the only kind that
		// operator may be declared as.
		if (types_.is_class(types_.strip_cv(target.type)))
		{
			std::vector<Value> operands;
			operands.push_back(target);
			for (std::size_t index = 0; index < arguments.size(); ++index)
			{
				operands.push_back(arguments[index]);
			}
			Value chosen;
			if (operator_expression(OP_LPAREN, ctx, line, operands, true,
			                        chosen))
			{
				return chosen;
			}
			throw std::runtime_error("an object of class type is called where "
			                         "its class declares no function call "
			                         "operator that accepts the arguments");
		}
		const TypeId pointer = decayed(target);
		if (types_.kind(pointer) != TypeKind::Pointer ||
		    types_.kind(types_.target(pointer)) != TypeKind::Function)
		{
			throw std::runtime_error("the callee of a call is neither a function "
			                         "nor a pointer to one");
		}
		function = types_.target(pointer);
	}

	const SemaEntity* const chosen =
		target.entity != nullptr && target.entity->kind == SemaKind::Function
			? target.entity
			: nullptr;
	return finish_call(line, function, arguments, chosen, ctx);
}

// 5.2.2p4 and 5.2.2p10: the arguments of a call converted to the types of the
// parameters they are passed to, the default-arguments 8.3.6 writes for the
// ones no argument reached, and the value the call is.  13.3 has already chosen
// the declaration; what is left is the same for a call the program wrote and
// for the one 13.3.1.2p1 makes of an operator, so both are written here.
SemaAnalyzer::Value SemaAnalyzer::finish_call(DumpNode& line, TypeId function,
                                              std::vector<Value>& arguments,
                                              const SemaEntity* chosen,
                                              const Context& ctx)
{
	const std::vector<TypeId>& parameters = types_.parameters(function);
	if ((arguments.size() < parameters.size() &&
	     (chosen == nullptr || !accepts_arity(*chosen, arguments.size()))) ||
	    (arguments.size() > parameters.size() && !types_.variadic(function)))
	{
		throw std::runtime_error("a call passes the wrong number of arguments");
	}
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (index >= parameters.size())
		{
			// 5.2.2p7: an argument matched by the ellipsis is passed as it is.
			require_complete_value(arguments[index]);
			continue;
		}
		const Match match = match_argument(arguments[index], parameters[index]);
		if (!match.viable)
		{
			throw std::runtime_error("an argument has no conversion to the type "
			                         "of the parameter it is passed to");
		}
		apply_conversion(arguments[index], parameters[index], match, ctx,
		                 Requested::Argument);
	}
	for (std::size_t index = arguments.size(); index < parameters.size(); ++index)
	{
		// 8.3.6p1: the call is read as if the default-argument had been
		// written where the argument is missing.
		write_default_argument(*chosen, index, line);
	}

	// 5.2.2p10: the result is a prvalue unless the function returns a
	// reference, and the dump writes the return type as declared.
	const TypeId result = types_.target(function);
	Value value;
	value.spelled = result;
	value.type = types_.is_reference(result) ? types_.target(result) : result;
	value.category = ValueCategory::PRValue;
	if (types_.kind(result) == TypeKind::LValueReference)
	{
		value.category = ValueCategory::LValue;
	}
	else if (types_.kind(result) == TypeKind::RValueReference)
	{
		value.category = ValueCategory::XValue;
	}
	value.node = &line;
	value.what = "call-expression";
	respell(value);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::functional_cast(const AstNode& node,
                                                  const Context& ctx,
                                                  DumpNode& parent,
                                                  TypeId target)
{
	const AstNode* list = arguments_of(node);
	const std::size_t count = list == nullptr ? 0 : list->children.size();
	Value value;
	value.type = target;
	value.spelled = target;
	value.category = ValueCategory::PRValue;
	if (types_.is_class(types_.strip_cv(target)))
	{
		// 5.2.3p1/p2 over a class: `T(a...)` and `T()` are both a prvalue of
		// class type, which 12.2p1 makes an object.  The initialization is the
		// one 8.5 and 13.3.1.3 give an object of the class, so the arguments
		// are the constructor's whatever their number, and the storage the
		// prvalue stands in is the function's.
		return materialize_temporary(target, list, ctx, parent, "tmpobj",
		                             count == 0);
	}
	if (count == 0)
	{
		// 5.2.3p2: `T()` is a prvalue of type T that is value-initialized,
		// which for the PA12 subset is the zero of that type.
		value.constant = true;
		value.what = "literal";
		value.payload = "0";
		value.node = &model_.open_node(
			parent, spell(value.what, value.category, target, value.payload));
		return value;
	}
	if (count != 1)
	{
		throw std::runtime_error("a functional cast is written with more than "
		                         "one operand");
	}
	DumpNode& line = model_.open_node(parent, std::string());
	Value source = expression(*list->children[0], ctx, line);
	if (source.type == kNoType && source.functions != nullptr)
	{
		SemaEntity* chosen = resolve_target(source, target);
		if (chosen == nullptr)
		{
			throw std::runtime_error("no declaration of an overloaded function "
			                         "name has the type a cast asks for");
		}
		name_function(source, *chosen, "id-expression");
	}
	value.what = "cast-expression";
	if (types_.is_reference(target))
	{
		// 5.2.3p1: `T(x)` is the cast `(T)x`, which for a reference type is
		// what 5.2.9p1 and 5.4p4 make of the operand.
		return cast_to_reference(target, source, parent, line, value);
	}
	value.node = &line;
	respell(value);
	return value;
}
