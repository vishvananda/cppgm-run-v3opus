#include "sema_analyzer.h"

#include <cstdlib>
#include <cstring>
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
			return match;
		}
		// 13.3.1.4p1: the other half of that candidate set is the conversion
		// functions of the argument's own class, which reach the parameter's
		// class where no constructor of it takes the argument.
		return conversion_match(argument, parameter, false);
	}
	if (types_.is_class(types_.strip_cv(argument.type)))
	{
		// 13.3.1.5p1: an argument of class type reaches a parameter of any
		// other type only through a conversion function of its class.
		return conversion_match(argument, parameter, false);
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
			// against another sequence that did the same.  4.10p3's conversion
			// to a base is followed by that same qualification conversion where
			// the base was named more qualified than the object, and 13.3.3.2p3
			// makes the sequence without it a proper subsequence of the one with
			// it - so the pointer the sequence produced orders these too, once
			// the base each of them reached has been compared.
			match.qualified = target;
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
	if (head == nullptr || standard_only_)
	{
		// 13.3.3.1.2p1: a user-defined conversion sequence holds one
		// user-defined conversion, so the sequence measured inside one holds
		// none.
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
		// 13.3.3.1.2p1: the sequence measured inside a user-defined conversion
		// holds no user-defined conversion of its own, whichever direction the
		// second one would run - a converting constructor of a further class or
		// a conversion function of the argument's.  One flag says it for both,
		// and it is set here as well as around 13.3.1.5's second sequence
		// because a candidate parameter of built-in type is exactly where a
		// conversion function would otherwise slip a second one in.
		standard_only_ = true;
		const Match match = match_argument(argument, wanted);
		standard_only_ = false;
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

// 5.2.2p3: what a call of `chosen` hands back, as a value the ranking can read.
// 12.3.2p1 makes it the conversion-type-id, which is the function's return type,
// and 5.2.2p10 makes it a prvalue unless that type is a reference.
SemaAnalyzer::Value SemaAnalyzer::conversion_result(
	const SemaEntity& chosen) const
{
	const TypeId result = types_.target(chosen.type);
	Value value;
	value.spelled = result;
	value.type = types_.is_reference(result) ? types_.target(result) : result;
	value.category = types_.kind(result) == TypeKind::LValueReference
		? ValueCategory::LValue
		: (types_.kind(result) == TypeKind::RValueReference
		   ? ValueCategory::XValue
		   : ValueCategory::PRValue);
	return value;
}

// 12.3.2p1 and 13.3.1.5p1: the user-defined conversion sequence one of the
// argument's class's conversion functions makes to `parameter`.
//
// 13.3.1.5p1 makes the candidate set the conversion functions of that class and
// of its bases, which the class already holds as one list, and 13.3 chooses
// among them on the implicit object argument alone - so what is ranked here is
// how the argument reaches each candidate's object parameter, with the standard
// conversion sequence from what the call hands back to the parameter as
// 13.3.3p1's tie-break.  13.3.3.1.2p1 stops the whole sequence at one
// user-defined conversion, so that second sequence is measured with none
// allowed in it, which is also what keeps two classes that convert to each
// other one probe rather than a walk that does not end.
SemaAnalyzer::Match SemaAnalyzer::conversion_match(const Value& argument,
                                                   TypeId parameter,
                                                   bool direct)
{
	Match match;
	if (standard_only_)
	{
		return match;
	}
	SemaEntity* const owner =
		model_.type_owner(types_.strip_cv(argument.type));
	if (owner == nullptr || owner->conversions_above == nullptr)
	{
		return match;
	}
	std::vector<SemaEntity*> candidates;
	gather_conversions(*owner, candidates);
	// 13.3.1p3 and 9.3.1p3: the implicit object argument reaches the object
	// parameter the same way any pointer reaches a pointer, which is the one
	// place a candidate of this set can be unviable.
	Value object;
	object.type = object.spelled = types_.pointer_to(argument.type);
	object.object_category = argument.category;
	object.category = ValueCategory::PRValue;
	SemaEntity* best = nullptr;
	Match chosen_object;
	Match chosen_result;
	bool tied = false;
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		SemaEntity* const at = candidates[index];
		if (at->deleted || (at->explicit_function && !direct))
		{
			// 12.3.2p2: only a direct-initialization, an explicit cast and a
			// contextual conversion may choose a conversion function declared
			// `explicit`.
			continue;
		}
		const Match reached =
			object_match(object, *at, types_.parameters(at->type)[0]);
		if (!reached.viable)
		{
			continue;
		}
		const Value produced = conversion_result(*at);
		standard_only_ = true;
		const Match result = match_argument(produced, parameter);
		standard_only_ = false;
		if (!result.viable)
		{
			continue;
		}
		if (at->explicit_function && result.rank != kExactMatch)
		{
			// 13.3.1.5p1: a direct-initialization adds the `explicit`
			// conversion functions that yield the destination type or one a
			// *qualification* conversion reaches it from - and no further, so
			// `char c(x)` on a class with `explicit operator bool` reaches
			// nothing rather than reaching `bool` and converting it.
			continue;
		}
		// 13.3.3p1: what a conversion of an object is worth is where it gets
		// to, so the second, standard conversion sequence to the destination
		// orders these candidates and the implicit object argument tells apart
		// only the ones that get equally far - which is what makes
		// `operator int` of a base beat `operator long` of a nearer one for an
		// `int` destination, and `operator T()` beat `operator T() const` on an
		// object that is not const.
		const int order = best == nullptr
			? 1
			: (compare_matches(result, chosen_result) != 0
			   ? compare_matches(result, chosen_result)
			   : compare_matches(reached, chosen_object));
		if (order > 0)
		{
			best = at;
			chosen_object = reached;
			chosen_result = result;
			tied = false;
			continue;
		}
		if (order == 0)
		{
			tied = true;
		}
	}
	if (best == nullptr || tied)
	{
		// 13.3.3p1: two candidates neither of which is better than the other
		// leave the conversion ambiguous, which is no viable sequence at all.
		return match;
	}
	match = chosen_result;
	match.viable = true;
	match.rank = kUserConversion;
	match.second_rank = chosen_result.rank;
	match.converted = best;
	match.converting = nullptr;
	return match;
}

// 4p3: a value of class type where the language itself needs a `bool`.
//
// 12.3.2p2 lets a conversion function declared `explicit` answer this one, and
// 13.3 chooses among the class's conversion functions exactly as it chooses for
// a direct-initialization of `bool` - so the whole of the rule is the
// user-defined conversion sequence, asked for with `explicit` left in.
void SemaAnalyzer::contextual_bool(Value& value, const Context& ctx)
{
	if (value.node == nullptr || !types_.is_class(types_.strip_cv(value.type)))
	{
		return;
	}
	const TypeId wanted = types_.fundamental(FT_BOOL);
	const Match match = conversion_match(value, wanted, true);
	if (match.viable)
	{
		apply_conversion(value, wanted, match, ctx, Requested::Written);
	}
}

// 5.2.9p4, 5.4p4 and 8.5p16: an operand of class type where a
// direct-initialization or an explicit cast asked for a value of another type.
//
// Both are direct-initializations of the target from the operand, which is the
// one context 12.3.2p2 lets a conversion function declared `explicit` answer -
// so the whole of the difference from an ordinary argument conversion is that
// `explicit` is left in the candidate set.
bool SemaAnalyzer::explicit_conversion(Value& value, TypeId target,
                                       const Context& ctx)
{
	if (value.node == nullptr || !types_.is_class(types_.strip_cv(value.type)) ||
	    types_.is_class(types_.strip_cv(target)) || types_.is_reference(target))
	{
		return false;
	}
	const Match match = conversion_match(value, target, true);
	if (!match.viable)
	{
		return false;
	}
	apply_conversion(value, target, match, ctx, Requested::Written);
	return true;
}

// 5.2.9p4 and 5.4p4: a cast of an operand of class type to any other object
// type is the direct-initialization of a value of that type from the operand,
// so a conversion function of the operand's class is the whole of what carries
// it.  Where none does, the cast reaches nothing - there is no reading of the
// bytes of the object behind it, which for a class of a different size or
// layout is not a value of the target type at all.  5.2.9p4's cast to `void`
// is the one that asks for no value, and a cast to a class or to a reference is
// 8.5's initialization of one and answered where that is.
void SemaAnalyzer::cast_conversion(Value& source, TypeId target,
                                   const Context& ctx)
{
	const TypeId wanted = types_.strip_cv(target);
	if (types_.is_class(wanted) && !types_.is_reference(target) &&
	    source.node != nullptr && types_.strip_cv(source.type) != wanted)
	{
		// 5.2.9p4: the cast is well formed exactly where `T t(e);` is for an
		// invented temporary, so a cast to a class type *is* that
		// direct-initialization - a call of the constructor 13.3.1.3 chooses,
		// with 13.3.1.4 leaving the class's `explicit` constructors in, and
		// never a reading of whatever bytes the operand happens to hold.  The
		// temporary is written around the operand in the place the operand
		// already had, so the cast goes on standing where the program wrote it.
		DumpNode& line = model_.wrap_node(*source.node, std::string());
		Value operand = source;
		operand.node = line.children[0];
		line.children.clear();
		source = build_temporary(wanted, line, nullptr, &operand, ctx, "tmpobj",
		                         false, true, true);
		return;
	}
	if (explicit_conversion(source, target, ctx) ||
	    !types_.is_class(types_.strip_cv(source.type)) ||
	    types_.is_class(types_.strip_cv(target)) ||
	    types_.is_reference(target) || types_.is_void(types_.strip_cv(target)))
	{
		return;
	}
	throw std::runtime_error("a cast reads an operand of " +
	                         types_.description(source.type) + " as " +
	                         types_.description(target) +
	                         ", which no conversion function of its class "
	                         "reaches");
}

// 6.4.2p2: the condition of a switch statement, which is contextually
// *implicitly* converted - so a conversion function declared `explicit` is none
// of the candidates, and the type the statement selects on is the one the class
// converts to rather than one the context named.
void SemaAnalyzer::contextual_integral(Value& value, const Context& ctx)
{
	if (value.node == nullptr || !types_.is_class(types_.strip_cv(value.type)))
	{
		return;
	}
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(value.type));
	if (owner == nullptr)
	{
		return;
	}
	std::vector<SemaEntity*> candidates;
	gather_conversions(*owner, candidates);
	SemaEntity* found = nullptr;
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		SemaEntity* const at = candidates[index];
		TypeId result = types_.target(at->type);
		if (types_.is_reference(result))
		{
			result = types_.target(result);
		}
		result = types_.strip_cv(result);
		if (at->explicit_function || at->deleted ||
		    (!types_.is_integral(result) &&
		     types_.kind(result) != TypeKind::Enum))
		{
			continue;
		}
		if (found != nullptr)
		{
			// 6.4.2p2: a class with two of them converts to no one type, so
			// there is no value for the statement to select on.
			return;
		}
		found = at;
	}
	if (found == nullptr)
	{
		return;
	}
	value = call_conversion(value, *found, ctx);
}

