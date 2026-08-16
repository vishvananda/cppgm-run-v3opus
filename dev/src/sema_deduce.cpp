#include "sema_deduce.h"

#include <utility>

#include "sema_analyzer.h"
#include "sema_pack.h"

// 14.8.2, the whole of it this milestone reads.
//
// Every entry point below ends the same way: a map from each place of the head
// to what it was deduced to, handed to `arguments_of`, which turns it into the
// one flat argument list 14.4p1 makes a specialization out of.  What differs is
// only where the P/A pairs came from.

Deduction::Deduction(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

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
		const bool simple = derived && types.kind(inner) == TypeKind::Class &&
			types.is_specialization(inner);
		return match(inner, types.target(argument), bindings, simple, simple);
	}

	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		return match(types.target(pattern), types.target(argument), bindings);

	case TypeKind::Array:
		return types.bounded(pattern) == types.bounded(argument) &&
			types.bound(pattern) == types.bound(argument) &&
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
		return types.variadic(pattern) == types.variadic(argument) &&
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

	default:
		// A fundamental type or an enumeration holds no parameter to deduce,
		// so the two agree exactly when they are the same type.
		return pattern == argument;
	}
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
	if (places.size() != 1)
	{
		// 14.8.2.5p9: a pattern this milestone deduces names one pack.
		return false;
	}
	std::vector<TypeId> run;
	run.reserve(given.size() - from);
	for (std::size_t at = from; at < given.size(); ++at)
	{
		// Each element is a P/A pair of its own, over bindings of its own: what
		// the place took there is one element of the run, and what it took in
		// another element is a different one.
		std::unordered_map<TypeId, TypeId> one;
		if (!match(inner, given[at], one))
		{
			return false;
		}
		const std::unordered_map<TypeId, TypeId>::const_iterator took =
			one.find(places[0]);
		if (took == one.end())
		{
			return false;
		}
		run.push_back(took->second);
	}
	// 14.8.2.5p2 over a run: two lists that deduce one pack differently deduce
	// nothing, exactly as two arguments at one place do.
	const TypeId bound = types.pack_type(run);
	const std::pair<std::unordered_map<TypeId, TypeId>::iterator, bool> held =
		bindings.insert(std::make_pair(places[0], bound));
	return held.second || held.first->second == bound;
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

SemaEntity* Deduction::from_call(SemaEntity& primary,
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
	return match(expected, given, bindings, reference, true);
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
			analyzer_.bind_argument(*inner.scope, parameters[before]->name,
			                        took->second, SemaKind::Typedef);
		}
		std::unordered_map<TypeId, TypeId> memo;
		// 14.1p4: what the place *is* says how its default is read - a type
		// place's is 8.1p1's type-id and a value place's is 5.19's constant
		// expression, converted to the type the place declared.
		const TypeId place = analyzer_.substituted(
			types.parameter_value_type(parameters[index]->type), filled, memo);
		TypeId given;
		if (place == kNoType)
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

SemaEntity* Deduction::from_target(SemaEntity& primary, TypeId wanted)
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
