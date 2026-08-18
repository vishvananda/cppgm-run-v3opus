#include "sema_deduce.h"

#include <utility>

#include "sema_analyzer.h"
#include "sema_pack.h"
#include "sema_template_head.h"

// 14.8.2, the whole of it this milestone reads.
//
// Every entry point below ends the same way: a map from each place of the head
// to what it was deduced to, handed to `arguments_of`, which turns it into the
// one flat argument list 14.4p1 makes a specialization out of.  What differs is
// only where the P/A pairs came from.

Deduction::Deduction(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

Substitution::Substitution(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
	, stood_(analyzer.stood_in_)
{}

bool Substitution::discards(const std::runtime_error& why)
{
	if (dynamic_cast<const Instantiated*>(&why) != nullptr)
	{
		// 14.8.2p8: the reading of an instantiated template body is no part of
		// the immediate context, so what it refused refuses the program however
		// many substitutions were waiting on it.
		return false;
	}
	if (analyzer_.checking_ != 0 && analyzer_.stood_in_ != stood_)
	{
		// 14.6p8: the reading ran out on a value it stood in for one an
		// argument list has yet to settle, so it says nothing about this
		// substitution and belongs to the reading that made the stand-in.
		return false;
	}
	// The stand-ins a discarded attempt made are no part of what the reading
	// around it stood in for.
	analyzer_.stood_in_ = stood_;
	return true;
}

SemaEntity* Substitution::specialize(SemaEntity& primary,
                                     const std::vector<TypeId>& arguments,
                                     std::string& refused)
{
	try
	{
		return &analyzer_.specialize(primary, arguments);
	}
	catch (const std::runtime_error& why)
	{
		if (!discards(why))
		{
			throw;
		}
		if (refused.empty())
		{
			refused = why.what();
		}
		return nullptr;
	}
}

// 14.8.3p1 with 14.8.2p8: a template is a candidate through the specialization
// a use makes of it, and no candidate at all where the use deduces none *or*
// where substituting what it deduced builds a declaration that is ill formed.
SemaEntity* Deduction::from_call(SemaEntity& primary,
                                 const std::vector<AnalyzedValue>& arguments)
{
	Substitution attempt(analyzer_);
	try
	{
		return deduced_call(primary, arguments);
	}
	catch (const std::runtime_error& why)
	{
		if (!attempt.discards(why))
		{
			throw;
		}
		return nullptr;
	}
}

SemaEntity* Deduction::from_target(SemaEntity& primary, TypeId wanted)
{
	Substitution attempt(analyzer_);
	try
	{
		return deduced_target(primary, wanted);
	}
	catch (const std::runtime_error& why)
	{
		if (!attempt.discards(why))
		{
			throw;
		}
		return nullptr;
	}
}

SemaEntity* Deduction::from_conversion(SemaEntity& primary, TypeId wanted)
{
	Substitution attempt(analyzer_);
	try
	{
		return deduced_conversion(primary, wanted);
	}
	catch (const std::runtime_error& why)
	{
		if (!attempt.discards(why))
		{
			throw;
		}
		return nullptr;
	}
}

bool Deduction::match(TypeId pattern, TypeId argument,
                      std::unordered_map<TypeId, TypeId>& bindings,
                      bool relaxed, bool derived)
{
	TypeTable& types = analyzer_.types_;
	if (types.dependent_owner(types.strip_cv(pattern)) != kNoType)
	{
		// 14.8.2.5p5: the nested-name-specifier of a type written as a
		// qualified-id is a non-deduced context, so the pair says nothing about
		// what the prefix names.  The parameter is settled by what the other
		// pairs deduce and by the substitution that follows them, and 13.3 is
		// then what asks whether the argument reaches it.
		return true;
	}
	const TypeId applied = types.applied_template(types.strip_cv(pattern));
	if (applied != kNoType)
	{
		return match_template_id(pattern, argument, applied, bindings, relaxed,
		                         derived);
	}
	if (types.kind(pattern) == TypeKind::TemplateParameter)
	{
		// 14.8.2.5p4: the qualifiers the parameter writes are matched by the
		// argument's own, and what is left of the argument is what the
		// parameter names.  14.8.2.1p4 lets the top of a reference parameter
		// write qualifiers the argument does not have, because binding the
		// reference adds them.
		if (!relaxed && (types.cv(pattern) & ~types.cv(argument)) != 0)
		{
			return false;
		}
		const TypeId deduced =
			types.qualified(types.strip_cv(argument),
			                types.cv(argument) & ~types.cv(pattern));
		const std::pair<std::unordered_map<TypeId, TypeId>::iterator, bool> held =
			bindings.insert(std::make_pair(types.strip_cv(pattern), deduced));
		// 14.8.2.5p2: two arguments that deduce one parameter differently
		// deduce nothing.
		return held.second || held.first->second == deduced;
	}
	if (types.kind(pattern) != types.kind(argument) ||
	    (relaxed ? (types.cv(argument) & ~types.cv(pattern)) != 0
	             : types.cv(pattern) != types.cv(argument)))
	{
		return false;
	}
	switch (types.kind(pattern))
	{
	case TypeKind::Pointer:
	{
		// 14.8.2.1p3: a pointer to a class of the form simple-template-id takes
		// a pointer to a class derived from it, so what the pair allows carries
		// through the one indirection the clause names.  The base reached that
		// way is 4.4's qualification conversion away from what the pointer was
		// written as, so the qualifiers P writes over it are its own; a pointee
		// that is no simple-template-id is matched as it stands.
		const TypeId inner = types.target(pattern);
		// A simple-template-id is written two ways here - over a template the
		// program declared, and over a place its own head declared - and the
		// clause is about the form and not about which of the two it named.
		const bool simple = derived &&
			((types.kind(inner) == TypeKind::Class &&
			  types.is_specialization(inner)) ||
			 types.applied_template(types.strip_cv(inner)) != kNoType);
		return match(inner, types.target(argument), bindings, simple, simple);
	}

	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		return match(types.target(pattern), types.target(argument), bindings);

	case TypeKind::Array:
		return match_bound(pattern, argument, bindings) &&
			match(types.target(pattern), types.target(argument), bindings);

	case TypeKind::MemberPointer:
		return match(types.member_class(pattern), types.member_class(argument),
		             bindings) &&
			match(types.target(pattern), types.target(argument), bindings);

	case TypeKind::Function:
		// 8.3.5p1 with 14.5.3p4: a parameter list is a list of entries paired
		// one for one, with a trailing `P...` standing for every entry the ones
		// before it did not take - which is what a template-argument-list is,
		// so it is the same match.  14.8.2.2's target type is the one A written
		// over a whole function type, and this is where its parameters are read.
		//
		// 8.3.5p7: the ref-qualifier is part of the function type and holds no
		// place to deduce, so two that spell it differently are two types - which
		// is what leaves `holder<R(A...)>` and `holder<R(A...) &>` two patterns
		// one argument list matches one of.  The cv-qualifier-seq is compared
		// above, where every other type's is.
		return types.variadic(pattern) == types.variadic(argument) &&
			types.function_ref_qualifier(pattern) ==
				types.function_ref_qualifier(argument) &&
			match(types.target(pattern), types.target(argument), bindings) &&
			match_arguments(types.parameters(pattern),
			                types.parameters(argument), bindings);

	case TypeKind::Class:
	{
		// 14.8.2.5p4: `A<T>` against `A<int>` deduces `T` from `int`, which is
		// the one class-typed pattern that holds a parameter at all.  The two
		// facts a specialization records - the template it was made of and the
		// arguments that made it - are what the match is over.
		if (pattern == argument || !types.is_specialization(pattern))
		{
			return pattern == argument;
		}
		if (types.is_specialization(argument) &&
		    types.template_name(pattern) == types.template_name(argument))
		{
			return match_arguments(types.template_arguments(pattern),
			                       types.template_arguments(argument), bindings);
		}
		// 14.8.2.1p3: where the pair is one the use wrote out, the A it deduces
		// may be a base of what was passed - so a call passing a class derived
		// from one specialization of P's template deduces from that base.
		const TypeId base = derived ? derived_from(pattern, argument) : kNoType;
		return base != kNoType &&
			match_arguments(types.template_arguments(pattern),
			                types.template_arguments(base), bindings);
	}

	case TypeKind::Value:
		// 14.3.2p1: a value argument is the bits it holds together with the type
		// it was converted to, and only the second of those can name a place -
		// `template<class T, T v>` written in a pattern as `ic<T, true>` is the
		// settled `true` over a type this pair deduces.  So the bits agree
		// exactly and the types are a pair of their own.
		return types.value_bits(pattern) == types.value_bits(argument) &&
			match(types.target(pattern), types.target(argument), bindings);

	default:
		// A fundamental type or an enumeration holds no parameter to deduce,
		// so the two agree exactly when they are the same type.
		return pattern == argument;
	}
}

// 14.8.2.1p3 at a template place: the A a pair written out by the use deduces
// may be a base of what was passed, and `L<A…>` names no one template for
// `named_below` to look for - what it asks of a base is 14.3.3p1's question,
// which is the same one the pair itself asks.
//
// `argument` itself where it already is a naming this pair can read, and the
// first class below it that is one otherwise.  10p1 makes a base of a base a
// base, so the whole tree is walked.
TypeId Deduction::derived_template_id(TypeId argument, TypeId place,
                                      bool derived) const
{
	TypeTable& types = analyzer_.types_;
	if (types.applied_template(argument) != kNoType ||
	    (types.kind(argument) == TypeKind::Class &&
	     types.is_specialization(argument)))
	{
		return argument;
	}
	const SemaEntity* const owner = analyzer_.model_.type_owner(argument);
	return derived && owner != nullptr ? specialization_below(*owner, place)
	                                   : kNoType;
}

TypeId Deduction::specialization_below(const SemaEntity& at, TypeId place) const
{
	TemplateHead heads(analyzer_);
	const TemplateInfo* const wanted = analyzer_.place_head(place);
	for (std::size_t index = 0; index < at.bases.size(); ++index)
	{
		const SemaEntity& base = *at.bases[index].entity;
		const TypeId type = analyzer_.types_.strip_cv(base.type);
		if (analyzer_.types_.is_specialization(type) &&
		    base.primary != nullptr && base.primary->templated != nullptr &&
		    (wanted == nullptr ||
		     heads.argument_matches(*wanted, *base.primary->templated)))
		{
			return type;
		}
		const TypeId deeper = specialization_below(base, place);
		if (deeper != kNoType)
		{
			return deeper;
		}
	}
	return kNoType;
}

// 14.8.2.5p4 at a template place: `L<A…>` against what an argument list was
// already applied to.
//
// The pair says two things at once, which is what makes it one arm rather than
// a case of the class arm below: which *template* the argument was applied to,
// which is what `L` is deduced to, and what its arguments were, which the rest
// of the pattern is matched against.  14.3.3p1 stands over the first of them -
// a template whose head does not fit the place is no deduction of it, and the
// pattern simply does not match - so a partial specialization written over a
// place of one shape passes a class made from another straight through to the
// primary.
//
// What the argument was applied to is a template the program declared wherever
// an argument list already made a class of it, and a place standing for itself
// where it did not.  14.5.5.2p1's ordering asks this pair of two patterns, so
// the second is the reading that says which of two partial specializations is
// the more specialized - and it is the same two questions of the same two
// heads.
bool Deduction::match_template_id(TypeId pattern, TypeId argument, TypeId place,
                                  std::unordered_map<TypeId, TypeId>& bindings,
                                  bool relaxed, bool derived)
{
	TypeTable& types = analyzer_.types_;
	if (relaxed ? (types.cv(argument) & ~types.cv(pattern)) != 0
	            : types.cv(pattern) != types.cv(argument))
	{
		// 14.8.2.5p4: the qualifiers stand in the pair as they are written, so
		// `L<T…>` is no match for a class an argument list wrote `const` on -
		// which is what leaves `holder<T const>` the one pattern it matches.
		// 14.8.2.1p4 is the one allowance: binding a reference parameter adds
		// the qualifiers P wrote, so a pair reached through one asks only that
		// the argument's own are among them - the same reading every other pair
		// of this walk is given.
		return false;
	}
	const TypeId bare = derived_template_id(types.strip_cv(argument), place,
	                                        derived);
	if (bare == kNoType)
	{
		return false;
	}
	const TypeId over = types.applied_template(bare);
	const TemplateInfo* given = nullptr;
	TypeId named = kNoType;
	if (over != kNoType)
	{
		given = analyzer_.place_head(over);
		named = over;
	}
	else
	{
		if (types.kind(bare) != TypeKind::Class ||
		    !types.is_specialization(bare))
		{
			return false;
		}
		SemaEntity* const made = analyzer_.model_.type_owner(bare);
		if (made == nullptr || made->primary == nullptr ||
		    made->primary->templated == nullptr)
		{
			return false;
		}
		given = made->primary->templated;
		named = TemplateHead(analyzer_).name_argument(*made->primary);
	}
	const TemplateInfo* const head = analyzer_.place_head(place);
	if (head != nullptr && given != nullptr &&
	    !TemplateHead(analyzer_).argument_matches(*head, *given))
	{
		return false;
	}
	const std::pair<std::unordered_map<TypeId, TypeId>::iterator, bool> held =
		bindings.insert(std::make_pair(place, named));
	if (!held.second && held.first->second != named)
	{
		// 14.8.2.5p2: two arguments that deduce one place differently deduce
		// nothing.
		return false;
	}
	return match_arguments(types.template_arguments(types.strip_cv(pattern)),
	                       types.template_arguments(bare), bindings);
}

// 14.8.2.5p13: 8.3.4p1's bound as a P/A pair of its own.
//
// A bound the pattern wrote a place in is deduced from the number the argument
// has, which is 5.19's value the reading of the argument's own declarator
// already arrived at.  The clause gives that value the type `std::size_t`, and
// 14.3.2p5 then converts it to the type the place declared - so `T[N]` over a
// `char N` and an argument of 300 elements deduces the 300 that place holds and
// not the 300 the array has, which is what makes the check below its own rather
// than a comparison of the bits the argument was written with.
//
// 14.5.5.2p1's partial ordering asks this same question of two *patterns*, where
// the argument's own bound is a place standing for itself.  That place is what
// the clause synthesizes a unique value as, so the pattern's place is deduced to
// it exactly as a type place is deduced to another type place - which is what
// makes `const T[N]` more specialized than `T[M]` and leaves neither of two
// bounds that both name places refusing the other.
bool Deduction::match_bound(TypeId pattern, TypeId argument,
                            std::unordered_map<TypeId, TypeId>& bindings)
{
	TypeTable& types = analyzer_.types_;
	const TypeId place = types.bound_place(pattern);
	if (place == kNoType)
	{
		// A pattern that wrote a number takes the same number and nothing else:
		// a bound standing for a place is no 5 however it is later settled.
		return types.bounded(pattern) == types.bounded(argument) &&
			types.bound(pattern) == types.bound(argument) &&
			types.bound_place(argument) == kNoType;
	}
	if (!types.bounded(argument))
	{
		// 8.3.4p1: an array of unknown bound has no value for the place to be
		// deduced from, so the pair says nothing and the deduction fails.
		return false;
	}
	TypeId deduced = types.bound_place(argument);
	if (deduced == kNoType)
	{
		// 14.8.2.5p13: the value is the number the argument's own declarator
		// arrived at, taken as `std::size_t` and then brought to the type the
		// place declared by 14.3.2p5 - so `T[N]` over a `char N` deduces the
		// value that place holds and not the bits the array was written with.
		SemaAnalyzer::Constant given;
		given.type = types.fundamental(FT_UNSIGNED_LONG_INT);
		given.bits = types.bound(argument);
		const TypeId declared = types.strip_cv(types.parameter_value_type(place));
		if (types.kind(declared) != TypeKind::Fundamental ||
		    !types.is_integral(declared))
		{
			// 14.8.2.5p17: an argument deduced from an array bound may be of
			// any *integral* type and of no other, so a place 3.9.1p7 does not
			// name one of deduces nothing here.  An enumeration is the reachable
			// one - 5.19p3 admits no conversion from the `std::size_t` the
			// clause gives the value to it, so `T[N]` over an `E N` matches no
			// array however the enumerators are numbered.
			return false;
		}
		deduced =
			types.value_type(declared, analyzer_.convert(given, declared).bits);
	}
	const std::pair<std::unordered_map<TypeId, TypeId>::iterator, bool> held =
		bindings.insert(std::make_pair(place, deduced));
	// 14.8.2.5p2: two bounds that deduce one place differently deduce nothing.
	return held.second || held.first->second == deduced;
}

// 14.5.3p1 with 14.8.2.5p4: two lists of entries - a template-argument-list or
// 8.3.5p1's parameter list, which the match reads by one rule - where the
// pattern's may end in 14.5.3p4's `P...`.  That entry is one place standing for
// a run, so the entries before it are paired one for one and every argument
// left is a pair of its own against the expansion's pattern.
bool Deduction::match_arguments(const std::vector<TypeId>& wanted,
                                const std::vector<TypeId>& given,
                                std::unordered_map<TypeId, TypeId>& bindings)
{
	TypeTable& types = analyzer_.types_;
	const bool packed =
		!wanted.empty() && types.is_pack_expansion(wanted[wanted.size() - 1]);
	const std::size_t fixed = packed ? wanted.size() - 1 : wanted.size();
	if (packed ? given.size() < fixed : given.size() != fixed)
	{
		return false;
	}
	for (std::size_t index = 0; index < fixed; ++index)
	{
		if (types.is_pack_expansion(wanted[index]))
		{
			// 14.5.3p1: a run this milestone deduces is the last of the list,
			// because an expansion before one is a run of no known length.
			return false;
		}
		if (types.is_pack_expansion(given[index]))
		{
			// 14.5.3p1 the other way round: this place takes one entry and the
			// entry facing it stands for a run of no known length, so there is no
			// argument for it to take.  It matters where A is itself a *pattern* -
			// 14.5.5.2p1 asks this pair of two partial specializations, and
			// `<Prev, Last>` deducing its `Last` from `<Prev, Others...>`'s run
			// would make each of the two at least as specialized as the other and
			// every list they both match ambiguous.
			return false;
		}
		if (!match(wanted[index], given[index], bindings))
		{
			return false;
		}
	}
	return !packed ||
		match_run(wanted[wanted.size() - 1], given, fixed, bindings);
}

bool Deduction::match_run(TypeId expansion, const std::vector<TypeId>& given,
                          std::size_t from,
                          std::unordered_map<TypeId, TypeId>& bindings)
{
	TypeTable& types = analyzer_.types_;
	const TypeId inner = types.target(expansion);
	std::vector<TypeId> runs;
	std::vector<TypeId> places;
	PackReading(analyzer_).packs_in(inner, runs, places);
	if (places.empty())
	{
		// 14.8.2.5p9: an expansion whose pattern names no place this pair could
		// deduce says nothing about how long the run is.
		return false;
	}
	// 14.5.3p4 and 14.5.3p6: one expansion may be written over several packs,
	// which are expanded together and are the same length - `pair<A, B>...` is
	// one run of pairs and two runs of one element each per pair.  So the pair
	// takes a run per place, which is what `expand_type` and `substitute_entry`
	// already read one over.
	std::vector<std::vector<TypeId> > taken(places.size());
	for (std::size_t index = 0; index < places.size(); ++index)
	{
		taken[index].reserve(given.size() - from);
	}
	for (std::size_t at = from; at < given.size(); ++at)
	{
		// Each element is a P/A pair of its own, over bindings of its own: what
		// a pack place took there is one element of its run, and what it took in
		// another element is a different one.
		//
		// 14.8.2.5p2: every *other* place the pattern names binds one argument
		// and not a run - `tuple<S<E>...>` deduces its `S` once from every
		// element - so each pair starts from what the pairs before it deduced
		// and what this one adds is merged back.
		std::unordered_map<TypeId, TypeId> one(bindings);
		// A pack place is one this pair may not start from: what an earlier list
		// deduced it to is a *run*, and what this element deduces it to is one
		// element of one.
		for (std::size_t index = 0; index < places.size(); ++index)
		{
			one.erase(places[index]);
		}
		if (!match(inner, given[at], one))
		{
			return false;
		}
		for (std::size_t index = 0; index < places.size(); ++index)
		{
			const std::unordered_map<TypeId, TypeId>::const_iterator took =
				one.find(places[index]);
			if (took == one.end())
			{
				return false;
			}
			taken[index].push_back(took->second);
			one.erase(places[index]);
		}
		for (std::unordered_map<TypeId, TypeId>::const_iterator entry =
			     one.begin();
		     entry != one.end(); ++entry)
		{
			const std::pair<std::unordered_map<TypeId, TypeId>::iterator, bool>
				held = bindings.insert(*entry);
			if (!held.second && held.first->second != entry->second)
			{
				return false;
			}
		}
	}
	// 14.8.2.5p2 over a run: two lists that deduce one pack differently deduce
	// nothing, exactly as two arguments at one place do.
	for (std::size_t index = 0; index < places.size(); ++index)
	{
		const TypeId bound = types.pack_type(taken[index]);
		const std::pair<std::unordered_map<TypeId, TypeId>::iterator, bool> held =
			bindings.insert(std::make_pair(places[index], bound));
		if (!held.second && held.first->second != bound)
		{
			return false;
		}
	}
	return true;
}

TypeId Deduction::derived_from(TypeId pattern, TypeId argument) const
{
	const SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(argument));
	// 10p1: a base of a base is a base, so the whole tree below the argument is
	// walked - one visit per class in it, because 10.1p3's repeated base is
	// refused where the class is completed.
	return owner == nullptr ? kNoType : named_below(pattern, *owner);
}

TypeId Deduction::named_below(TypeId pattern, const SemaEntity& at) const
{
	TypeTable& types = analyzer_.types_;
	for (std::size_t index = 0; index < at.bases.size(); ++index)
	{
		const TypeId base = at.bases[index].entity->type;
		if (types.is_specialization(base) &&
		    types.template_name(base) == types.template_name(pattern))
		{
			return base;
		}
		const TypeId deeper = named_below(pattern, *at.bases[index].entity);
		if (deeper != kNoType)
		{
			return deeper;
		}
	}
	return kNoType;
}

TypeId Deduction::written_part(SemaEntity& primary, SemaEntity& made_of,
                               std::unordered_map<TypeId, TypeId>& bindings)
{
	// 14.8.1p2: a template-id that wrote a leading part of the argument list
	// names the template with those arguments already given, so the deduction
	// begins from them: they are substituted into the type before the P/A pairs
	// are read, which is what leaves a parameter they made non-dependent to
	// 13.3's conversion rather than to a deduction that would refuse it.
	if (primary.partial_of == nullptr)
	{
		return primary.type;
	}
	const std::vector<TypeId>& given =
		analyzer_.types_.type_list_at(primary.template_arguments);
	const std::vector<SemaEntity*>& places =
		made_of.template_parameters->declarations;
	for (std::size_t index = 0; index < given.size() && index < places.size();
	     ++index)
	{
		bindings.insert(std::make_pair(places[index]->type, given[index]));
	}
	std::unordered_map<TypeId, TypeId> memo;
	return analyzer_.substituted(made_of.type, bindings, memo);
}

SemaEntity* Deduction::deduced_call(SemaEntity& primary,
                                    const std::vector<AnalyzedValue>& arguments)
{
	TypeTable& types = analyzer_.types_;
	SemaEntity& made_of =
		primary.partial_of != nullptr ? *primary.partial_of : primary;
	std::unordered_map<TypeId, TypeId> bindings;
	const TypeId written_type = written_part(primary, made_of, bindings);
	// 14.8.2.1p1: each parameter is deduced from the argument passed to it, so
	// a call that passes more of them deduces nothing.  8.3.6p1 lets a call
	// leave the trailing parameters a default-argument stands for unwritten;
	// those deduce nothing, and 14.8.2p5 below is what says whether what is
	// left named every parameter.
	//
	// 9.3.1p3's object parameter is none of those pairs: no argument is written
	// for it and its type is the class's, which a class template's own argument
	// list settled before this head declared anything - so a member template
	// deduces from the arguments after it.
	const std::size_t implicit = made_of.object_member ? 1u : 0u;
	const std::vector<TypeId>& pattern = types.parameters(written_type);
	const std::size_t places =
		pattern.size() > implicit ? pattern.size() - implicit : 0u;
	// 14.5.3p4 and 14.8.2.1p1: a trailing parameter written `P...` is one
	// pattern standing for every argument the places before it did not take, so
	// what a call has to write is those places and not this one.
	const bool packed =
		places > 0 && types.is_pack_expansion(pattern[pattern.size() - 1]);
	const std::size_t fixed = packed ? places - 1 : places;
	if (pattern.size() < implicit || types.variadic(written_type) ||
	    (packed ? arguments.size() < fixed
	            : (arguments.size() > places ||
	               (arguments.size() < places &&
	                !analyzer_.accepts_arity(made_of,
	                                         arguments.size() + implicit)))))
	{
		return nullptr;
	}
	for (std::size_t at = 0; at < arguments.size() && at < fixed; ++at)
	{
		if (!match_argument(pattern[at + implicit], arguments[at], bindings))
		{
			return nullptr;
		}
	}
	if (packed)
	{
		// 14.8.2.1p1 over 14.5.3p4's trailing pattern: each argument left is a
		// P/A pair against the same pattern, and what the pack is deduced to is
		// the run of what its own place took in each of them.
		const TypeId inner = types.target(pattern[fixed + implicit]);
		std::vector<TypeId> runs;
		std::vector<TypeId> named;
		PackReading(analyzer_).packs_in(inner, runs, named);
		if (named.size() != 1)
		{
			return nullptr;
		}
		std::vector<TypeId> run;
		for (std::size_t at = fixed; at < arguments.size(); ++at)
		{
			std::unordered_map<TypeId, TypeId> one;
			if (!match_argument(inner, arguments[at], one))
			{
				return nullptr;
			}
			const std::unordered_map<TypeId, TypeId>::const_iterator took =
				one.find(named[0]);
			if (took == one.end())
			{
				return nullptr;
			}
			run.push_back(took->second);
		}
		bindings.insert(std::make_pair(named[0], types.pack_type(run)));
	}

	std::vector<TypeId> deduced;
	if (!arguments_of(made_of, bindings, deduced))
	{
		return nullptr;
	}
	return &analyzer_.specialize(made_of, deduced);
}

// 14.8.2.1p1's one pair: the parameter type as it stands against the type of
// what the call put there.
bool Deduction::match_argument(TypeId parameter, const AnalyzedValue& argument,
                               std::unordered_map<TypeId, TypeId>& bindings)
{
	TypeTable& types = analyzer_.types_;
	// 14.8.2.5p3: a parameter written over no template parameter deduces
	// nothing at all, so whether the argument reaches it is 13.3's question
	// about a conversion rather than this one about a substitution.
	if (!types.is_dependent(parameter))
	{
		return true;
	}
	// 14.8.2.1p2: where the parameter is a reference, what deduces the
	// arguments is the type it refers to, and the argument's own type is used
	// as it stands rather than decayed.
	const TypeKind kind = types.kind(parameter);
	const bool reference = kind == TypeKind::LValueReference ||
		kind == TypeKind::RValueReference;
	const TypeId expected = reference ? types.target(parameter) : parameter;
	if (argument.type == kNoType)
	{
		// 14.8.2.1p6: an argument that is an overload set has no type of its
		// own, so the deduction is tried against each declaration in it and
		// stands only where exactly one of them deduces.  A set holding a
		// template is left alone: 13.4p1 has no target type here to make one
		// of its specializations, so the parameter deduces nothing at all.
		return overload_set(argument, expected, bindings, reference);
	}
	TypeId given = reference ? argument.type : analyzer_.decayed(argument);
	// 14.8.2.1p3: an rvalue reference written over a parameter the declarator
	// wrote no qualifier on is deduced from "lvalue reference to A" wherever
	// the argument is an lvalue, so 8.3.2p6's collapsing of the two references
	// is what makes the parameter bind it.
	if (kind == TypeKind::RValueReference &&
	    types.kind(expected) == TypeKind::TemplateParameter &&
	    types.cv(expected) == 0 &&
	    argument.category == ValueCategory::LValue)
	{
		given = types.reference_to(given, false);
	}
	return match(expected, qualification_converted(expected, given), bindings,
	             reference, true);
}

TypeId Deduction::qualification_converted(TypeId pattern, TypeId argument)
{
	TypeTable& types = analyzer_.types_;
	const TypeId under_pattern = types.strip_cv(pattern);
	const TypeId under_argument = types.strip_cv(argument);
	if (types.kind(under_pattern) != TypeKind::Pointer ||
	    types.kind(under_argument) != TypeKind::Pointer)
	{
		// 4.4p1 writes the conversion over a pointer's pointee, so a pair that
		// is no longer one pointer against another is read as it stands.
		return argument;
	}
	const TypeId inner_pattern = types.target(under_pattern);
	const TypeId written = types.target(under_argument);
	const TypeId inner = qualification_converted(inner_pattern, written);
	const unsigned added = types.cv(inner_pattern) & ~types.cv(inner);
	if (added == 0 && inner == written)
	{
		return argument;
	}
	return types.qualified(
		types.pointer_to(types.qualified(inner, added)), types.cv(argument));
}

// 14.8.2p5 and 14.1p9: the argument each parameter of `primary` was deduced or,
// where the deduction reached none, the one its head wrote a default for - which
// is 14.8.1p2's other arm, a trailing argument being omitted where it can be
// deduced *or* obtained from a default.  False where a place is left with
// neither, because there is nothing to substitute for it.
//
// A default is read in a region of its own, because 14.1p9 lets it name the
// places before it and what those name is what the deduction settled - so
// `class U = T` takes what `T` deduced, `int M = N + 1` takes the constant `N`
// took, and a later default takes what an earlier one filled.
bool Deduction::arguments_of(const SemaEntity& primary,
                             const std::unordered_map<TypeId, TypeId>& bindings,
                             std::vector<TypeId>& out)
{
	TypeTable& types = analyzer_.types_;
	const std::vector<SemaEntity*>& parameters =
		primary.template_parameters->declarations;
	out.reserve(parameters.size());
	std::unordered_map<TypeId, TypeId> filled;
	bool defaulted = false;
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		const std::unordered_map<TypeId, TypeId>& settled =
			defaulted ? filled : bindings;
		const std::unordered_map<TypeId, TypeId>::const_iterator bound =
			settled.find(parameters[index]->type);
		if (bound != settled.end())
		{
			if (types.is_settled_run(bound->second) &&
			    index + 1 == parameters.size())
			{
				// 14.5.3p1: the arguments of a specialization are one list,
				// which the *last* place contributes its whole run to - so
				// `f<int, char>` and a deduction that took the two are one
				// argument list.  A run at any earlier place stands as one
				// entry, because a flat list could not say where it ended.
				const std::vector<TypeId>& run =
					types.pack_elements(bound->second);
				out.insert(out.end(), run.begin(), run.end());
				continue;
			}
			out.push_back(bound->second);
			continue;
		}
		if (types.is_template_pack(parameters[index]->type))
		{
			// 14.8.2.1p1: a pack the call reached with no argument at all is
			// bound to a run of none rather than left unsettled - which the
			// last place writes as no entries and every earlier one as the
			// empty run standing at its own place.
			if (index + 1 != parameters.size())
			{
				out.push_back(types.pack_type(std::vector<TypeId>()));
			}
			continue;
		}
		const std::unordered_map<std::uint32_t, const AstNode*>::const_iterator
			written = analyzer_.parameter_defaults_.find(parameters[index]->id);
		if (written == analyzer_.parameter_defaults_.end())
		{
			return false;
		}
		if (!defaulted)
		{
			filled = bindings;
			defaulted = true;
		}
		// 14.1p9: the default may name the places written before it, so it is
		// read in a region of its own binding each of them to what the
		// deduction settled - the region the class tier's own fill opens.  A
		// type-id could be read against the head's region and substituted
		// afterwards, but 5.19's constant expression is *evaluated* where it
		// stands, so a place it names has to be a constant there.
		SemaContext inner;
		inner.scope = &analyzer_.model_.open(
			ScopeKind::TemplateParameters,
			*primary.template_parameters->parent, nullptr,
			primary.template_parameters->dump);
		inner.dump = inner.scope->dump;
		inner.node = nullptr;
		for (std::size_t before = 0; before < index; ++before)
		{
			const std::unordered_map<TypeId, TypeId>::const_iterator took =
				filled.find(parameters[before]->type);
			if (parameters[before]->name.empty() || took == filled.end())
			{
				// 14.1p3: a place its head left unnamed is one no default can
				// have written, and a pack the deduction reached with nothing
				// stands for no argument at all.
				continue;
			}
			TemplateHead(analyzer_).bind(*inner.scope, parameters[before]->name,
			                        took->second, SemaKind::Typedef);
		}
		std::unordered_map<TypeId, TypeId> memo;
		// 14.1p4: what the place *is* says how its default is read - a type
		// place's is 8.1p1's type-id and a value place's is 5.19's constant
		// expression, converted to the type the place declared.
		const TypeId place = analyzer_.substituted(
			types.parameter_value_type(parameters[index]->type), filled, memo);
		TypeId given;
		if (types.is_parameter_template(parameters[index]->type))
		{
			// 14.1p9 at a template place: what the default names is a template,
			// which is neither 8.1p1's type-id nor 5.19's constant expression -
			// the same reading the class tier's own fill gives it.
			given = TemplateHead(analyzer_).place_default(
				*written->second,
				analyzer_.place_head(parameters[index]->type), inner);
		}
		else if (place == kNoType)
		{
			given = analyzer_.substituted(
				analyzer_.type_id_type(*written->second, inner), filled, memo);
		}
		else if (types.is_dependent(place))
		{
			return false;
		}
		else
		{
			given = types.value_type(
				place,
				analyzer_.convert(analyzer_.evaluate(*written->second, inner),
				                  place).bits);
		}
		filled.insert(std::make_pair(parameters[index]->type, given));
		out.push_back(given);
	}
	return true;
}