// 13.6: the type a built-in operator reads an operand of class type as.
//
// 13.6 writes a candidate for every built-in operand type there is, and an
// operand of class type reaches one of them only through a conversion function
// of its class - so what the operand can be is what those conversions produce,
// and a class that produces two of them leaves no one built-in candidate that
// 13.3 could choose.  The class already holds its conversions as one list, so
// this is one walk of that list and never a search.
//
// `reference` is 13.6p3 and p5's operand: `++E` and `--E` are written over
// `VQ T&`, so what reaches them is a conversion function that hands back an
// lvalue of a type they are written for, and the type the operand takes is that
// reference rather than the value behind it.
TypeId SemaAnalyzer::builtin_conversion_type(const Value& value, bool reference)
{
	SemaEntity* const owner =
		model_.type_owner(types_.strip_cv(value.type));
	if (owner == nullptr)
	{
		return kNoType;
	}
	// 13.3.1p3: what tells two of these candidates apart is how the operand
	// reaches each conversion function's object parameter, which is the one
	// argument they are ranked over - so `operator T*()` and
	// `operator const T*() const` are two built-in candidates and the object's
	// own cv-qualification says which.
	Value object;
	object.type = object.spelled = types_.pointer_to(value.type);
	object.object_category = value.category;
	object.category = ValueCategory::PRValue;
	std::vector<SemaEntity*> candidates;
	gather_conversions(*owner, candidates);
	TypeId found = kNoType;
	Match chosen;
	bool tied = false;
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		SemaEntity* const at = candidates[index];
		if (at->explicit_function || at->deleted)
		{
			continue;
		}
		const TypeId handed = types_.target(at->type);
		TypeId result = handed;
		if (types_.is_reference(result))
		{
			result = types_.target(result);
		}
		result = types_.strip_cv(result);
		const TypeKind kind = types_.kind(result);
		if (!types_.is_arithmetic(result) && kind != TypeKind::Enum &&
		    kind != TypeKind::Pointer && kind != TypeKind::MemberPointer)
		{
			continue;
		}
		if (reference)
		{
			// 13.6p3 and p5: the operand of `++` and `--` is an lvalue of an
			// arithmetic or pointer type, so a conversion that hands back a
			// value reaches neither, and one that hands back an lvalue of an
			// enumeration or a pointer to member reaches no candidate written
			// over one.
			if (types_.kind(handed) != TypeKind::LValueReference ||
			    (!types_.is_arithmetic(result) && kind != TypeKind::Pointer))
			{
				continue;
			}
			result = handed;
		}
		const Match reached =
			object_match(object, *at, types_.parameters(at->type)[0]);
		if (!reached.viable)
		{
			continue;
		}
		const int order =
			found == kNoType ? 1 : compare_matches(reached, chosen);
		if (order > 0)
		{
			found = result;
			chosen = reached;
			tied = false;
			continue;
		}
		if (order == 0 && found != result)
		{
			tied = true;
		}
	}
	return tied ? kNoType : found;
}

