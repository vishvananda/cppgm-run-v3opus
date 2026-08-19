#include "sema_analyzer.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"
#include "sema_access.h"
#include "sema_argument_lookup.h"
#include "sema_builtin.h"
#include "sema_constexpr.h"
#include "sema_deduce.h"
#include "sema_derivation.h"
#include "sema_operator.h"
#include "sema_pack.h"

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

// 13.3.3.1.2p1 and 13.3.3.1p4: holds "no user-defined conversion may stand
// here" over one measurement or one resolution, and puts back what stood before
// however that reading leaves.
//
// A refusal is one of the probes around this catches and goes on asking
// questions past - 14.6p8's reading of a pattern, a fold asked speculatively,
// 8.5.1p11's probe of a clause - so the flag has to be put back where the
// reading threw as much as where it returned.  Left standing, it says of every
// later conversion in the unit that one user-defined conversion already stands
// in the sequence, which is a wrong answer no diagnostic is written for.
struct StandardOnly
{
	explicit StandardOnly(bool& held)
		: held_(held)
		, outer_(held)
	{
		held_ = true;
	}

	~StandardOnly() { held_ = outer_; }

private:
	bool& held_;
	const bool outer_;
};

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

// 5p9 and 13.6p17: an operand the usual arithmetic conversions bring to the one
// type a built-in arithmetic or bitwise candidate is written over.  4.5p3
// promotes an unscoped enumeration before that question is asked, so it is one
// of them; 7.2p9 leaves a scoped one converted by nothing at all.
bool arithmetic_operand(const TypeTable& types, TypeId type)
{
	return types.is_arithmetic(type) ||
		(types.kind(type) == TypeKind::Enum && !types.is_scoped_enum(type));
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
	// Whether a level below the top has been reached, which is what says the
	// pair standing here has already been compared.  Each descent compares the
	// level it arrives at against an `above_all_const` covering the levels
	// strictly above it - so asking again once the walk stops would ask 4.4p4's
	// second condition of the last level about *itself*, and `volatile int
	// (*)[3]` off an `int (*)[3]` is the conversion that answers no.
	bool descended = false;
	for (;;)
	{
		const bool descend = types_.kind(source) == TypeKind::Pointer &&
			types_.kind(target) == TypeKind::Pointer;
		if (!descend)
		{
			if (types_.object_unqualified(source) !=
			    types_.object_unqualified(target))
			{
				return false;
			}
			return descended ||
				((types_.object_cv(source) & ~types_.object_cv(target)) == 0 &&
				 (types_.object_cv(source) == types_.object_cv(target) ||
				  above_all_const));
		}
		descended = true;
		source = types_.target(source);
		target = types_.target(target);
		if ((types_.object_cv(source) & ~types_.object_cv(target)) != 0)
		{
			return false;
		}
		if (types_.object_cv(source) != types_.object_cv(target) &&
		    !above_all_const)
		{
			return false;
		}
		above_all_const = above_all_const &&
			(types_.object_cv(target) & kCvConst) != 0;
	}
}

// 3.9.3p5 is why every level above reads `object_cv` rather than `cv`: a
// qualifier written on an array is its element's, so `int (*)[3]` reaching
// `const int (*)[3]` is 4.4's conversion and nothing the array node holds says
// so.  The same fact tells the last comparison to be of the types with that
// qualification off, which is what `object_unqualified` gives.

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
	    Derivation(*this).base_in(source, target) != nullptr)
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
	// 3.9.3p5: a cv-qualifier written on an array attaches to its element type,
	// and the array is considered to carry the same qualification - so the
	// qualifiers `const X (*)[]` gives up on the way to `void *` are the
	// element's, which the array node itself does not hold.
	if ((types_.object_cv(source) & ~types_.cv(target)) != 0)
	{
		return false;
	}
	rank = kConversion;
	exact = false;
	return true;
}