// 14.8.2.1p6: the deduction one parameter gets from an argument that is an
// unresolved overload set.  Each declaration in the set is tried on a copy of
// the bindings so far, and the deduction stands only where exactly one of them
// succeeded - two that both deduce leave the parameter as ambiguous as the name
// was.  A set holding a function template names no one type to try, so the
// parameter is left deducing nothing rather than refused.
bool Deduction::overload_set(const AnalyzedValue& argument, TypeId expected,
                             std::unordered_map<TypeId, TypeId>& bindings,
                             bool reference)
{
	if (argument.functions == nullptr)
	{
		// 13.3.3.1.5p1's braced-init-list is the other value with no type, and
		// it names nothing a parameter can be deduced from.
		return false;
	}
	std::unordered_map<TypeId, TypeId> only;
	std::size_t deduced = 0;
	for (std::size_t index = 0; index < argument.functions->size(); ++index)
	{
		for (SemaEntity* at = (*argument.functions)[index]; at != nullptr;
		     at = at->next)
		{
			if (at->template_parameters != nullptr || at->object_member)
			{
				// 13.4p1 chooses between the non-member declarations of the name;
				// a member's pointer is a pointer to member, which 14.8.2.5 makes
				// a pair of its own that this argument did not write.
				continue;
			}
			// 4.3p1: what the name stands for where it is passed is a pointer to
			// the function, which is what a non-reference parameter is deduced
			// from and what a reference parameter refers to.
			std::unordered_map<TypeId, TypeId> tried(bindings);
			if (!match(expected,
			           reference ? at->type : analyzer_.types_.pointer_to(at->type),
			           tried, reference))
			{
				continue;
			}
			++deduced;
			only.swap(tried);
		}
	}
	if (deduced != 1)
	{
		// 14.8.2.1p6: a set no member of deduces, and one two members deduce
		// differently, both leave the parameter a non-deduced context - so what
		// it is written over is deduced from the arguments elsewhere and 13.4p1
		// then picks the declaration the substituted parameter names.
		return true;
	}
	bindings.swap(only);
	return true;
}