// 13.3.1.2p2 and 13.6: the operands the built-in operator reads, where the
// operand the program wrote is of class type.  What it becomes is what the
// conversion function 13.3 chose hands back, written in the place the operand
// already had - so the reading that follows is the one any built-in operand
// gets and no operator repeats the question.
bool SemaAnalyzer::builtin_operands(unsigned token, const Context& ctx,
                                    std::vector<Value>& operands)
{
	// 13.6 writes each candidate's operands out, and which of them a conversion
	// function can fill is which of them is taken by value: `&E`, `E = F` and
	// `E, F` take the operand as it stands or take a reference 13.6 writes over
	// the operand's own type, so an operand of class type reaches no built-in
	// candidate of theirs at all.  A compound assignment takes its left operand
	// by reference and its right by value, so only the right one is asked.
	// 13.6p3 and p5's `++E` and `--E` are the ones written over a reference an
	// operand *can* reach, because a conversion function may hand back an
	// lvalue of the type they increment.
	std::size_t first = 0;
	bool reference = false;
	switch (token)
	{
	case OP_ASS:
	case OP_COMMA:
	case OP_ARROW:
		return false;
	case OP_INC:
	case OP_DEC:
		reference = true;
		break;
	case OP_AMP:
		if (operands.size() == 1)
		{
			return false;
		}
		break;
	case OP_PLUSASS:
	case OP_MINUSASS:
	case OP_STARASS:
	case OP_DIVASS:
	case OP_MODASS:
	case OP_XORASS:
	case OP_BANDASS:
	case OP_BORASS:
	case OP_LSHIFTASS:
	case OP_RSHIFTASS:
		first = 1;
		break;
	default:
		break;
	}
	if (token == OP_LNOT || token == OP_LAND || token == OP_LOR)
	{
		// 5.3.1p9, 5.14p1 and 5.15p1: these operands are contextually converted
		// to bool, which 12.3.2p2 lets a conversion function declared
		// `explicit` answer - so they are not the ordinary 13.6 question.
		bool converted = false;
		for (std::size_t index = first; index < operands.size(); ++index)
		{
			const TypeId before = operands[index].type;
			contextual_bool(operands[index], ctx);
			converted = converted || operands[index].type != before;
		}
		return converted;
	}
	bool any = false;
	for (std::size_t index = first; index < operands.size(); ++index)
	{
		Value& operand = operands[index];
		if (operand.node == nullptr ||
		    !types_.is_class(types_.strip_cv(operand.type)))
		{
			continue;
		}
		const TypeId wanted = builtin_conversion_type(operand, reference);
		if (wanted == kNoType)
		{
			continue;
		}
		const Match match = conversion_match(operand, wanted, false);
		if (!match.viable)
		{
			continue;
		}
		apply_conversion(operand, wanted, match, ctx, Requested::Written);
		any = true;
	}
	return any;
}