SemaAnalyzer::Match SemaAnalyzer::match_by_value(const Value& argument,
                                                 TypeId parameter, bool direct)
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
		return conversion_match(argument, parameter, direct);
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
			                      : Derivation(*this).base_in(types_.target(source),
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
	// 13.3.3p1's last tie-break: whether the candidate now held is one a
	// template was specialized for, which is the only thing left when two
	// candidates convert the argument equally well.
	bool chosen_template = false;
	bool tied = false;
	for (SemaEntity* declared = head; declared != nullptr;
	     declared = declared->next)
	{
		SemaEntity* at = declared;
		const bool from_template = declared->template_parameters != nullptr;
		if (from_template)
		{
			// 13.3.1.4p1 with 14.8.3p1: a constructor *template* is one of the
			// converting constructors this set is chosen from, and it is one
			// through the specialization the argument deduces for it - the
			// template itself declares a parameter of no type an argument can
			// reach.  12.3.1p2's `explicit` is asked of the template, because
			// the specialization inherits it and deducing first would be work
			// spent on a declaration this set does not hold.
			//
			// This is the door `box<int>` reaches `const box<long>&` through:
			// 13.3.3.1.2p1's user-defined conversion is a call of
			// `template<class U> box(const box<U>&)`, and the deduction is one
			// P/A pair over one argument.
			if (declared->explicit_function || declared->deleted)
			{
				continue;
			}
			std::vector<Value> alone(1, argument);
			at = Deduction(*this).from_call(*declared, alone);
			if (at == nullptr)
			{
				continue;
			}
		}
		const std::vector<TypeId>& parameters = types_.parameters(at->type);
		// 9.3.1p3 put the object 12.1 constructs in the type, so a constructor
		// that converts takes exactly one argument beside it.  13.3.2p2 counts
		// a declaration whose parameter-declaration-clause ends in `...` viable
		// for every argument past the ones it wrote, so `A(...)` is a
		// constructor one argument reaches too - and 13.3.3.1.3 gives that
		// argument the ellipsis conversion sequence, which is the worst there
		// is and asks nothing of the argument's own type.
		if (at->explicit_function || at->deleted || !accepts_arity(*at, 2))
		{
			continue;
		}
		Match match;
		if (parameters.size() < 2)
		{
			if (!types_.variadic(at->type) || argument.braced != nullptr)
			{
				continue;
			}
			match.viable = true;
			match.rank = kEllipsis;
		}
		else
		{
			const TypeId wanted = parameters[1];
			const TypeId bare = types_.strip_cv(
				types_.is_reference(wanted) ? types_.target(wanted) : wanted);
			if (types_.is_class(bare) &&
			    types_.strip_cv(argument.type) != bare &&
			    Derivation(*this).base_in(argument.type, bare) == nullptr)
			{
				// 13.3.3.1.2p1: the argument reaches this parameter only
				// through a second user-defined conversion, and one sequence
				// holds one.
				continue;
			}
			// 13.3.3.1.2p1: the sequence measured inside a user-defined
			// conversion holds no user-defined conversion of its own, whichever
			// direction the second one would run - a converting constructor of
			// a further class or a conversion function of the argument's.  One
			// flag says it for both, and it is set here as well as around
			// 13.3.1.5's second sequence because a candidate parameter of
			// built-in type is exactly where a conversion function would
			// otherwise slip a second one in.
			const StandardOnly measured(standard_only_);
			match = match_argument(argument, wanted);
			if (!match.viable || match.converting != nullptr)
			{
				continue;
			}
		}
		int order = best == nullptr ? 1 : compare_matches(match, chosen);
		if (order == 0 && from_template != chosen_template)
		{
			// 13.3.3p1's last tie-break, and the whole reason it is asked here:
			// a class that declares both `box(const box<int> &)` and
			// `template<class U> box(const box<U> &)` offers two candidates
			// whose conversion sequences are identical, and calling that
			// ambiguous leaves the class unreachable from `box<int>`
			// altogether.  14.5.6.2's ordering between two *specializations* is
			// not asked: this set is chosen on one argument, so two of them
			// that tie on it differ only in what their own heads wrote.
			order = from_template ? -1 : 1;
		}
		if (order > 0)
		{
			best = at;
			chosen = match;
			chosen_template = from_template;
			tied = false;
		}
		else if (order == 0)
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
	if (owner == nullptr || owner->conversions_above.empty())
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
	SemaEntity* chosen_from = nullptr;
	Match chosen_object;
	Match chosen_result;
	bool tied = false;
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		SemaEntity* at = candidates[index];
		// 13.3.3p1: the template this candidate was deduced from, and null for
		// one the class declared - which is the last thing that tells two of
		// them apart.
		SemaEntity* const from =
			at->template_parameters != nullptr ? at : nullptr;
		if (from != nullptr)
		{
			// 13.3.1.5p1 and 14.8.2.3: a conversion function *template* of the
			// class is a candidate through the specialization the destination
			// type deduces for it, and through no other - the template itself
			// hands back a place, which reaches nothing.  It is deduced here
			// rather than where the class collected its conversions because the
			// destination is what says which specialization there is.
			at = Deduction(*this).from_conversion(*at, parameter);
			if (at == nullptr)
			{
				continue;
			}
		}
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
		Match result;
		{
			const StandardOnly measured(standard_only_);
			result = match_argument(produced, parameter);
		}
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
		int order = best == nullptr ? 1 : compare_matches(result, chosen_result);
		if (best != nullptr && order == 0)
		{
			order = compare_matches(reached, chosen_object);
		}
		if (best != nullptr && order == 0 &&
		    (from == nullptr) != (chosen_from == nullptr))
		{
			// 13.3.3p1: a conversion function the class declared beats a
			// specialization of one of its templates that gets exactly as far,
			// which is what leaves `explicit operator bool` chosen for a
			// contextual conversion beside an `operator T` that deduces `bool`.
			order = from == nullptr ? 1 : -1;
		}
		if (best != nullptr && order == 0 && from != nullptr &&
		    chosen_from != nullptr && from != chosen_from)
		{
			// 13.3.3p1: two specializations whose conversions are
			// indistinguishable are told apart by 14.5.6.2's ordering of the
			// templates they were made from, exactly as a call tells any two
			// deduced candidates apart - over the types 14.8.2.4p3's second
			// bullet names, which for a conversion function is what it hands
			// back and not the object it is the only parameter of.
			order = more_specialized(*from, *chosen_from, kResultPlace)
				? 1
				: (more_specialized(*chosen_from, *from, kResultPlace) ? -1 : 0);
		}
		if (order > 0)
		{
			best = at;
			chosen_from = from;
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
	if (types_.is_void(types_.strip_cv(source.type)) && !types_.is_void(wanted))
	{
		// 5.2.9p4 with 3.9.1p9: a cast to anything but cv `void` is the
		// direct-initialization of a temporary of the target's type from the
		// operand, and an expression of type `void` has no value to initialize
		// one with - so `(int)((void)0)` and 5.2.3p1's `int((void)0)` reach
		// nothing, where `(void)((void)0)` is the discarded-value expression it
		// already was.  Every cast to a type that is not a reference comes
		// through here, however it was written.
		throw std::runtime_error("an operand of type void is cast to " +
		                         types_.description(target));
	}
	// 12.8p31: a prvalue of the target's own class already *is* the object the
	// cast is worth, so there is nothing between the two and no second one to
	// build.  Every other operand names an object that goes on existing, which
	// is what leaves 5.2.9p4 an initialization to run - including one of the
	// target's own class, whose initialization is the copy or move constructor
	// 13.3 chooses for it.
	const bool same_class = types_.strip_cv(source.type) == wanted;
	if (types_.is_class(wanted) && !types_.is_reference(target) &&
	    source.node != nullptr &&
	    (!same_class || source.category != ValueCategory::PRValue))
	{
		// 5.2.9p4: the cast is well formed exactly where `T t(e);` is for an
		// invented temporary, so a cast to a class type *is* that
		// direct-initialization - a call of the constructor 13.3.1.3 chooses,
		// with 13.3.1.4 leaving the class's `explicit` constructors in, and
		// never a reading of whatever bytes the operand happens to hold.
		//
		// 12.3.2p2: where the operand's own class reaches the target through a
		// conversion function of its own, a cast may name that function however
		// the class declared it - which converts the operand rather than
		// constructing an object of the target's class from it.  A cast to the
		// operand's own class reaches no such function: 12.3.2p1 leaves a class
		// no conversion to itself, so what it names is a constructor.
		if (!same_class && types_.is_class(types_.strip_cv(source.type)) &&
		    explicit_conversion(source, target, ctx))
		{
			return;
		}
		// The temporary is written around the operand in the place the operand
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
//
// 13.3.1.2p1's other operand is the enumeration, and it reaches a built-in
// candidate without asking anything of a conversion function: 13.6 writes the
// integral operators over 4.5p3's *promoted* operands, so `E | F` is the
// built-in `operator|(int, int)` and an `operator|` declared over a class an
// enumerator can be converted to is a candidate that loses to it.  An operand
// that is neither is one 13.3.1.2p2 would have left the built-in operator
// outright, so it says nothing about whether there is a candidate here.
bool SemaAnalyzer::better_builtin(const SemaEntity& chosen, const Value& object,
                                  const std::vector<Value>& operands)
{
	std::vector<TypeId> wanted(operands.size(), kNoType);
	bool any = false;
	for (std::size_t index = 0; index < operands.size(); ++index)
	{
		const TypeId bare = types_.strip_cv(operands[index].type);
		if (!types_.is_class(bare))
		{
			wanted[index] = decayed(operands[index]);
			any = any || (types_.kind(bare) == TypeKind::Enum &&
			              !types_.is_scoped_enum(bare));
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
	if (operands.size() == 2 && arithmetic_operand(types_, wanted[0]) &&
	    arithmetic_operand(types_, wanted[1]) && wanted[0] != wanted[1])
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
	// 13.3.1.2p4: every operand here is a place both lists wrote, and no pair of
	// templates is ordered on this path at all.
	return better_candidate(builtin.data(), declared.data(), operands.size(), 0,
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
	return types_.rebuilt_array(type, bare_type(types_.target(type)));
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
	SemaEntity* const to_base = Derivation(*this).base_in(source, referenced);
	const bool related =
		bare_type(source) == bare_type(referenced) || to_base != nullptr;
	const bool const_lvalue_ref = (types_.cv(referenced) & kCvConst) != 0 &&
		(types_.cv(referenced) & kCvVolatile) == 0;
	// 13.3.1.4p1: this reference is the first parameter of a constructor of the
	// class an object is being direct-initialized of, and the temporary bound to
	// it is initialized in the context of that direct-initialization - so the
	// `explicit` conversion functions of the argument's class are candidates for
	// it, which is the same set 5.2.9p4's cast to that class reaches.  Every
	// other reference measures a copy-initialization and reaches none of them.
	const bool direct = direct_initialized_ != kNoType &&
		types_.strip_cv(referenced) == direct_initialized_;
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
		const Match reached = conversion_match(argument, parameter, direct);
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
	const Match converted = match_by_value(argument, referenced, direct);
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
	if (argument.braced != nullptr)
	{
		// 13.3.3.1.5p1: the argument is a braced-init-list, whose conversion
		// sequence is a fact of the parameter and of nothing else the argument
		// carries.
		return match_list(argument.clauses, parameter, argument.listed_class);
	}
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

TypeId member_pointer_of(TypeTable& types, const SemaEntity& function)
{
	if (!function.object_member || function.region == nullptr ||
	    function.region->owner == nullptr)
	{
		return kNoType;
	}
	const std::vector<TypeId>& parameters = types.parameters(function.type);
	const TypeId object = types.target(parameters[0]);
	const std::vector<TypeId> written(parameters.begin() + 1, parameters.end());
	// 8.3.5p1 and 8.3.5p7: both qualifiers written after the parameter-clause
	// are part of the function type a pointer to member points to.
	const TypeId declared = types.ref_qualified_function(
		types.qualified_function(
			types.function_of(types.target(function.type), written,
			                  types.variadic(function.type)),
			types.cv(object)),
		types.function_ref_qualifier(function.type));
	return types.member_pointer_to(types.strip_cv(object), declared);
}

// 14.8.2.2p1 where the target is a pointer to member: the one A the deduction
// is over is the function type the pointer points to, and what the template
// declares is a member function - whose type 9.3.1p3 begins with the object
// parameter the pointer type says nothing about.  So the A is that pointee with
// this template's own object parameter put back in front of it, which leaves
// 8.3.5p7's cv-qualifier-seq where `member_pointer_of` reads it from and lets
// that same reading check the specialization the deduction made.
TypeId member_target(TypeTable& types, const SemaEntity& candidate,
                     TypeId wanted)
{
	const TypeId pointee = types.target(wanted);
	const std::vector<TypeId>& own = types.parameters(candidate.type);
	if (!candidate.object_member || own.empty() ||
	    types.kind(pointee) != TypeKind::Function)
	{
		return kNoType;
	}
	const std::vector<TypeId>& written = types.parameters(pointee);
	std::vector<TypeId> shaped;
	shaped.reserve(written.size() + 1);
	shaped.push_back(own[0]);
	shaped.insert(shaped.end(), written.begin(), written.end());
	return types.ref_qualified_function(
		types.function_of(types.target(pointee), shaped,
		                  types.variadic(pointee)),
		types.function_ref_qualifier(pointee));
}

SemaEntity* SemaAnalyzer::resolve_target(const Value& value, TypeId target)
{
	TypeId wanted = target;
	if (types_.is_reference(wanted))
	{
		wanted = types_.target(wanted);
	}
	// 13.4p1 lists a pointer to member among the targets that choose one
	// declaration, and the declaration it chooses is the member whose pointer
	// type is the one asked for.  8.3.3p1 leaves that type saying which member
	// of a class it names rather than being a function type, so the walk below
	// asks `member_pointer_of` of each declaration instead of comparing its
	// type - and 14.8.2.2p1's deduction is over the function type the pointer
	// points to, with 9.3.1p3's object parameter put back in front of it.
	const bool of_member =
		types_.kind(types_.strip_cv(wanted)) == TypeKind::MemberPointer;
	if (of_member)
	{
		wanted = types_.strip_cv(wanted);
	}
	else
	{
		if (types_.kind(wanted) == TypeKind::Pointer)
		{
			wanted = types_.target(wanted);
		}
		wanted = types_.strip_cv(wanted);
		if (types_.kind(wanted) != TypeKind::Function)
		{
			return nullptr;
		}
	}
	// 13.4p1 and 14.8.2.2p1: a function template in the set is one of the
	// declarations the target chooses between through the specialization the
	// target type deduces.  13.4p1 leaves a declaration the program wrote ahead
	// of every such specialization, so a deduced one is only kept and the walk
	// goes on; several that deduce are told apart the way 13.3.3p1 tells two
	// specializations apart, by 14.5.6.2's ordering of the templates.
	std::vector<SemaEntity*> deduced;
	std::vector<SemaEntity*> patterns;
	for (std::size_t index = 0; index < value.functions->size(); ++index)
	{
		for (SemaEntity* at = (*value.functions)[index]; at != nullptr;
		     at = at->next)
		{
			if (!model_.reachable(*at))
			{
				// 14.6.4.2p1 again: 13.4p1's set is the one the name found,
				// which under a second reading's bound is what stood then.
				continue;
			}
			const bool names_it = of_member
				? member_pointer_of(types_, *at) == wanted
				: at->type == wanted;
			if (names_it)
			{
				return at;
			}
			if (at->template_parameters == nullptr)
			{
				continue;
			}
			const TypeId asked =
				of_member ? member_target(types_, *at, wanted) : wanted;
			SemaEntity* const made = asked == kNoType
				? nullptr
				: Deduction(*this).from_target(*at, asked);
			if (made != nullptr &&
			    (!of_member || member_pointer_of(types_, *made) == wanted))
			{
				deduced.push_back(made);
				// 14.5.6.2 orders the templates and not the names a use gave
				// them, so 14.8.1p2's partly written list is ordered as the
				// template it wrote a part of.
				patterns.push_back(at->partial_of != nullptr ? at->partial_of
				                                             : at);
			}
		}
	}
	if (deduced.empty())
	{
		return nullptr;
	}
	std::size_t best = 0;
	for (std::size_t index = 1; index < deduced.size(); ++index)
	{
		if (more_specialized(*patterns[index], *patterns[best]))
		{
			best = index;
		}
	}
	for (std::size_t index = 0; index < deduced.size(); ++index)
	{
		// 13.4p1: the target names one declaration, so a set that leaves two
		// specializations neither of which is more specialized names none.
		if (index != best && !more_specialized(*patterns[best], *patterns[index]))
		{
			return nullptr;
		}
	}
	return deduced[best];
}

// 14.8.1p2 read at a naming no deduction follows: whether the written list left
// this entry of `explicit_specializations`' set a place still to be filled.
//
// `explicit_specializations` answers for a *call*, where every place the list
// left over is one 14.8.2 deduces from the arguments - so an entry it made a
// candidate of is a declaration and not yet a specialization.  A naming with no
// call and no target type has nothing to deduce from, and the one place that is
// still a specialization's there is 14.8.2p? trailing parameter pack the list
// reached and gave nothing: it stands for a run of none and nothing has to fill
// it.  So `f<int>` names `template<class T, class... U> f()` and does not name
// `template<class T, class U> f()`.
namespace
{

bool names_specialization(const TypeTable& types, const SemaEntity& entry)
{
	if (entry.partial_of == nullptr)
	{
		return true;
	}
	if (entry.template_parameters == nullptr)
	{
		return false;
	}
	const std::vector<SemaEntity*>& places =
		entry.template_parameters->declarations;
	const std::size_t fixed = function_pack_place(types, places);
	return fixed + 1 >= places.size() &&
		types.type_list_at(entry.template_arguments).size() >= fixed;
}

}  // namespace

// 14.2 asked of one spelling by a reading that has no overload set to hand on.
//
// `id_expression` asks these same two doors in this same order and hands the
// set on: a target type or a call's arguments choose from it later, and the
// line that names one declaration is written then.  5.19's three readings have
// no later - `&f<int>` comes to an address where it stands - and neither has
// 7.1.6.2p4, which asks what one id-expression *names*.  So the two differences
// are here.  A list that fits several declarations of the name is 13.4p1's set
// with nothing to choose from it and names none; and the one it does fit is
// chosen, which is what 14.7.1p1 asks the template for the body with.
//
// 3.2p2 is what says whether that ask is made at all: a naming written in a
// potentially-evaluated expression is a use of the specialization, and 5p8's
// unevaluated operand is no use of anything.
SemaEntity* SemaAnalyzer::folded_name(const std::string& spelling,
                                      const Context& ctx, bool used)
{
	std::vector<SemaEntity*> found;
	if (template_specializations(spelling, ctx, found) == nullptr)
	{
		return resolve(spelling, ctx, LookupKind::Any);
	}
	// 14.8.1p2: a declaration the written list did not fill is one a deduction
	// still has to finish, and a naming with neither a call nor a target type
	// makes none - so it is no member of 13.4p1's set here however it ranks in
	// one a call builds.  `&f<int>` beside `template<class T, class U> int f()`
	// names the one declaration the list completed.
	SemaEntity* specialized = nullptr;
	std::size_t names = 0;
	for (std::size_t index = 0; index < found.size(); ++index)
	{
		if (!names_specialization(types_, *found[index]))
		{
			continue;
		}
		specialized = names == 0 ? found[index] : specialized;
		++names;
	}
	if (names == 0)
	{
		throw NotConstant(spelling + " writes a template-argument-list that "
		                  "completes no declaration of the name, and this "
		                  "naming deduces nothing to finish one", false);
	}
	if (names > 1)
	{
		throw NotConstant(spelling + " names more than one function template "
		                  "specialization and this naming writes no target "
		                  "type for 13.4 to choose between them", false);
	}
	if (!used || checking_ != 0)
	{
		// 3.2p2 at an unevaluated operand, and 14.6p8 at a reading of a
		// pattern: the first names the entity and uses nothing, and the second
		// declares nothing into the output and names nothing the unit owes a
		// definition of - the member template of a class template's own body
		// has no pattern recorded there at all.  The instantiation reads that
		// same spelling again with the arguments bound, and that naming is
		// where 14.7.1p1's demand is made.
		return specialized;
	}
	return &named_function(*specialized);
}

// 3.2p2: naming a function is a use of it, and what a use asks for is the same
// wherever the name was written - which is what puts this beside the line that
// spells one rather than inside it.  A fold of a constant expression names a
// declaration too: 13.3 chooses it there over the constants the call holds, and
// the body the fold then reads is a body only this asks the template for.
SemaEntity& SemaAnalyzer::named_function(SemaEntity& selected)
{
	// 7.3.3p1: 13.3 chose among the declarations the class made, and one a
	// using-declaration made names the base's.  Naming a function is a use of
	// it - the body a call runs, the address `&` takes, the symbol the object
	// file holds - and every use reaches the declaration the base wrote, so the
	// two part company here and nowhere below.
	SemaEntity& function = declared_member(selected);
	// 14.7.1p1 with 3.2p2: a specialization is instantiated where it is named in
	// a context that *requires* the definition to exist, and 5p8's unevaluated
	// operand is not one - `decltype(declval<T>())` says what the call would be
	// worth and runs nothing, so the body is no part of what the operand asks
	// for.  The declaration the deduction made is the whole answer there, and it
	// already stands; a later naming in a potentially-evaluated expression is
	// what asks for the definition, and finds this one not yet instantiated.
	const bool used = unevaluated_ == 0;
	if (function.primary != nullptr && used)
	{
		// 14.7.1p1: choosing a specialization is what asks for it, and the
		// declaration it stands for is written once however often it is named.
		instantiate(function);
	}
	// 8.4.3p2: a program that names a deleted function is ill formed, and a
	// name is what every use of one but 12.1's construction of an object goes
	// through - a call, an address, an operator expression the class of an
	// operand answered.  12.8p11 and p23 are what delete most of them, and
	// nothing below this point could tell that from a declaration the program
	// never wrote.
	if (function.deleted)
	{
		throw std::runtime_error(function.name +
		                         " is named and is a deleted function");
	}
	if (!used)
	{
		return function;
	}
	// 12.8p28 and 3.2p3: naming an assignment operator the standard gave a
	// class is what asks this unit for the definition of it.
	demand_transfer_definition(function);
	// 14.7.1p1: naming a member of a class template specialization - as a
	// callee, as the operand of an `&`, as the declaration a target type chose
	// - is what asks the instantiation for the body it put aside.
	require_definition(function);
	return function;
}

void SemaAnalyzer::name_function(Value& value, SemaEntity& selected,
                                 const char* what)
{
	SemaEntity& function = named_function(selected);
	// An id-expression writes the name as the program spelled it; a callee
	// writes the one its declaration has.  The two part company wherever a
	// lookup crossed a region - a using-directive, a template-id - and a
	// declaration reached under one name is written under another.
	const std::string named =
		value.name != nullptr ? payload_of(*value.name) : function.dump_name;
	// 8.3.5p1 and 9.3.1p3: the name of a member function stands for a type whose
	// first parameter is the object, and what the dump spells of it is that
	// parameter rather than the qualifiers the declarator wrote after its
	// parameter-clause - which the type goes on carrying for 13.1 to read.
	const TypeId shown =
		function_description_type(function.type, function.object_member);
	if (value.addressed != nullptr)
	{
		// 5.3.1p3 and 13.4: `&f` is a pointer to the declaration the target
		// chose, and the name under it is that declaration.
		value.addressed->text = spell("id-expression", ValueCategory::LValue,
		                              shown, named);
		value.addressed->fact.kind = FactKind::Id;
		value.addressed->fact.type = function.type;
		value.addressed->fact.spelled = shown;
		value.addressed->fact.category = ValueCategory::LValue;
		value.addressed->fact.entity = &function;
		value.type = member_pointer_of(types_, function);
		if (value.type == kNoType)
		{
			value.type = types_.pointer_to(function.type);
		}
		value.spelled = value.type;
		value.category = ValueCategory::PRValue;
		value.what = "unary-expression";
		if (value.node != nullptr)
		{
			respell(value);
		}
		return;
	}
	value.type = function.type;
	value.spelled = shown;
	value.category = ValueCategory::LValue;
	value.entity = &function;
	if (value.node == nullptr)
	{
		return;
	}
	if (std::strcmp(what, "callee") == 0)
	{
		// The callee of a call is the one line that names a declaration before
		// its type rather than after it, so it is the one line `spell` does not
		// write and the one a conversion is never written around.
		value.node->text =
			"callee " + function.dump_name + " " +
			function_description(function.type, function.object_member);
		value.node->fact.kind = FactKind::Callee;
		value.node->fact.type = function.type;
		value.node->fact.spelled = function.type;
		value.node->fact.category = ValueCategory::LValue;
		value.node->fact.entity = &function;
		return;
	}
	value.what = what;
	value.payload = named;
	respell(value);
}

// 13.3.3p1: one candidate is better than another when its conversion for every
// argument is at least as good and for one is better.
SemaEntity* SemaAnalyzer::select_overload(
	const std::vector<SemaEntity*>& candidates,
	const std::vector<Value>& arguments, const std::string& name,
	const Value* object, bool converting, std::size_t singles,
	const Value* operand, bool* unviable, std::size_t associated)
{
	std::vector<SemaEntity*> viable;
	// 3.4.2p2 lets one declaration be reached by more than one of the searches
	// that gathered these candidates, and 13.3p1 puts each declaration in the
	// set once.  A single chain cannot repeat itself, so the question is asked
	// only where several were gathered.
	std::unordered_set<const SemaEntity*> gathered;
	// 13.3.3p1: which of the viable candidates is a specialization of a
	// template, which is what tells two apart whose conversions tie - and, for
	// two that are, the template each was made of, which 14.5.6.2 orders.
	std::vector<char> templated;
	std::vector<SemaEntity*> patterns;
	std::vector<Match> matches;
	// 13.3.1p3: every candidate is compared over the same argument list, so the
	// implicit object argument holds a place in it whether or not a candidate is
	// a non-static member.  13.3.1p4 makes that place an exact match for a
	// candidate with no implicit object parameter, so it never decides between
	// two candidates on its own.
	const std::size_t implicit = object != nullptr ? 1u : 0u;
	// 14.8.2.4p3: how many of the ranked places are 13.3.1p3's implicit object
	// argument, which is the one a candidate need have written no parameter for.
	// 13.3.1.2p4's first operand is not one: a non-member operator candidate
	// wrote it as its own first parameter, so the ordering asks both lists over
	// it exactly as it asks over the rest.
	const std::size_t objects = operand != nullptr ? 0u : implicit;
	// 13.3.1.2p4 and 14.8.2.1p1: a non-member operator candidate takes the
	// first operand as its own first argument, so the list a deduction of one
	// reads from is that operand and then the rest.  It is built once, because
	// every template candidate of one operator deduces from the same list.
	std::vector<Value> operands;
	if (operand != nullptr)
	{
		operands.reserve(arguments.size() + 1);
		operands.push_back(*operand);
		operands.insert(operands.end(), arguments.begin(), arguments.end());
	}
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
		if (associated != kAssociatedUnknown &&
		    chain + associated < candidates.size() &&
		    !model_.reachable(*candidate))
		{
			// 14.6.4.2p1: 3.4.1's half of the set is found from the template
			// definition context, so a declaration made after the pattern this
			// reading belongs to was written is no candidate of it - and an
			// overload added later is exactly that, standing on the chain a
			// binding that did stand heads.  3.4.2's half is the other arm of
			// the same clause and is answered at the instantiation context as
			// well, so the entries the associated regions contributed are
			// asked no such question: a hidden friend of a class defined after
			// the pattern, and a function declared into an associated
			// namespace after it, are both ones the call reaches.
			continue;
		}
		SemaEntity* at = candidate;
		if (candidate->template_parameters != nullptr)
		{
			// 14.8.3p1: a template is a candidate through the specialization
			// the arguments deduce, and no candidate at all when they deduce
			// none.
			at = Deduction(*this).from_call(*candidate,
			                                operand != nullptr &&
			                                !candidate->object_member
				? operands
				: arguments);
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
				// the worst rank there is - and 13.3.3.1.5p1's braced-init-list
				// is not an argument the ellipsis reaches at all, because every
				// sequence 13.3.3.1.5 gives one is keyed on a parameter.
				match.viable = arguments[index].braced == nullptr;
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
		// 14.5.6.2p2 orders the templates a call made specializations of, and
		// an ordinary declaration is ordered against nothing.
		patterns.push_back(at->primary != nullptr &&
		                   at->primary->template_parameters != nullptr
			? at->primary
			: nullptr);
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
		                     objects, templated[index] == 0,
		                     templated[best] != 0,
		                     patterns[index], patterns[best]))
		{
			best = index;
		}
	}
	for (std::size_t index = 0; index < viable.size(); ++index)
	{
		if (index != best &&
		    !better_candidate(rows + best * count, rows + index * count, count,
		                      objects, templated[best] == 0,
		                      templated[index] != 0,
		                      patterns[best], patterns[index]))
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
		// 13.3.3.2p3: two list-initialization sequences hold the same
		// user-defined conversion where they "initialize the same class", which
		// is the one thing a sequence 13.3.3.1.5 gave has instead of a
		// constructor or a conversion function.
		const bool same = left.converted == right.converted &&
			left.converting == right.converting &&
			left.list_class == right.list_class;
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
		if (Derivation(*this).base_in(left.to_base->type, right.to_base->type) != nullptr)
		{
			return 1;
		}
		if (Derivation(*this).base_in(right.to_base->type, left.to_base->type) != nullptr)
		{
			return -1;
		}
	}
	// 13.3.3.2p3: where two sequences differ only in the qualifiers they gave
	// the argument, the one whose qualifiers are a proper subset of the other's
	// is better - which is what 13.3.1.1.1 orders `f()` above `f() const` by on
	// an object that is not const, and what orders `f(T&)` above `f(const T&)`.
	// The two clauses the field carries are different ones: for a reference
	// binding it is how qualified the reference made the object, and for every
	// other sequence it is what a qualification conversion made of a pointer.
	// A sequence of one kind is ordered against a sequence of the other by
	// neither, which is what leaves `f(T)` and `f(const T &)` to 14.5.6.2.
	if (left.reference == right.reference && left.qualified != kNoType &&
	    right.qualified != kNoType && left.qualified != right.qualified)
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
                                    std::size_t count, std::size_t objects,
                                    bool left_written, bool right_deduced,
                                    SemaEntity* left_template,
                                    SemaEntity* right_template)
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
	if (strictly_better || (left_written && right_deduced))
	{
		return true;
	}
	// 13.3.3p1: two specializations whose conversions are indistinguishable are
	// told apart by 14.5.6.2's ordering of the templates they were made from -
	// a question about the two patterns, except for 14.8.2.4p3's one fact of
	// this call: how many places it wrote arguments for, which is the count the
	// conversions above were already ranked over less the object argument only
	// a non-static member wrote a parameter for.  A parameter no argument
	// reached orders nothing, so `f(T &, U &, bool = true, bool = false)` and
	// `f(T &, U &, V, W, bool = false)` are ordered over four places and not
	// left unordered by the two lists being of different lengths.
	return left_template != nullptr && right_template != nullptr &&
		more_specialized(*left_template, *right_template, count - objects,
		                 objects != 0);
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
	Access(*this).require_access(chosen, ctx.scope,
	               chosen.region != nullptr ? chosen.region : nullptr);
	// 7.3.3p1 with 13.3.1p4's last sentence, asked before the step it is about:
	// the base subobject below is the conversion 11.2p5 leaves the
	// base-specifier's own access unasked about, so what says whether to ask is
	// this conversion function's own declaration and not what the operand was
	// reached through.  A call written on an object asks the same question of
	// the declaration 13.3 chose, one step earlier than its own base step.
	const bool published = named_by_using(chosen);
	if (declared != nullptr &&
	    types_.strip_cv(self.type) != types_.strip_cv(declared->type))
	{
		// 10.2p2 and 4.10p3: a conversion function a base class declared is
		// called on the base class subobject of the object the argument named,
		// which the tree names rather than leaving the address to be adjusted.
		self = Derivation(*this).base_value(self, *declared, !published);
	}
	address_of_object(self, model_.wrap_node(*self.node, std::string()), false);
	self.through_using = published;
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
	// 11.2p4: the conversion is written where the initialization is - a
	// declaration's initializer, a return, an argument - and the reading of the
	// operand it converts has already given its own region back, so the region
	// the access is asked in is this one and not what that reading left behind.
	const Written where(*this, ctx.scope);
	if (value.braced != nullptr)
	{
		// 8.5.4: the type the list initializes is settled, so this is where its
		// clauses are read - once, into the line the argument already held.
		initialize_from_list(value, target, match, ctx, by);
		return;
	}
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
		value = Derivation(*this).base_value(value, *match.to_base, !value.through_using);
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
		// 13.3.3.1p4: this resolution is 13.3.1.4's, and there the first
		// parameter of a constructor candidate considers no user-defined
		// conversion sequence of its own - so the class's copy constructor is
		// not a way to reach the class from an argument of another type.  The
		// flag has to be held over the resolution and not only over the
		// measurement `converting_constructor` made, because otherwise the two
		// disagree about which constructor 13.3 chose: a class whose only
		// converting constructor takes the ellipsis would be reached here by a
		// copy constructor whose own argument came back through this same
		// door, which is a regress with no bottom.  It is held by `StandardOnly`
		// because `build_temporary` refuses by throwing, and a refusal one of
		// the probes above catches leaves the flag standing over everything the
		// unit reads after it.
		const StandardOnly resolved(standard_only_);
		value = build_temporary(wanted, line, nullptr, &source, ctx,
		                        requested_prefix(by, match.reference, wanted), false,
		                        match.reference, false, false,
		                        by != Requested::Written);
		return;
	}
	if (by != Requested::Written && match.reference && !match.binds_lvalue &&
	    value.category == ValueCategory::PRValue)
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
		    !types_.bytes_stand_for_object(types_.strip_cv(target)))
		{
			// 5.2.2p4 and 6.6.3p2: the parameter and the returned object are
			// objects of their own, and the argument names one that goes on
			// existing - so what fills the new one is the copy or move
			// constructor 12.8p15 gives the class, chosen by 13.3 from the
			// value category the argument has.  A class whose bytes stand for
			// the object is left to the copy of the bytes, which is what the
			// storage this asked for already is.
			const TypeId wanted = types_.strip_cv(target);
			Value source = value;
			DumpNode& line = model_.wrap_node(*value.node, std::string());
			source.node = line.children[0];
			line.children.clear();
			value = build_temporary(wanted, line, nullptr, &source, ctx,
			                        requested_prefix(by, false, wanted), false,
			                        false, false, false, true);
			return;
		}
		// 12.8p31: the argument is a prvalue of the parameter's own class, so
		// the temporary it is may be created in the storage the call passes -
		// and then there is one object rather than an object and a copy of it.
		// A returned prvalue is the same rule read at the other end.
		name_argument_temporary(
			value, requested_prefix(by, false, types_.strip_cv(target)), ctx,
			false);
		if (by == Requested::Argument && value.node != nullptr &&
		    types_.passes_indirectly(types_.strip_cv(target)))
		{
			// 5.2.2p4 and 12.4p5: the object standing here *is* the parameter,
			// and the function called is what ends it - at every return and
			// where its body falls off the end.  So this side owes nothing for
			// it on any path: neither 12.2p3's end of the full-expression, which
			// the release above took away, nor 15.2p2's handler, which reads the
			// end written on the node that began the lifetime.
			value.node->fact.destruction = nullptr;
		}
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


SemaAnalyzer::Value SemaAnalyzer::argument_expression(const AstNode& node,
                                                      const Context& ctx,
                                                      DumpNode& parent)
{
	if (node.kind != AstKind::BracedInitList)
	{
		return expression(node, ctx, parent);
	}
	// 13.3.3.1.5p1: the list is not an expression, so reading it now would be
	// reading it for a type 13.3 has not chosen yet.  What travels is the list;
	// what is written now is the place among the arguments it will take, so an
	// argument beside it that does convert is still written where the call
	// passes it.
	Value value;
	value.braced = &node;
	// 14.5.3p4: how long the list is is settled here, where the region binding
	// its packs still stands - 13.3 ranks it by that length and is asked long
	// after the argument was written.
	value.clauses = WrittenList(&node, *this, ctx).size();
	value.category = ValueCategory::PRValue;
	value.node = &model_.open_node(parent, std::string());
	return value;
}


// 12.8p32: where the criteria 12.8p31 gives for eliding the copy into the
// returned object are met - or would be met but for the operand naming a
// parameter - the resolution that chooses the constructor runs first as if the
// operand were an rvalue, so an object of the function's own is *moved* out of
// the function and not copied.  The second resolution p32 then describes is the
// ordinary lvalue one, run where the first chose no constructor taking an
// rvalue reference to the class: `selected_transfer` is that first resolution,
// because 12.8p15 already settled which of the two members a class hands an
// rvalue of itself to.
//
// The operand has to be the name of a non-volatile automatic object of the
// function's own returned type.  A block-scope object of static storage
// duration is one the declaration itself refuses in this milestone, so a name
// a block declared is automatic.  **An lvalue reference names no such object**:
// 3.9p9 leaves a reference no object at all, and the one its name reaches was
// declared somewhere this function cannot see the end of - so `V& r = v;
// return r;` and a `V&` parameter alike are the copy 12.8p15 gives an lvalue,
// which is what the reference binary and g++ both write.  An rvalue reference
// is the one kind of reference this reads through, because the object it binds
// is one nothing after the return can name.
void SemaAnalyzer::return_as_rvalue(Value& value, TypeId target)
{
	if (value.category != ValueCategory::LValue || value.entity == nullptr ||
	    value.node == nullptr || value.node->fact.kind != FactKind::Id)
	{
		return;
	}
	const SemaEntity& named = *value.entity;
	if ((named.kind != SemaKind::Variable && named.kind != SemaKind::Parameter) ||
	    named.region == nullptr ||
	    (named.region->kind != ScopeKind::Block &&
	     named.region->kind != ScopeKind::Function) ||
	    types_.kind(named.type) == TypeKind::LValueReference ||
	    (types_.cv(value.type) & kCvVolatile) != 0 ||
	    !types_.is_class(types_.strip_cv(target)) ||
	    types_.strip_cv(value.type) != types_.strip_cv(target))
	{
		return;
	}
	const SemaEntity* const chosen =
		selected_transfer(target, kMoveConstructorTransfer);
	if (chosen == nullptr || chosen->transfer != kMoveConstructorTransfer)
	{
		return;
	}
	value.category = ValueCategory::XValue;
	respell(value);
}

SemaAnalyzer::Value SemaAnalyzer::initialize(const AstNode& node, TypeId target,
                                             const Context& ctx,
                                             DumpNode& parent, bool listed,
                                             Requested by, bool direct)
{
	if (node.kind == AstKind::BracedInitList)
	{
		// 8.5.4 and 13.3.3.1.5: the list is read for the type it initializes,
		// which every other initialization reaches through the same two steps -
		// what the conversion is, and what writing it comes to.
		Value value = argument_expression(node, ctx, parent);
		const Match match = match_argument(value, target);
		if (!match.viable)
		{
			throw std::runtime_error("a braced-init-list initializes no object "
			                         "of " + types_.description(target));
		}
		apply_conversion(value, target, match, ctx, by);
		return value;
	}
	Value value = expression(node, ctx, parent);
	if (by == Requested::Returned)
	{
		// 12.8p32: the returned object is initialized from an object of the
		// function's own, which is about to end - so the resolution that
		// chooses the transfer runs first as if the operand were an rvalue.
		return_as_rvalue(value, target);
	}
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
	// 11.2p5: the class the callee's nested-name-specifier named, which 9.3.1p3
	// reaches the object through where the call wrote no object expression.
	Scope* naming_region = nullptr;
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
			named = resolve(callee.text, ctx, LookupKind::Any, found,
			                &naming_region);
			if (named != nullptr && names_a_type(*named))
			{
				return functional_cast(node, ctx, parent, named->type);
			}
			Value builtin;
			if (named == nullptr &&
			    BuiltinReading(*this).call(callee.text, arguments_of(node), ctx,
			                               parent, builtin))
			{
				return builtin;
			}
			if (named == nullptr)
			{
				// 1.4p8: the name is one the implementation reserves for a
				// function of its own, so what the program did not declare the
				// implementation declares here, once for the unit.
				named = BuiltinReading(*this).reserved(callee.text, found);
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
		!QualifiedName(callee.text).qualified() &&
		ArgumentLookup(*this).allowed(named);
	// 5.2.2p1 and 10.3p12: a virtual function called through a qualified-id is
	// the declaration that id names and not the final overrider the object's own
	// class has, and only the two forms that can name a member of an object can
	// name it either way at all.
	const bool named_by_qualified_id =
		(callee.kind != AstKind::MemberExpression &&
		 callee.kind != AstKind::IdExpression) ||
		QualifiedName(called).qualified();
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
			? named_value(callee, require(named, callee.text), ctx, line,
			              found, false, adl)
			: expression(callee, ctx, line);
		if (adl && named != nullptr && named->primary != nullptr &&
		    named->template_parameters == nullptr && named->next == nullptr &&
		    found != nullptr && found->size() == 1 && unevaluated_ == 0)
		{
			// 14.7.1p1: naming a specialization an explicit
			// template-argument-list already made is what settles the
			// declaration - its parameter-type-list is the pattern's
			// declarator read again with the arguments bound, and 14.5.3p4's
			// expansion comes to as many parameters as the run is long only
			// there.  3.4.2p3 has just taken the naming away, and 13.3 ranks
			// the candidate by that list, so the demand stands on its own.
			instantiate(*named);
		}
		if (target.functions != nullptr && target.addressed == nullptr)
		{
			implicit_object_argument(*target.functions, line, object,
			                         naming_region);
		}
	}

	const AstNode* list = arguments_of(node);
	std::vector<Value> arguments;
	// 14.5.3p4: an argument the program wrote `pattern...` stands for one
	// argument per element of the run its packs are bound to, each of them that
	// same pattern read again in a region binding the packs to that element.
	Clauses written(list, *this, ctx);
	for (; !written.spent(); ++written.at)
	{
		// 13.3.3.1.5p1: an argument written as a braced-init-list is not an
		// expression, so it is carried until 13.3 has chosen the parameter it
		// reaches.
		arguments.push_back(
			argument_expression(written.next(), written.in(ctx), line));
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
		// 3.4.2 appends its own candidates after 3.4.1's, so where the two
		// halves meet is the size the set had before it ran - which is what
		// 14.6.4.2p1 asks its question of one side of.
		const std::size_t reached = candidates.size();
		const std::size_t singles =
			adl ? ArgumentLookup(*this).call_candidates(called, arguments, ctx,
			                                            candidates)
			    : 0;
		const std::size_t associated = candidates.size() - reached;
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
			                 singles, nullptr, nullptr, associated);
		// 7.3.3p1: a using-declaration made the class declare what its base
		// declared, and what the call runs is the base's function - reached
		// through the base subobject of the object the call names, which
		// 11.2p5 leaves the base-specifier's own access unasked about.
		object.through_using = named_by_using(selected);
		SemaEntity& chosen = declared_member(selected);
		name_function(target, chosen, "callee");
		function = target.type;
		if (target.node != nullptr)
		{
			// 5.2.2p1: the call runs the final overrider the object's own class
			// has, wherever the program named the member on an object and left
			// the name unqualified.  5.2.4's explicit destructor call is named
			// on an object whose class the program wrote, and the references
			// run the declaration it named; every other special member is
			// reached through no name at all.
			target.node->fact.dispatches = chosen.virtual_function &&
				chosen.special == kOrdinaryFunction &&
				!named_by_qualified_id && chosen.object_member &&
				object.node != nullptr;
		}
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
	else
	{
		// 5.2.2p1: what is left is a call of the one value the callee came to,
		// which is the same reading 20.8.2p1 makes of `INVOKE`'s first operand.
		return call_value(line, target, arguments, ctx);
	}

	const SemaEntity* const chosen =
		target.entity != nullptr && target.entity->kind == SemaKind::Function
			? target.entity
			: nullptr;
	return finish_call(line, function, arguments, chosen, ctx);
}

// 5.2.2p1 and 13.5.4p1: the call a callee that came to one value makes.  13.3
// has nothing left to choose among by here - what the value is says which of
// the three readings the call gets, and the arguments are already read.
SemaAnalyzer::Value SemaAnalyzer::call_value(DumpNode& line, Value& target,
                                             std::vector<Value>& arguments,
                                             const Context& ctx)
{
	TypeId function = kNoType;
	if (types_.kind(target.type) == TypeKind::Function)
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
			// 5.2.2p1 with 14.7.1p1: the class of the object called shall be
			// complete where the call stands, because what the call names is a
			// member of it - the `operator()`s 13.5.4p1 looks up and the
			// conversions 13.3.1.1.2p2 builds a surrogate per.  A declaration
			// of a *reference* to a specialization asks for none of that, so a
			// call written on one is the first use that does.
			if (!types_.is_dependent(types_.strip_cv(target.type)))
			{
				require_settled_type(types_.strip_cv(target.type));
			}
			std::vector<Value> operands;
			operands.push_back(target);
			for (std::size_t index = 0; index < arguments.size(); ++index)
			{
				operands.push_back(arguments[index]);
			}
			Value chosen;
			if (OperatorCall(*this).expression(
				    OP_LPAREN, ctx, line, operands,
				    OperatorCall::member_only(OP_LPAREN), chosen))
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
			// 12.8p15 with 12.2p1: a value of class type is storage rather
			// than a value, so what crosses the ellipsis is the object - and
			// with no parameter on the other side to say how, the address is
			// the one description both ends share.  That is the half of
			// 5.2.2p4's answer the name `arg` says, so the storage is named
			// here as it is there.  Its lifetime is not: a call through an
			// ellipsis tells the function nothing about the type, so nothing
			// there could end it and 12.2p3 leaves it where it was written.
			DumpNode* const passed = arguments[index].node;
			if (passed != nullptr &&
			    passed->fact.kind == FactKind::TemporaryObject &&
			    types_.is_class(types_.strip_cv(arguments[index].type)))
			{
				passed->fact.spelling =
					requested_prefix(Requested::Argument, true, kNoType);
			}
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
	// 5.19p2 with 7.1.5p2: a call of a constexpr function whose arguments are
	// themselves constant *is* a constant expression, so the resolved node
	// carries the value rather than the call - which is what lets 3.6.2p2 give
	// an object at namespace scope its value in the program image.  A call on
	// an object is not one of these: the object it would read is one no
	// constant expression here names.
	// 8.3.6p1: a call that stops short of a place the declaration gave a
	// default-argument is one the fold fills for itself, so the written
	// arguments are all this asks for and the arity was settled above.
	// 5.2.3p2: a fold whose answer is an *object* holds the identifier of the
	// list its subobjects came to and not a value, and `value` is a value to
	// every reader of one - so only a call whose result is arithmetic carries
	// what it folded to.
	if (chosen != nullptr && !chosen->object_member &&
	    chosen->constexpr_function && chosen->constexpr_body != nullptr &&
	    arithmetic_type(value.type) != kNoType)
	{
		std::vector<Constant> folded;
		folded.reserve(arguments.size());
		for (std::size_t index = 0; index < arguments.size(); ++index)
		{
			if (!arguments[index].constant)
			{
				folded.clear();
				break;
			}
			Constant given;
			given.type = arguments[index].type;
			given.bits = arguments[index].value;
			given.real = arguments[index].real;
			folded.push_back(given);
		}
		if (folded.size() == arguments.size())
		{
			try
			{
				const Constant answer =
					ConstexprReading(*this).call(
						const_cast<SemaEntity&>(*chosen), nullptr, folded);
				value.constant = true;
				value.value = answer.bits;
				value.real = answer.real;
			}
			catch (const NotConstant&)
			{
				// 5.19p2: the body reads something no constant expression may,
				// so the call stands as one the program carries out.
			}
		}
	}
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
		list != nullptr && list->braced ? list->children[0] : nullptr;
	// 14.5.3p4: what the arguments *are* is how many the run its packs are
	// bound to holds and where each of them is read, so `T(a...)` over a run of
	// none is 5.2.3p2's `T()` and over a run of one is the pattern read in that
	// element's region rather than the entry the program wrote.
	Clauses written(list, *this, ctx);
	const std::size_t count = written.list.size();
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
	if (braced != nullptr || (count == 1 &&
	                          written.next().kind ==
	                          AstKind::BracedInitList))
	{
		// 5.2.3p3 over any other type: the braces make 8.5.4's
		// list-initialization of a prvalue of the type, which is the same
		// reading of the same list a declaration of an object of it would get.
		// 5.2.3p2's `T({...})` reaches the same place: the result object is
		// direct-initialized with the expression-list, and one clause that is
		// a list is that same list-initialization.  Only a class tells the two
		// spellings apart, because only there is a list an *argument* of a
		// constructor rather than the whole initializer.
		return braced != nullptr
			? list_initialize(*braced, target, ctx, parent)
			: list_initialize(written.next(), target, written.in(ctx), parent);
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
		// 8.5p7 and 4.10p1: the zero is the value the *object* was initialized
		// with rather than a literal the program wrote, so a pointer holds the
		// null pointer value - which LowIR spells `nullptr` - and not the
		// integer a null pointer constant is written as.
		value.node->fact.zero_initialized = true;
		return value;
	}
	if (count != 1)
	{
		throw std::runtime_error("a functional cast is written with more than "
		                         "one operand");
	}
	DumpNode& line = model_.open_node(parent, std::string());
	Value source = expression(written.next(), written.in(ctx), line);
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
		return cast_to_reference(target, source, parent, line, value, ctx);
	}
	cast_conversion(source, target, ctx);
	value.node = &line;
	respell(value);
	return value;
}