SemaEntity* Deduction::deduced_target(SemaEntity& primary, TypeId wanted)
{
	// 14.8.2.2p1: the target type is the one A the deduction is over, so the
	// result type and every parameter type of the template are matched against
	// it at once - which is what `match` does with a function type.
	SemaEntity& made_of =
		primary.partial_of != nullptr ? *primary.partial_of : primary;
	std::unordered_map<TypeId, TypeId> bindings;
	if (!match(written_part(primary, made_of, bindings), wanted, bindings))
	{
		return nullptr;
	}
	std::vector<TypeId> deduced;
	if (!arguments_of(made_of, bindings, deduced))
	{
		return nullptr;
	}
	SemaEntity& made = analyzer_.specialize(made_of, deduced);
	// 14.8.2.2p2: what the target names is a declaration of exactly that type,
	// so a substitution that produced another one chose nothing.
	return made.type == wanted ? &made : nullptr;
}

SemaEntity* Deduction::deduced_conversion(SemaEntity& primary, TypeId wanted)
{
	TypeTable& types = analyzer_.types_;
	SemaEntity& made_of =
		primary.partial_of != nullptr ? *primary.partial_of : primary;
	std::unordered_map<TypeId, TypeId> bindings;
	// 12.3.2p1: the conversion-type-id is the function's result type, so the
	// one P of this deduction is what the declarator handed back.
	TypeId pattern = types.target(written_part(primary, made_of, bindings));
	TypeId argument = wanted;
	if (!types.is_reference(argument))
	{
		// 14.8.2.3p2: where A is no reference type, a P that is one is the type
		// it refers to and neither side carries the cv-qualification a prvalue
		// of the type would not - so `operator U()` deduces `U` from `const
		// int` exactly as it does from `int`.
		if (types.is_reference(pattern))
		{
			pattern = types.target(pattern);
		}
		pattern = types.strip_cv(pattern);
		argument = types.strip_cv(argument);
	}
	if (!match(pattern, argument, bindings))
	{
		return nullptr;
	}
	std::vector<TypeId> deduced;
	if (!arguments_of(made_of, bindings, deduced))
	{
		return nullptr;
	}
	return &analyzer_.specialize(made_of, deduced);
}
