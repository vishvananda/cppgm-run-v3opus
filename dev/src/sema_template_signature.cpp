#include "sema_template_signature.h"

#include "sema_analyzer.h"
#include "sema_deduce.h"

#include <utility>

// 14.5.6.1p5: when two template-declarations declare the same template.
//
// The clause is one comparison asked at four tiers - a redeclaration of a
// function template, a definition written out for one, 7.3.3p14's hiding of a
// base's member template by a derived class's, and 13.1's own index of what a
// region declares of one name - and each of them has two heads whose places are
// declarations of their own.  So the answer cannot be
// read off the types: `const K&` written under one head and `const K&` written
// under another are two types, and a place *neither* declarator mentioned
// stands in no type at all.  What settles both is one canonical form per
// declaration: the places stand for their positions, the type is substituted
// onto those stand-ins, and the stand-ins themselves stand at the end of it.
//
// `TemplateSignatures` is that vocabulary, and it lives beside the head because
// it is the head's own: one stand-in per position, made once and shared by
// every signature built from then on, and one built form per declaration held
// under its id so that a walk of a chain costs one comparison per link rather
// than one substitution.

TypeId TemplateSignatures::place(TypeTable& types, SemaModel& model,
                                 std::size_t index, bool pack)
{
	// 14.5.3p1: a place that binds a *run* is not the place that binds one
	// argument, so the two stand for different things - otherwise
	// `f(T)` and `f(Ts...)` are one signature, because the expansion over a
	// place standing for no pack is the place itself.
	std::vector<TypeId>& canonical = pack ? packs_ : places_;
	while (canonical.size() <= index)
	{
		// 14.5.6.1p5 asks whether two heads declared their parameters in the
		// same places, so a place is what a parameter stands for here: one type
		// per position, made once and shared by every signature.
		const TypeId made = types.template_parameter_type(
			model.type_entity_id(), false,
			(pack ? "#..." : "#") + std::to_string(canonical.size()));
		types.set_template_pack(made, pack);
		canonical.push_back(made);
	}
	return canonical[index];
}

TypeId TemplateSignatures::value_place(TypeTable& types, SemaModel& model,
                                       std::size_t index, bool pack, TypeId of)
{
	const std::uint64_t key = (static_cast<std::uint64_t>(index) << 33) |
		(static_cast<std::uint64_t>(pack ? 1u : 0u) << 32) | of;
	const std::pair<std::unordered_map<std::uint64_t, TypeId>::iterator, bool>
		held = values_.insert(std::make_pair(key, kNoType));
	if (held.second)
	{
		const TypeId made = types.template_parameter_type(
			model.type_entity_id(), false,
			(pack ? "#v..." : "#v") + std::to_string(index) + "_" +
			std::to_string(of));
		types.set_template_pack(made, pack);
		types.set_parameter_value_type(made, of);
		held.first->second = made;
	}
	return held.first->second;
}

TypeId& TemplateSignatures::built(std::uint32_t declaration, bool& held)
{
	const std::pair<std::unordered_map<std::uint32_t, TypeId>::iterator, bool>
		found = built_.insert(std::make_pair(declaration, kNoType));
	held = !found.second;
	return found.first->second;
}