// 13.6 and 13.3.3p1: whether a built-in operator reads these operands better
// than the operator function 13.3 chose among the declarations.
//
// 13.6's candidates are candidates of the same set, so the two are ranked over
// the same argument list: the declaration through its own parameters, and the
// built-in through the operand types 5p9 brings the operands to, which for an
// operand of class type is what its conversion function hands back.  A class
// that reaches no built-in operand type has no such candidate at all, and then
// the declaration stands.
bool SemaAnalyzer::better_builtin(const SemaEntity& chosen, const Value& object,
                                  const std::vector<Value>& operands)
{
	std::vector<TypeId> wanted(operands.size(), kNoType);
	bool any = false;
	for (std::size_t index = 0; index < operands.size(); ++index)
	{
		if (!types_.is_class(types_.strip_cv(operands[index].type)))
		{
			wanted[index] = decayed(operands[index]);
			continue;
		}
		wanted[index] = builtin_conversion_type(operands[index]);
		if (wanted[index] == kNoType)
		{
			return false;
		}
		any = true;
	}
	if (!any)
	{
		return false;
	}
	if (operands.size() == 2 && types_.is_arithmetic(wanted[0]) &&
	    types_.is_arithmetic(wanted[1]) && wanted[0] != wanted[1])
	{
		// 5p9: two arithmetic operands of a built-in operator are brought to
		// one type, which is the type of the candidate that reads them.
		Value left;
		Value right;
		left.type = left.spelled = wanted[0];
		right.type = right.spelled = wanted[1];
		left.category = right.category = ValueCategory::PRValue;
		const TypeId common = binary_operand_type(OP_PLUS, left, right);
		if (common != kNoType)
		{
			wanted[0] = wanted[1] = common;
		}
	}
	const std::vector<TypeId>& parameters = types_.parameters(chosen.type);
	const std::size_t written = chosen.object_member ? 1u : 0u;
	std::vector<Match> declared(operands.size());
	std::vector<Match> builtin(operands.size());
	for (std::size_t index = 0; index < operands.size(); ++index)
	{
		if (index == 0 && chosen.object_member)
		{
			declared[0] = match_argument(object, parameters[0]);
		}
		else if (index + written < parameters.size())
		{
			declared[index] = match_argument(operands[index],
			                                 parameters[index + written]);
		}
		else
		{
			// 13.3.3.1.3: an operand the ellipsis matched converts with the
			// worst rank there is.
			declared[index].viable = true;
			declared[index].rank = kEllipsis;
		}
		builtin[index] = match_argument(operands[index], wanted[index]);
		if (!builtin[index].viable)
		{
			return false;
		}
	}
	return better_candidate(builtin.data(), declared.data(), operands.size(),
	                        false, false);
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
		// how qualified they made it, which is what orders them - and two that
		// bound the same base subobject of it differ in nothing else either,
		// so 4.10p3's conversion does not take the question away.
		match.qualified = referenced;
		return match;
	}
	if (related)
	{
		// 8.5.3p5: the temporary a reference-related initializer would be
		// converted into has to be at least as cv-qualified, so dropping a
		// qualifier binds nothing.
		return match;
	}
	if (types_.is_class(types_.strip_cv(source)))
	{
		// 8.5.3p5: where the initializer is of class type, a conversion
		// function that hands back an lvalue reference-compatible with the
		// referenced type binds that lvalue itself rather than a temporary
		// copied out of it - so the reference, and not the type it refers to,
		// is what the sequence is measured against.  It stands before the
		// refusal below, because that one is about the temporary a conversion
		// *to* the referenced type would make: an lvalue a conversion function
		// handed back is an object of its own, and a non-const lvalue
		// reference binds one.
		const Match reached = conversion_match(argument, parameter, false);
		if (reached.viable)
		{
			return reached;
		}
	}
	if (!rvalue_ref && !const_lvalue_ref)
	{
		// 8.5.3p5: a non-const lvalue reference binds no temporary, and what is
		// left below is the temporary a conversion to the referenced type would
		// be held in.
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

// 13.3.1p4 and p5: how the implied object argument reaches a member candidate's
// implicit object parameter.
//
// 9.3.1p3 holds that parameter as a pointer, and the pointer is still what the
// conversion is measured over: it is what carries 4.10p3's conversion to a base
// subobject and 13.3.3.2p3's ordering of `f()` above `f() const`.  What the
// pointer cannot say is 13.3.1p4's other half - that the parameter is a
// *reference*, and that 8.3.5p1's ref-qualifier says which categories it binds:
// `&` binds an lvalue, and an rvalue only through the non-volatile const every
// lvalue reference binds one through; `&&` binds only an rvalue; and a member
// that wrote neither binds either, which is 13.3.1p5's added rule.  Those two
// facts are written beside the pointer's match, where 13.3.3.2p3 already reads
// them off every other reference binding.
SemaAnalyzer::Match SemaAnalyzer::object_match(const Value& object,
                                               const SemaEntity& candidate,
                                               TypeId parameter)
{
	Match match = match_argument(object, parameter);
	if (!match.viable)
	{
		return match;
	}
	const RefQualifier ref = types_.function_ref_qualifier(candidate.type);
	const bool is_lvalue = object.object_category == ValueCategory::LValue;
	const unsigned cv = types_.object_cv(types_.target(parameter));
	const bool const_lvalue_ref =
		(cv & kCvConst) != 0 && (cv & kCvVolatile) == 0;
	if (ref == RefQualifier::RValue
		    ? is_lvalue
		    : (ref == RefQualifier::LValue && !is_lvalue && !const_lvalue_ref))
	{
		match.viable = false;
		return match;
	}
	match.reference = true;
	match.binds_lvalue = is_lvalue;
	match.binds_rvalue_ref = ref == RefQualifier::RValue;
	return match;
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
	// 8.3.5p1 and 8.3.5p7: both qualifiers written after the parameter-clause
	// are part of the function type a pointer to member points to.
	const TypeId declared = types_.ref_qualified_function(
		types_.qualified_function(
			types_.function_of(types_.target(function.type), written,
			                   types_.variadic(function.type)),
			types_.cv(object)),
		types_.function_ref_qualifier(function.type));
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
				match = object_match(*object, *at, parameters[0]);
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
	// 13.3.3.2p3: two user-defined conversion sequences are indistinguishable
	// unless they hold the same user-defined conversion, and where they do it
	// is their second, standard conversion sequences that order them - which is
	// what tells `pick(int)` from `pick(long)` apart on a class with one
	// `operator int`.
	if (left.rank == kUserConversion || right.rank == kUserConversion)
	{
		const bool same = left.converted == right.converted &&
			left.converting == right.converting;
		if (!same)
		{
			return 0;
		}
		if (left.second_rank != right.second_rank)
		{
			return left.second_rank < right.second_rank ? 1 : -1;
		}
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
const char* SemaAnalyzer::requested_prefix(Requested by, bool reference,
                                           TypeId passed)
{
	switch (by)
	{
	case Requested::Argument:
		// 8.5.3p5 and 5.2.2p4: a reference parameter binds the temporary the
		// argument made, and a parameter of class type is passed the object
		// itself, which 12.8p31 lets the temporary be - as its bytes where the
		// ABI carries a class that way, and as the one object the caller built
		// and the callee was handed the address of where it does not.  Which of
		// the two the storage is is what the name says.
		return reference ? "arg"
		                 : types_.passes_indirectly(passed) ? "arg" : "argobj";
	case Requested::Returned:
		return "retobj";
	case Requested::Written:
		break;
	}
	return "tmpobj";
}

// 12.3.2p1 and 5.2.2p3: the call of a conversion function on the object the
// argument named.
//
// It is written around the operand in the place that operand already had, so
// the node the caller is holding goes on standing where it stood and what is
// added is the call around it - which is what lets an argument, a condition and
// an operand each reach a conversion without any of them moving anything.
SemaAnalyzer::Value SemaAnalyzer::call_conversion(const Value& object,
                                                  SemaEntity& chosen,
                                                  const Context& ctx)
{
	DumpNode& line = model_.wrap_node(*object.node, std::string());
	Value self = object;
	self.node = line.children[0];
	// 7.3.3p1: what a call runs is the declaration the class named, which a
	// using-declaration in a derived class stands for.
	SemaEntity& run = declared_member(chosen);
	SemaEntity* const declared =
		run.region != nullptr ? run.region->owner : nullptr;
	require_access(chosen, ctx.scope,
	               chosen.region != nullptr ? chosen.region : nullptr);
	if (declared != nullptr &&
	    types_.strip_cv(self.type) != types_.strip_cv(declared->type))
	{
		// 10.2p2 and 4.10p3: a conversion function a base class declared is
		// called on the base class subobject of the object the argument named,
		// which the tree names rather than leaving the address to be adjusted.
		self = base_value(self, *declared, !object.through_using);
	}
	address_of_object(self, model_.wrap_node(*self.node, std::string()), false);
	self.through_using = chosen.shadowed != nullptr;
	std::vector<Value> arguments;
	arguments.push_back(self);
	// The callee stands before the argument, as it does in a call the program
	// wrote, and the operand already holds the place after it.
	DumpNode& named = model_.open_node(line, std::string());
	line.children.pop_back();
	line.children.insert(line.children.begin(), &named);
	Value callee;
	callee.node = &named;
	name_function(callee, run, "callee");
	return finish_call(line, run.type, arguments, &run, ctx);
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
	if (match.converted != nullptr && value.node != nullptr)
	{
		// 13.3.1.5p1: the argument reaches the parameter through a conversion
		// function of its own class, and what the parameter is given is what
		// that call handed back - which the second, standard conversion
		// sequence of 13.3.3.2p3 then carries the rest of the way.
		value = call_conversion(value, *match.converted, ctx);
		Match rest = match;
		rest.converted = nullptr;
		rest.rank = match.second_rank;
		apply_conversion(value, target, rest, ctx, by);
		return;
	}
	if (match.to_base != nullptr && value.node != nullptr)
	{
		// 4.10p3 and 8.5.3p4: what the argument became is the base class
		// subobject of the object it named, or a pointer to it, and the tree
		// names that subobject rather than leaving the address to be adjusted
		// by whoever reads the call.  11.2p5 asks about the base-specifier's
		// own access wherever the program wrote the conversion; the object a
		// member a using-declaration brought into a class is called on is one
		// the class named itself, so there nothing was written to ask about.
		value = base_value(value, *match.to_base, !value.through_using);
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
		                        requested_prefix(by, match.reference, wanted), false,
		                        match.reference);
		return;
	}
	if (by != Requested::Written && match.reference && !match.binds_lvalue)
	{
		// 8.5.3p5: the reference binds a temporary rather than an object the
		// argument named, and where that temporary is the prvalue itself the
		// argument is what asked for its storage.  A base subobject of it was
		// already read above, which is what leaves that temporary named after
		// the expression that wrote it rather than after this argument.
		name_argument_temporary(value, requested_prefix(by, true, kNoType), ctx,
		                        true);
	}
	if (by != Requested::Written && !match.reference &&
	    types_.is_class(types_.strip_cv(target)) &&
	    types_.strip_cv(value.type) == types_.strip_cv(target))
	{
		if (value.category != ValueCategory::PRValue && value.node != nullptr &&
		    !types_.is_trivially_copied(types_.strip_cv(target)))
		{
			// 5.2.2p4 and 6.6.3p2: the parameter and the returned object are
			// objects of their own, and the argument names one that goes on
			// existing - so what fills the new one is the copy or move
			// constructor 12.8p15 gives the class, chosen by 13.3 from the
			// value category the argument has.  A class whose copy carries
			// nothing but bytes is left to the copy of the bytes, which is
			// what the storage this asked for already is.
			const TypeId wanted = types_.strip_cv(target);
			Value source = value;
			DumpNode& line = model_.wrap_node(*value.node, std::string());
			source.node = line.children[0];
			line.children.clear();
			value = build_temporary(wanted, line, nullptr, &source, ctx,
			                        requested_prefix(by, false, wanted), false,
		                        false);
			return;
		}
		// 12.8p31: the argument is a prvalue of the parameter's own class, so
		// the temporary it is may be created in the storage the call passes -
		// and then there is one object rather than an object and a copy of it.
		// A returned prvalue is the same rule read at the other end.
		name_argument_temporary(
			value, requested_prefix(by, false, types_.strip_cv(target)), ctx,
			false);
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
		if (owner->aggregate && !clauses.spent() &&
		    !clause_initializes_class(bare, clauses.next(), ctx))
		{
			// 8.5.1p11: the braces around the member's own clauses may be left
			// out, and then the clauses of the enclosing list initialize it.
			// Only a clause that cannot initialize the whole subaggregate is
			// one of them: a value of its own class type initializes it, which
			// 8.5.1p2 copy-initializes it from.
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

SemaAnalyzer::Value SemaAnalyzer::initialize(const AstNode& node, TypeId target,
                                             const Context& ctx,
                                             DumpNode& parent, bool listed,
                                             Requested by, bool direct)
{
	if (node.kind == AstKind::BracedInitList)
	{
		return list_initialize(node, target, ctx, parent);
	}
	Value value = expression(node, ctx, parent);
	if (direct)
	{
		// 8.5p16 and 12.3.2p2: a direct-initialization may choose a conversion
		// function declared `explicit`, which is the one thing that tells it
		// from the copy-initialization every other initializer here is.
		explicit_conversion(value, target, ctx);
	}
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

// 5.1.1p6: a parenthesized expression may be used wherever the expression it
// holds may be, and with the same meaning, so the parentheses around a callee
// change neither what it names nor how it is called.  3.4.2p1 is the one thing
// they do change: the associated namespaces are searched for an unqualified-id,
// and a parenthesized id-expression is not one - which `parenthesized` is what
// says.
namespace
{

const AstNode& written_callee(const AstNode& node, bool& parenthesized)
{
	const AstNode* written = &node;
	while (written->kind == AstKind::ParenthesizedExpression &&
	       !written->children.empty())
	{
		written = written->children[0];
		parenthesized = true;
	}
	return *written;
}

}

SemaAnalyzer::Value SemaAnalyzer::call_expression(const AstNode& node,
                                                  const Context& ctx,
                                                  DumpNode& parent)
{
	bool parenthesized = false;
	const AstNode& callee = written_callee(*node.children[0], parenthesized);
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
		if (child_kind(callee, AstKind::CarriedExpression) != nullptr)
		{
			// 7.1.6.2p1: the callee's nested-name-specifier begins with a
			// decltype-specifier, so the region it is looked up in is the one
			// the type of that expression names.
			named = &require(
				decltype_qualified_name(callee, ctx, LookupKind::Any, found),
				callee.text);
		}
		else
		{
			named = template_specializations(callee.text, ctx, *found);
		}
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
			if (named == nullptr)
			{
				// 1.4p8: the name is one the implementation reserves for a
				// function of its own, so what the program did not declare the
				// implementation declares here, once for the unit.
				named = reserved_function(callee.text, found);
			}
		}
	}

	// 5.2.4: `E1.~T()` for a `T` that is no class names no function at all, so
	// it is settled before the call's own node is opened.
	Value pseudo;
	if (pseudo_destructor_call(node, callee, ctx, parent, pseudo))
	{
		return pseudo;
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
	const bool adl = callee.kind == AstKind::IdExpression && !parenthesized &&
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
		SemaEntity& selected =
			*select_overload(candidates, arguments, called,
			                 object.node != nullptr ? &object : nullptr, false,
			                 singles);
		// 7.3.3p1: a using-declaration made the class declare what its base
		// declared, and what the call runs is the base's function - reached
		// through the base subobject of the object the call names, which
		// 11.2p5 leaves the base-specifier's own access unasked about.
		object.through_using = selected.shadowed != nullptr;
		SemaEntity& chosen = declared_member(selected);
		name_function(target, chosen, "callee");
		function = target.type;
		if (chosen.special == kDestructorFunction)
		{
			// 5.2.4 and the ABI: a destructor a call names is named on an
			// object the program wrote, which is a complete object - so this
			// is the complete-object entry rather than the base-object one a
			// base subobject's own action runs.  The call odr-uses it, so
			// 12.4p6's definition of an implicitly declared one is asked for
			// here as it is wherever else a lifetime ends: the object file
			// otherwise names a symbol no unit defines.
			note_destruction_entry(chosen, false);
		}
		else if (chosen.special != kOrdinaryFunction)
		{
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
	// 5.2.3p3: `T{...}` is written where `T(...)` is, and the one braced list
	// standing where the arguments do is the whole of what says so.  What it
	// makes is list-initialized from that list, which is not the same as being
	// built from its clauses as arguments: 8.5.1 gives them to the members of
	// an aggregate, and 8.5.4p7 refuses one that narrows.
	const AstNode* const braced =
		list != nullptr && list->children.size() == 1 &&
		list->children[0]->kind == AstKind::BracedInitList
			? list->children[0]
			: nullptr;
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
		return materialize_temporary(target, braced != nullptr ? braced : list,
		                             ctx, parent, "tmpobj", count == 0);
	}
	if (braced != nullptr)
	{
		// 5.2.3p3 over any other type: the braces make 8.5.4's
		// list-initialization of a prvalue of the type, which is the same
		// reading of the same list a declaration of an object of it would get.
		return list_initialize(*braced, target, ctx, parent);
	}
	if (count == 0)
	{
		// 5.2.3p2: `T()` is a prvalue of type T that is value-initialized,
		// which for the PA12 subset is the zero of that type - and 2.14.4
		// spells the zero of a floating type as a value of that type, with the
		// suffix that says which of the three widths it is a value of, rather
		// than as the integer the other types share.
		value.constant = true;
		value.what = "literal";
		value.payload = floating_zero(types_.strip_cv(target));
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
	cast_conversion(source, target, ctx);
	value.node = &line;
	respell(value);
	return value;
}
