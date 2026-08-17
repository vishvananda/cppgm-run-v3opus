#include "sema_specialize.h"

#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_deduce.h"
#include "sema_name.h"
#include "sema_template.h"
#include "sema_template_head.h"

// 14.5.5's partial specialization and 14.5.1p1's variable template.
//
// Both are heads that write a declaration the primary template's own three
// steps - record the pattern, bind an argument list, read the pattern once per
// list - cannot answer for.  A partial specialization writes a *pattern* where
// the primary wrote parameters, so the step it changes is the middle one: an
// argument list no longer reaches the primary's body directly but is first
// matched against every pattern beside it.  A variable template writes an
// object where the primary wrote a class, so the step it changes is the last:
// what one argument list makes of it is a constant rather than a type.
//
// Nothing here re-reads the primary's syntax and nothing scans the argument
// lists already made: the choice is asked once per template and list, over the
// interned number a naming already holds, and a template no head partially
// specialized answers it with one test of an empty vector.

namespace
{

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

// 14.5.1p1: the one init-declarator a variable template's declaration writes.
// A head parameterises one declaration, so a simple-declaration that declared
// two objects declares no template of either.
const AstNode* sole_declarator(const AstNode& declared)
{
	if (declared.kind != AstKind::SimpleDeclaration || declared.children.empty())
	{
		return nullptr;
	}
	const AstNode* const list = child_of(declared, AstKind::InitDeclaratorList);
	if (list == nullptr || list->children.size() != 1 ||
	    list->children[0]->children.empty() ||
	    list->children[0]->children[0]->kind != AstKind::Declarator)
	{
		return nullptr;
	}
	return list->children[0];
}

// 14.5.1p1: one argument list of one variable template, held for as long as its
// initializer is being read and taken off however that reading ends.
class ReadingList
{
public:
	ReadingList(std::vector<std::uint32_t>& held, std::uint32_t list)
		: held_(held)
	{
		held_.push_back(list);
	}

	~ReadingList()
	{
		held_.pop_back();
	}

private:
	ReadingList(const ReadingList&);
	ReadingList& operator=(const ReadingList&);

	std::vector<std::uint32_t>& held_;
};

}