TemplateSignature::TemplateSignature(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

TypeId TemplateSignature::of(const Scope& parameters, TypeId type)
{
	TypeTable& types = analyzer_.types_;
	SemaModel& model = analyzer_.model_;
	TemplateSignatures& held = analyzer_.signatures_;
	const std::vector<SemaEntity*>& declared = parameters.declarations;
	std::unordered_map<TypeId, TypeId> bindings;
	std::unordered_map<TypeId, TypeId> memo;
	std::vector<TypeId> shape;
	shape.reserve(declared.size());
	for (std::size_t index = 0; index < declared.size(); ++index)
	{
		const SemaEntity& place = *declared[index];
		if (place.kind != SemaKind::TemplateType &&
		    place.kind != SemaKind::TemplateValue)
		{
			// 14.1p1's remaining parameter binds a template rather than a type
			// or a value, and nothing this walk substitutes stands for one - so
			// a head that declares one is left declaring a template of its own.
			return kNoType;
		}
		const bool pack = types.is_template_pack(place.type);
		if (place.kind == SemaKind::TemplateType)
		{
			shape.push_back(held.place(types, model, index, pack));
			bindings.insert(std::make_pair(place.type, shape.back()));
			continue;
		}
		// 14.1p4: the type a value place binds a value of is written over the
		// places before it, so it is canonicalized with the bindings this walk
		// has made so far - which are exactly those places, and no place after
		// this one can stand in it.  That is what lets one memo serve every
		// substitution here and the whole type below.
		shape.push_back(held.value_place(
			types, model, index, pack,
			analyzer_.substituted(types.parameter_value_type(place.type),
			                      bindings, memo)));
		bindings.insert(std::make_pair(place.type, shape.back()));
	}
	const TypeId built = analyzer_.substituted(type, bindings, memo);
	if (types.kind(built) != TypeKind::Function)
	{
		return built;
	}
	// 14.5.6.1p5 asks about the two heads as much as about what was declared
	// over them, and a place neither declarator mentioned stands nowhere in
	// the type the substitution made - so `template<int N> f(int)` and
	// `template<class T> f(int)` would be one declaration read out of the type
	// alone.  The stand-ins themselves stand at the end of the parameter list
	// of the answer, where they are part of every comparison of it and of
	// 7.3.3p14's key alike: the list a declarator wrote is what precedes them.
	std::vector<TypeId> list(types.parameters(built));
	list.insert(list.end(), shape.begin(), shape.end());
	return types.ref_qualified_function(
		types.function_of(types.target(built), list, types.variadic(built)),
		types.function_ref_qualifier(built));
}

// 14.5.6.1p5 at 13.1's index: the type a declaration of one name is keyed by.
//
// 13.1 tells two declarations of one name apart by the parameter-type-list the
// declarator wrote, and every chain a region holds is indexed by it - which is
// one probe rather than a walk of the declarations already made.  A declaration
// written under a head wrote that list in places its own head declared, so the
// list alone says neither that two of them are one declaration nor that they
// are two: `template<int N> int e(int)` and `template<class T> int e(int)`
// wrote one list and declare two templates, and two declarations of one
// template wrote two lists.  The canonical form is what the index has to be
// keyed by for its one probe to answer either, and a head with no such form -
// one that declares a template-template parameter - keeps the type it was
// written with, which is what the declarations of it were already keyed by.
TypeId TemplateSignature::indexed(const Scope* head, TypeId type)
{
	if (head == nullptr)
	{
		return type;
	}
	const TypeId canonical = of(*head, type);
	return canonical == kNoType ? type : canonical;
}

// 7.3.3p14 hides a member function template of a base with a member function
// template of this class "with the same name, parameter-type-list,
// cv-qualification and ref-qualifier", and the two lists are written in places
// two different heads declared - `const K&` in the base and `const K&` in the
// derived class are two types.  A head that declares a template-template
// parameter has no canonical form, and the declaration's own type is then the
// key it keeps - which matches nothing else, so the two declarations overload
// as they did.
std::uint32_t TemplateSignature::hiding_key(const SemaEntity& function)
{
	if (function.template_parameters == nullptr)
	{
		return analyzer_.member_signature(function);
	}
	// One canonical form per declaration, held where 14.5.6.1p5's own
	// comparison already holds it: the class's declarations of one name are
	// walked three times over one using-declaration - once where it is
	// written and twice where 9.2p2 completes the class - and the
	// substitution that builds the form is what a walk would otherwise pay
	// for at every pass.
	bool built = false;
	TypeId canonical = analyzer_.signatures_.built(function.id, built);
	if (!built)
	{
		// The build makes places of its own, so the entry is written back
		// rather than filled through a reference the build could move.
		canonical = of(*function.template_parameters, function.type);
		analyzer_.signatures_.built(function.id, built) = canonical;
	}
	// A template and a non-template of one spelling hide nothing of each
	// other's: the stand-ins stand in no type a declarator wrote, so the two
	// keys differ wherever one head was read and the other was not.
	return canonical == kNoType
		? analyzer_.member_signature(function)
		: analyzer_.member_signature(canonical, function.object_member);
}

SemaEntity* TemplateSignature::equivalent(SemaEntity& head, Scope& parameters,
                                          TypeId type)
{
	const TypeId wanted = of(parameters, type);
	if (wanted == kNoType)
	{
		return nullptr;
	}
	const std::size_t arity = parameters.declarations.size();
	for (SemaEntity* at = &head; at != nullptr; at = at->next)
	{
		if (at->template_parameters == nullptr ||
		    at->template_parameters == &parameters ||
		    at->template_parameters->declarations.size() != arity ||
		    at->shadowed != nullptr)
		{
			// 7.3.3p14: a declaration a using-declaration brought into this
			// class is one this class's own declaration *hides* rather than
			// one 14.5.6.1p5 makes it a redeclaration of - which is why
			// `declare_using_member` leaves it out of 13.1's index too.  The
			// two orders the program may write them in reach this the same
			// way: the hiding is settled where 9.2p2 completes the class, and
			// until then the brought-in declaration stands beside the class's
			// own without either being a declaration of the other.
			continue;
		}
		bool built = false;
		TypeId signature = analyzer_.signatures_.built(at->id, built);
		if (!built)
		{
			// The build makes places of its own, so the entry is written back
			// rather than filled through a reference the build could move.
			signature = of(*at->template_parameters, at->type);
			analyzer_.signatures_.built(at->id, built) = signature;
		}
		if (signature == wanted)
		{
			return at;
		}
	}
	return nullptr;
}

// 14.5.6.2 [temp.func.order]: which of two function templates a call that
// deduced both is a specialization of.
//
// It stands beside 14.5.6.1p5 rather than beside 13.3's ranking because it is
// the same question read one step further: p5 asks when two heads declare *one*
// template, and this asks, of two that declare different ones, which of them a
// deduction from the other's places still succeeds against.  Both are answered
// over the places a declarator wrote and not over the types a use built.

namespace
{

// 14.5.6.2p2 and 13.3.1p4: the parameter types `entity` is ordered against
// `other` by, which are the places its own declarator wrote wherever the two
// declarations declare the same kind of function.
//
// 9.3.1p3 put a non-static member's object parameter in its type as a pointer
// to the class, and it is the one place the two lists can part company.
// 14.5.6.2p2 is what lines them up: where only one of the two is a non-static
// member, that one is considered to have a first parameter of "reference to cv
// A" - which is the place the other declaration wrote its own first operand in,
// and is what lets 13.5p6's member operator be ordered against the non-member
// beside it.  9.4p1's static member of the same class is the exception: 13.3.1p4
// gives it an implicit object parameter that matches any object, so it tells the
// two apart by nothing and the object parameter is dropped from both.
//
// The list the declaration wrote is what most pairs are ordered by, so
// `adjusted` is filled only where 14.5.6.2p2 has something to change.
const std::vector<TypeId>& ordering_parameters(TypeTable& types,
                                               const SemaEntity& entity,
                                               const SemaEntity& other,
                                               std::size_t limit,
                                               std::vector<TypeId>& adjusted)
{
	if (limit == kResultPlace)
	{
		// 14.8.2.4p3's second bullet: 12.3.2p1 wrote the conversion-type-id
		// where a declarator writes a return type, so the one type these two
		// are ordered over is what each hands back - their parameter lists are
		// the object alone and tell them apart by nothing.
		adjusted.assign(1, types.target(entity.type));
		return adjusted;
	}
	const std::vector<TypeId>& written = types.parameters(entity.type);
	if (!entity.object_member || other.object_member || written.empty())
	{
		return written;
	}
	adjusted.assign(written.begin(), written.end());
	if (entity.region != nullptr && entity.region == other.region &&
	    entity.region->kind == ScopeKind::Class)
	{
		adjusted.erase(adjusted.begin());
	}
	else
	{
		adjusted[0] = types.reference_to(types.target(adjusted[0]), false);
	}
	return adjusted;
}

// 14.8.2.4p3's first bullet: in the context of a function call the types used
// are the parameter types the call wrote arguments for, and p3's footnote says
// a default argument is no argument here - so a declaration with more places
// than the call filled is ordered over the ones it filled and no further.
//
// The places both lists start at are the same, because `ordering_parameters`
// has already put the object parameter of a non-static member either in the
// place the other declaration wrote its first operand in or out of both lists,
// so `limit` counts from the front of each.
//
// 14.8.2.4p12: a trailing run is a place of its own however few arguments it
// took, so the limit never cuts one off.  That is the whole of what leaves
// `dispatch(C &, S, T &&)` more specialized than `dispatch(C &, S, T &&,
// R &&...)` for a call of three: cut off, the run's own place is never reached
// and p8's first sentence - an A a run was transformed from against a P that is
// not one - has nothing to fail on.
std::size_t ordering_places(TypeTable& types,
                            const std::vector<TypeId>& written,
                            std::size_t limit)
{
	const std::size_t used = written.size() < limit ? written.size() : limit;
	if (used < written.size() &&
	    types.is_pack_expansion(written[written.size() - 1]))
	{
		return written.size();
	}
	return used;
}

}

// 14.5.6.2p2 and p8: whether the parameter types `right` was written over are
// every one of them deduced from the type `left` wrote in the same place, with
// `left`'s own parameters standing for types of their own.
//
// 14.5.6.2p5 and p7 are what make `const T &` and `T` comparable at all: a
// reference on either side is replaced by what it refers to and the top-level
// qualifiers of both are dropped, so what is left is the shape each template
// wrote.  One binding map runs the whole list, because a parameter that two
// places deduce differently makes neither template the other's.
//
// 14.8.2.4p9 with 14.5.3p4: a trailing `P...` stands for every place the ones
// before it did not take, so each of those is a pair of its own - and a place
// the *other* template wrote as a run is no argument for one this template
// wrote singly, which is what leaves `f(T)` more specialized than `f(Ts...)`.
bool SemaAnalyzer::at_least_as_specialized(SemaEntity& left, SemaEntity& right,
                                           std::size_t limit)
{
	const std::uint64_t key =
		(static_cast<std::uint64_t>(left.id) << 32) | right.id;
	OrderingAnswers& answers = specialization_order_[key];
	for (std::size_t at = 0; at < answers.size(); ++at)
	{
		if (answers[at].first == limit)
		{
			return answers[at].second;
		}
	}
	std::vector<TypeId> adjusted_left;
	std::vector<TypeId> adjusted_right;
	const std::vector<TypeId>& wrote =
		ordering_parameters(types_, left, right, limit, adjusted_left);
	const std::vector<TypeId>& against =
		ordering_parameters(types_, right, left, limit, adjusted_right);
	const std::size_t used = ordering_places(types_, wrote, limit);
	const std::size_t asked = ordering_places(types_, against, limit);
	const bool packed = asked == against.size() && !against.empty() &&
		types_.is_pack_expansion(against[against.size() - 1]);
	const std::size_t fixed = packed ? asked - 1 : asked;
	bool answer = packed ? used >= fixed : used == asked;
	std::unordered_map<TypeId, TypeId> bindings;
	for (std::size_t index = 0; answer && index < used; ++index)
	{
		const bool run = index >= fixed;
		TypeId pattern =
			run ? types_.target(against[against.size() - 1]) : against[index];
		TypeId argument = wrote[index];
		if (types_.is_pack_expansion(argument))
		{
			if (!run)
			{
				answer = false;
				break;
			}
			argument = types_.target(argument);
		}
		if (types_.is_reference(pattern))
		{
			pattern = types_.target(pattern);
		}
		if (types_.is_reference(argument))
		{
			argument = types_.target(argument);
		}
		if (!run)
		{
			answer = Deduction(*this).match(types_.strip_cv(pattern),
			                                types_.strip_cv(argument), bindings);
			continue;
		}
		// Each place a run stands for is a pair over bindings of its own, the
		// way 14.8.2.1p1's run is: what the pack took at one of them says
		// nothing about what it took at another.
		std::unordered_map<TypeId, TypeId> one(bindings);
		answer = Deduction(*this).match(types_.strip_cv(pattern),
		                                types_.strip_cv(argument), one);
	}
	specialization_order_[key].push_back(std::make_pair(limit, answer));
	return answer;
}

// 14.5.6.2p9: where the deduction of one place succeeded in both directions the
// two templates wrote types that are identical once 14.5.6.2p5 and p7 have
// taken the references and the qualifiers off them - so what is left to tell
// them apart is what those took: an lvalue reference is more specialized than
// what is not one, and a reference to a more qualified type is more specialized
// than a reference to a less qualified one.  14.5.6.2p10 wants one template
// ahead at some place and behind at none, so a place answering each way leaves
// neither.
int SemaAnalyzer::reference_order(SemaEntity& left, SemaEntity& right,
                                  std::size_t limit)
{
	std::vector<TypeId> adjusted_left;
	std::vector<TypeId> adjusted_right;
	const std::vector<TypeId>& wrote =
		ordering_parameters(types_, left, right, limit, adjusted_left);
	const std::vector<TypeId>& against =
		ordering_parameters(types_, right, left, limit, adjusted_right);
	const std::vector<TypeId>& shorter =
		wrote.size() < against.size() ? wrote : against;
	const std::size_t places =
		shorter.size() < limit ? shorter.size() : limit;
	int order = 0;
	for (std::size_t index = 0; index < places; ++index)
	{
		if (!types_.is_reference(wrote[index]) ||
		    !types_.is_reference(against[index]))
		{
			continue;
		}
		const bool lvalue =
			types_.kind(wrote[index]) == TypeKind::LValueReference;
		const bool other =
			types_.kind(against[index]) == TypeKind::LValueReference;
		const unsigned held = types_.cv(types_.target(wrote[index]));
		const unsigned theirs = types_.cv(types_.target(against[index]));
		int here = 0;
		if (lvalue != other)
		{
			here = lvalue ? 1 : -1;
		}
		else if (held != theirs)
		{
			here = (theirs & ~held) == 0 ? 1 : ((held & ~theirs) == 0 ? -1 : 0);
		}
		if (here == 0)
		{
			continue;
		}
		if (order != 0 && order != here)
		{
			return 0;
		}
		order = here;
	}
	return order;
}

// 14.5.6.2p4: `left` is more specialized than `right` when it is at least as
// specialized and `right` is not.  Which places the two are compared at is
// `ordering_parameters`' answer, so a pair whose lists name different places -
// a member against a non-member that wrote no operand for the object - is left
// unordered by the deduction failing rather than by a question asked here.
bool SemaAnalyzer::more_specialized(SemaEntity& left, SemaEntity& right,
                                    std::size_t limit)
{
	if (&left == &right || !at_least_as_specialized(left, right, limit))
	{
		return false;
	}
	return !at_least_as_specialized(right, left, limit) ||
		reference_order(left, right, limit) > 0;
}