Specialization::Specialization(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

Specialization::Reading::Reading()
	: declared(nullptr)
	, region(nullptr)
{}

bool Specialization::record(const AstNode& clause, const AstNode& declared,
                            const SemaContext& ctx)
{
	const bool of_a_class = declared.kind == AstKind::ClassSpecifier ||
		declared.kind == AstKind::ClassForwardDeclaration;
	const AstNode* const init =
		of_a_class ? nullptr : sole_declarator(declared);
	if (!of_a_class && init == nullptr)
	{
		return false;
	}
	const AstNode* const written =
		of_a_class ? nullptr : SemaAnalyzer::declarator_id(*init->children[0]);
	const std::string spelling =
		of_a_class ? declared.text
		           : (written == nullptr ? std::string() : written->text);
	const QualifiedName name(spelling);
	if (spelling.empty())
	{
		return false;
	}
	const TemplateId id(name.last());
	if (!id.valid())
	{
		// 14.5.1p1: a head over a declarator-id that is a plain name declares
		// the template itself, which is a variable template wherever what it
		// parameterises is an object.  14.5.1.3p1's out-of-class member
		// definition writes a nested-name-specifier and declares no template,
		// which the walk that records members already took.
		return !of_a_class && !name.qualified() &&
			declare_variable(clause, declared, *init, spelling, ctx);
	}
	// 9.4.2p1 and 3.4.1p8: a declarator-id with a nested-name-specifier names
	// the template the region that name reaches already declared, and 14.1p2's
	// own parameter names stand over that region while the pattern is read.
	SemaContext target = ctx;
	if (name.qualified())
	{
		target.scope = analyzer_.resolve_prefix(name, ctx);
		if (target.scope == nullptr)
		{
			return false;
		}
		target.dump = target.scope->dump;
	}
	const LookupKind wanted =
		of_a_class ? LookupKind::Type : LookupKind::Any;
	SemaEntity* const primary =
		name.qualified()
			? analyzer_.model_.lookup_in(*target.scope, id.name(), wanted)
			: analyzer_.model_.lookup(*ctx.scope, id.name(), wanted);
	// 14.5.5p1: what the pattern specializes is the template the declarator-id
	// names, and a class-head-name names a class template where a declarator
	// names 14.5.1p1's object.  A function template is neither: 14.5.5p1 gives
	// no meaning to a partial specialization of one, so the declaration is left
	// to the walk that reads an ordinary one.
	if (primary == nullptr || primary->templated == nullptr ||
	    primary->kind != (of_a_class ? SemaKind::Class : SemaKind::Variable))
	{
		return false;
	}
	TemplateInfo* head = nullptr;
	std::vector<TypeId> pattern;
	if (!read_pattern(*primary, id, clause, target, head, pattern))
	{
		// 14.5.5p1: the declaration is a second body an argument list may be
		// read from, and this milestone could not read which lists those are -
		// so leaving it out is not leaving it unsaid.  Every list would then be
		// read from the primary's body, which is a different program: what
		// cannot be read is what cannot be instantiated, and 14.3p1's gate on
		// the primary is where every naming of it already asks.
		primary->templated->supported = false;
		return true;
	}
	hold_pattern(*primary->templated, *head, pattern, declared);
	return true;
}

// 14.5.5p1: the partial specialization, held beside the primary under the
// pattern it wrote.
//
// 14.5.5p2 leaves one pattern one declaration however many times it is written,
// so a definition that follows a declaration of the same pattern replaces what
// that declaration left rather than standing beside it - which is what makes
// `template<class A> struct s<A *>;` and its later body one specialization.
void Specialization::hold_pattern(TemplateInfo& info, TemplateInfo& head,
                                  std::vector<TypeId>& pattern,
                                  const AstNode& declared)
{
	// 14.5.5p2 with 14.1p2: each head declares places of its own, so two
	// declarations of one pattern write it over different types - and what says
	// they are the same pattern is 14.5.6.1p5's signature, the pattern with each
	// place standing for its position.
	const std::uint32_t signature = canonical_pattern(head, pattern);
	for (std::size_t index = 0; index < info.partials.size(); ++index)
	{
		if (info.partials[index].signature != signature)
		{
			continue;
		}
		if (declared.kind == AstKind::ClassForwardDeclaration)
		{
			// 9.2p2: a declaration that follows the definition leaves the
			// definition standing.
			return;
		}
		if (info.partials[index].body != nullptr &&
		    info.partials[index].body->kind == declared.kind)
		{
			// 3.2p1: one pattern has one definition, whether the body is a class
			// body or the initializer of a variable template.
			throw std::runtime_error("one partial specialization of a template "
			                         "is defined twice");
		}
		info.partials[index].head = &head;
		info.partials[index].pattern.swap(pattern);
		info.partials[index].body = &declared;
		return;
	}
	info.partials.push_back(TemplateInfo::Partial());
	info.partials.back().head = &head;
	info.partials.back().pattern.swap(pattern);
	info.partials.back().body = &declared;
	info.partials.back().signature = signature;
	// 14.5.5.1p1: a pattern written after a list was already answered for is a
	// pattern that list never saw, so what was answered is dropped rather than
	// kept - which is one clear at a declaration and no scan at a use.
	info.chosen.clear();
}

// 14.5.6.1p5: the pattern with each place its head declared standing for the
// position it was declared in, which is the one spelling two heads that wrote
// the same pattern share.
std::uint32_t Specialization::canonical_pattern(const TemplateInfo& head,
                                                const std::vector<TypeId>& pattern)
{
	std::unordered_map<TypeId, TypeId> bindings;
	for (std::size_t at = 0; at < head.parameters.size(); ++at)
	{
		bindings.insert(std::make_pair(
			head.parameters[at].self,
			analyzer_.canonical_parameter(at, head.parameters[at].pack)));
	}
	std::vector<TypeId> canonical;
	canonical.reserve(pattern.size());
	std::unordered_map<TypeId, TypeId> memo;
	for (std::size_t at = 0; at < pattern.size(); ++at)
	{
		canonical.push_back(analyzer_.substituted(pattern[at], bindings, memo));
	}
	return analyzer_.types_.type_list(canonical);
}

bool Specialization::read_pattern(SemaEntity& primary, const TemplateId& id,
                                  const AstNode& clause, const SemaContext& ctx,
                                  TemplateInfo*& head,
                                  std::vector<TypeId>& pattern)
{
	analyzer_.template_patterns_.push_back(TemplateInfo());
	head = &analyzer_.template_patterns_.back();
	head->region = ctx.scope;
	head->dump = ctx.dump;
	TemplateHead(analyzer_).read(clause, *head, false);
	if (!head->supported)
	{
		return false;
	}
	// 14.6.1p1: the pattern names the places *this* head declared, so it is read
	// in the region that head opened rather than in the one the primary's did.
	TemplateHead(analyzer_).open_region(*head);
	SemaContext inner;
	inner.scope = head->parameter_region;
	inner.dump = head->reading_dump;
	inner.node = nullptr;
	const unsigned stood = analyzer_.stood_in_;
	try
	{
		TemplateHead(analyzer_).bind_arguments(primary, id.arguments(), inner,
		                                  pattern);
	}
	catch (const std::exception&)
	{
		// 14p1: a pattern this milestone cannot read is not a declaration it
		// may quietly drop - what its caller does instead is leave the template
		// one no argument list is answered for, exactly as a head it gives no
		// meaning to does.  14.6p8's count is of the stand-ins a *reading* made,
		// and this one was thrown away.
		analyzer_.stood_in_ = stood;
		pattern.clear();
		return false;
	}
	return true;
}

bool Specialization::record_explicit(const AstNode& declared,
                                     const SemaContext& ctx)
{
	const AstNode* const init = sole_declarator(declared);
	const AstNode* const written =
		init == nullptr ? nullptr : SemaAnalyzer::declarator_id(*init->children[0]);
	if (written == nullptr || child_of(*init, AstKind::Initializer) == nullptr)
	{
		return false;
	}
	const QualifiedName name(written->text);
	const TemplateId id(name.last());
	if (name.qualified() || !id.valid())
	{
		return false;
	}
	SemaEntity* const primary =
		analyzer_.model_.lookup(*ctx.scope, id.name(), LookupKind::Any);
	if (primary == nullptr || primary->kind != SemaKind::Variable ||
	    primary->templated == nullptr)
	{
		return false;
	}
	std::vector<TypeId> arguments;
	TemplateHead(analyzer_).bind_arguments(*primary, id.arguments(), ctx, arguments);
	// 14.7.3p6: the declaration is what this argument list *is*, wherever it is
	// named - so it is held before the specialization is made, and a naming
	// above it that already made one from the pattern is an error the program
	// wrote rather than a reading this milestone repairs.
	primary->templated->explicit_variables[analyzer_.types_.type_list(arguments)] =
		&declared;
	return true;
}

// 14.5.1p1: the variable template itself, which declares a name in the region
// its head stands in and no object anywhere.
//
// The declaration is a pattern like a class template's: nothing is read from it
// until an argument list arrives, and what is read then is one init-declarator
// against a region binding the places.  The type its own decl-specifier-seq
// writes is read once here, in 14.6.1p1's region, because that is what says the
// head parameterises an object rather than a function and what a diagnostic
// names when the object is outside the supported subset.
bool Specialization::declare_variable(const AstNode& clause,
                                      const AstNode& declared,
                                      const AstNode& init,
                                      const std::string& name,
                                      const SemaContext& ctx)
{
	if (child_of(init, AstKind::Initializer) == nullptr)
	{
		return false;
	}
	SemaEntity* held = analyzer_.model_.find(*ctx.scope, name, LookupKind::Any);
	if (held != nullptr)
	{
		if (held->kind != SemaKind::Variable || held->templated == nullptr)
		{
			return false;
		}
		// 14p1: a second head over one name declares the same template, and the
		// one that wrote an initializer is what an instantiation reads.
		held->templated->pattern = &declared;
		return true;
	}
	analyzer_.template_patterns_.push_back(TemplateInfo());
	TemplateInfo& info = analyzer_.template_patterns_.back();
	info.region = ctx.scope;
	info.dump = ctx.dump;
	info.pattern = &declared;
	TemplateHead(analyzer_).read(clause, info);
	if (!info.supported)
	{
		return false;
	}
	TemplateHead(analyzer_).open_region(info);
	SemaContext inner;
	inner.scope = info.parameter_region;
	inner.dump = info.reading_dump;
	inner.node = nullptr;
	SemaEntity& entity = analyzer_.model_.create(
		SemaKind::Variable, name, declared_type(declared, init, inner));
	entity.templated = &info;
	entity.region = ctx.scope;
	analyzer_.model_.bind(*ctx.scope, name, entity);
	analyzer_.model_.declare_in(*ctx.scope, entity);
	return true;
}

// 8p1: the type one init-declarator of a simple-declaration declares, read in
// `ctx`.  A variable template's is read twice - once where the head stands, for
// the declaration, and once per argument list, because the places may be what
// the type names.
TypeId Specialization::declared_type(const AstNode& declared,
                                     const AstNode& init,
                                     const SemaContext& ctx)
{
	SemaSpan span;
	span.begin = declared.begin;
	span.end = declared.end;
	const TypeId base = analyzer_.specifier_type(analyzer_.read_specifiers(
		*declared.children[0], ctx, span, true, std::string()));
	std::string ignored;
	return analyzer_.declarator_type(*init.children[0], base, ctx, &ignored);
}

std::size_t Specialization::chosen(SemaEntity& primary,
                                   const std::vector<TypeId>& arguments,
                                   std::vector<TypeId>& deduced)
{
	TemplateInfo& info = *primary.templated;
	if (info.partials.empty())
	{
		return kNoPartial;
	}
	TypeTable& types = analyzer_.types_;
	const std::uint32_t list = types.type_list(arguments);
	const std::unordered_map<std::uint32_t, TemplateInfo::Chosen>::const_iterator
		held = info.chosen.find(list);
	if (held != info.chosen.end())
	{
		deduced = types.type_list_at(held->second.arguments);
		return held->second.at;
	}
	std::vector<std::size_t> matched;
	std::vector<std::vector<TypeId> > took;
	for (std::size_t index = 0; index < info.partials.size(); ++index)
	{
		std::vector<TypeId> one;
		if (!matches(info, index, arguments, one))
		{
			continue;
		}
		matched.push_back(index);
		took.push_back(std::vector<TypeId>());
		took.back().swap(one);
	}
	TemplateInfo::Chosen answer;
	answer.at = kNoPartial;
	answer.arguments = 0;
	if (!matched.empty())
	{
		const std::size_t best = most_specialized(info, primary.name, matched);
		answer.at = matched[best];
		answer.arguments = types.type_list(took[best]);
		deduced.swap(took[best]);
	}
	info.chosen.insert(std::make_pair(list, answer));
	return answer.at;
}

// 14.5.5.1p1 and 14.8.2.5p4: whether the pattern at `index` matches `arguments`,
// and what the places its own head declared were deduced to.
//
// 14.5.5p8.3 makes every place of that head deducible from its own pattern, so a
// place the match left unbound is a declaration no argument list could ever fill
// rather than a list this pattern happens not to take.
bool Specialization::matches(const TemplateInfo& info, std::size_t index,
                            const std::vector<TypeId>& arguments,
                            std::vector<TypeId>& deduced)
{
	const TemplateInfo::Partial& partial = info.partials[index];
	std::unordered_map<TypeId, TypeId> bindings;
	if (!Deduction(analyzer_).match_arguments(partial.pattern, arguments,
	                                          bindings))
	{
		return false;
	}
	const std::vector<TemplateInfo::Parameter>& places =
		partial.head->parameters;
	deduced.reserve(places.size());
	for (std::size_t at = 0; at < places.size(); ++at)
	{
		const std::unordered_map<TypeId, TypeId>::const_iterator bound =
			bindings.find(places[at].self);
		if (bound == bindings.end())
		{
			// 14.5.5p8.3: the pattern matched, and a place its own head
			// declared is still empty - so there is a body this list would be
			// read from and no arguments to read it against.  Answering from
			// the primary instead would be a different program read silently,
			// which is what the declaration being ill-formed rules out.
			throw std::runtime_error("a partial specialization declares the "
			                         "template parameter " + places[at].name +
			                         ", which its argument pattern does not "
			                         "deduce");
		}
		if (!places[at].pack || at + 1 != places.size())
		{
			// 14.5.5p1: a pack that is not the last place its head declared
			// stands as the one run it took, because the places after it are
			// places of their own and a flat list could not say where the run
			// ended.
			deduced.push_back(bound->second);
			continue;
		}
		// 14.5.3p1: what a pack place took is a run, and the list a reading of
		// the pattern is opened over is one flat list - so the run is written
		// out where 14.1p11 leaves it, which is at the end.
		const std::vector<TypeId>& run =
			analyzer_.types_.pack_elements(bound->second);
		deduced.insert(deduced.end(), run.begin(), run.end());
	}
	return true;
}

// 14.5.5.2p1: the one of `matched` every other is at least as general as.  Two
// that neither is more specialized than leave the naming ambiguous, which is
// what 14.5.5.1p1 makes ill-formed.
std::size_t Specialization::most_specialized(const TemplateInfo& info,
                                             const std::string& name,
                                             const std::vector<std::size_t>& matched)
{
	std::size_t best = 0;
	for (std::size_t index = 1; index < matched.size(); ++index)
	{
		if (at_least_as_specialized(info, matched[index], matched[best]) &&
		    !at_least_as_specialized(info, matched[best], matched[index]))
		{
			best = index;
		}
	}
	for (std::size_t index = 0; index < matched.size(); ++index)
	{
		if (index == best ||
		    (at_least_as_specialized(info, matched[best], matched[index]) &&
		     !at_least_as_specialized(info, matched[index], matched[best])))
		{
			continue;
		}
		throw std::runtime_error("a template-argument-list of " + name +
		                         " matches two partial specializations and "
		                         "neither is more specialized than the other");
	}
	return best;
}

bool Specialization::at_least_as_specialized(const TemplateInfo& info,
                                             std::size_t left, std::size_t right)
{
	// 14.5.5.2p1 rewrites both as function templates and asks 14.5.6.2 which
	// deduces from which, which over one argument list against another is the
	// match 14.8.2.5p4 already reads: the general pattern is the one that takes
	// the specialized pattern as its arguments.
	std::unordered_map<TypeId, TypeId> bindings;
	return Deduction(analyzer_).match_arguments(info.partials[right].pattern,
	                                            info.partials[left].pattern,
	                                            bindings);
}

SemaEntity& Specialization::variable(SemaEntity& primary,
                                     const std::vector<TypeId>& arguments,
                                     const SemaContext& ctx)
{
	const std::uint32_t list = analyzer_.types_.type_list(arguments);
	SemaEntity* const made =
		analyzer_.model_.specialization_of(primary, list);
	if (made != nullptr)
	{
		// 14.7.1p1: one declaration per template and argument list, so a second
		// naming reads nothing again.
		return *made;
	}
	static_cast<void>(ctx);
	// 5.19p2 with 14.7.1p1: what this specialization *is* is the constant its
	// initializer evaluates to, so there is nothing to hold until the reading is
	// over - and a naming of this same list reached from inside that reading is
	// asking the reading for its own answer.  A class does not need this: it is
	// held before its body is read, so the naming inside finds it incomplete.
	TemplateInfo& info = *primary.templated;
	for (std::size_t at = 0; at < info.reading.size(); ++at)
	{
		if (info.reading[at] != list)
		{
			continue;
		}
		throw std::runtime_error("the initializer of a specialization of " +
		                         primary.name + " names that specialization");
	}
	const ReadingList held(info.reading, list);
	return read_variable(primary, variable_reading(primary, arguments, list),
	                     arguments, list);
}

Specialization::Reading Specialization::variable_reading(
	SemaEntity& primary, const std::vector<TypeId>& arguments,
	std::uint32_t list)
{
	TemplateInfo& info = *primary.templated;
	Reading out;
	const std::unordered_map<std::uint32_t, const AstNode*>::const_iterator
		written = info.explicit_variables.find(list);
	if (written != info.explicit_variables.end())
	{
		// 14.7.3p1: the declaration was written with the arguments spelled out,
		// so it is read against no bindings at all.
		out.declared = written->second;
		out.region = info.region;
		return out;
	}
	std::vector<TypeId> deduced;
	const std::size_t at = chosen(primary, arguments, deduced);
	if (at == kNoPartial)
	{
		out.declared = info.pattern;
		out.region = &TemplateHead(analyzer_).open_bindings(info, arguments);
		return out;
	}
	out.declared = info.partials[at].body;
	out.region =
		&TemplateHead(analyzer_).open_bindings(*info.partials[at].head, deduced);
	return out;
}

// 14.5.1p1 with 5.19: what one argument list makes of a variable template.
//
// The declaration the reading chose is read once, against the region binding
// what that declaration's own head took: the type its declarator makes, and the
// constant its initializer evaluates to.  Nothing declares an object - a
// specialization of a variable template is reached where 5.19 asks for a
// constant, and this milestone's subset is the one an integral constant
// expression can be read out of, so what the reading leaves is the value.
SemaEntity& Specialization::read_variable(SemaEntity& primary,
                                          const Reading& reading,
                                          const std::vector<TypeId>& arguments,
                                          std::uint32_t list)
{
	const AstNode* const init =
		reading.declared == nullptr ? nullptr : sole_declarator(*reading.declared);
	const AstNode* const value =
		init == nullptr ? nullptr : child_of(*init, AstKind::Initializer);
	if (value == nullptr || value->children.empty())
	{
		throw std::runtime_error(primary.name + " is a variable template no "
		                         "declaration of which wrote an initializer");
	}
	SemaContext inner;
	inner.scope = reading.region;
	inner.dump = primary.templated->dump;
	inner.node = nullptr;
	const TypeId type = declared_type(*reading.declared, *init, inner);
	if (analyzer_.integral_type(type) == kNoType)
	{
		throw std::runtime_error("a variable template of " +
		                         analyzer_.types_.description(type) +
		                         " is outside the PA20 subset");
	}
	const SemaConstant folded =
		analyzer_.convert(analyzer_.evaluate(*value->children[0], inner), type);
	std::string spelled = primary.name + "<";
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (index != 0)
		{
			spelled += ", ";
		}
		spelled += analyzer_.type_spelling(arguments[index]);
	}
	spelled += ">";
	SemaEntity& made =
		analyzer_.model_.create(SemaKind::TemplateValue, spelled, type);
	made.primary = &primary;
	made.template_arguments = list;
	made.region = primary.templated->region;
	made.constant = true;
	made.value = folded.bits;
	analyzer_.model_.hold_specialization(primary, list, made);
	return made;
}
