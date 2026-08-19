#include "sema_specialize.h"

#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_deduce.h"
#include "sema_name.h"
#include "sema_pack.h"
#include "sema_reading.h"
#include "sema_template.h"
#include "sema_template_head.h"

// 14.5.5's partial specialization, 14.5.1p1's variable template and 14.5.7p1's
// alias template.
//
// All three are heads that write a declaration the primary template's own three
// steps - record the pattern, bind an argument list, read the pattern once per
// list - cannot answer for.  A partial specialization writes a *pattern* where
// the primary wrote parameters, so the step it changes is the middle one: an
// argument list no longer reaches the primary's body directly but is first
// matched against every pattern beside it.  A variable template writes an
// object where the primary wrote a class, so the step it changes is the last:
// what one argument list makes of it is a constant rather than a type.  An
// alias template writes a type-id there, so the step it changes is the last one
// too: what an argument list makes of it is a type that already exists.
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
		info.partials[index].visible = analyzer_.model_.written_bound();
		return;
	}
	info.partials.push_back(TemplateInfo::Partial());
	info.partials.back().head = &head;
	info.partials.back().pattern.swap(pattern);
	info.partials.back().body = &declared;
	info.partials.back().visible = analyzer_.model_.written_bound();
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
			analyzer_.signatures_.place(analyzer_.types_, analyzer_.model_, at,
			                            head.parameters[at].pack)));
	}
	std::vector<TypeId> canonical;
	canonical.reserve(pattern.size());
	std::unordered_map<TypeId, TypeId> memo;
	for (std::size_t at = 0; at < pattern.size(); ++at)
	{
		// 14.5.3p4: an entry of the list may be an expansion, and a substitution
		// reaches *through* one rather than past it - `substituted` leaves an
		// expansion standing as it was, which would keep this head's own pack
		// place in the canonical list and make two declarations of one pattern
		// two patterns.  The stand-ins bind no run, so the reading appends the
		// one entry `#...n` the two declarations then share.
		PackReading(analyzer_).substitute_entry(pattern[at], bindings, memo,
		                                        canonical);
	}
	return analyzer_.types_.type_list(canonical);
}

// 14.5.5p1 with 14.5.1.3p1: which body an out-of-class member definition was
// written over.
//
// `template<class K, class V> int Map<Pair<K, V>, tag>::get()` declares a member
// of the class `template<class K, class V> struct Map<Pair<K, V>, tag>` declares
// and of nothing else - so what says which body it belongs to is the argument
// *pattern* its declarator-id wrote, read exactly as the pattern of a partial
// specialization's own declaration is: in a region binding the places this
// definition's own head declared.  14.1p2 lets the two heads spell those places
// differently, so what is compared is 14.5.6.1p5's signature and not the types.
//
// A template no head partially specialized pays one test of an empty vector; the
// reading below costs one head and one argument list per definition written over
// a template that has patterns, which is what the declaration of the pattern
// itself already cost.
std::size_t Specialization::member_pattern(SemaEntity& primary,
                                           const std::string& wrote,
                                           const AstNode& clause,
                                           const SemaContext& ctx)
{
	const TemplateInfo& info = *primary.templated;
	const TemplateId id(wrote);
	if (info.partials.empty() || !id.valid())
	{
		return kNoPartial;
	}
	// 14.6p8: this reading answers which declaration the definition belongs to
	// and nothing else, so it asks for no definition of what it names - the
	// specializations its arguments write are declarations like any a pattern
	// names.
	const DialectReading dialect(analyzer_);
	TemplateInfo* head = nullptr;
	std::vector<TypeId> pattern;
	if (!read_pattern(primary, id, clause, ctx, head, pattern))
	{
		// 14.5.5p1: a pattern this milestone cannot read names no partial
		// specialization it recorded either, so the definition is left to the
		// primary the way every unreadable head already is.
		return kNoPartial;
	}
	const std::uint32_t signature = canonical_pattern(*head, pattern);
	for (std::size_t at = 0; at < info.partials.size(); ++at)
	{
		if (info.partials[at].signature == signature)
		{
			return at;
		}
	}
	return kNoPartial;
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

// 14.7.3p11 with 14.8.2.6: which template a `template<>` declaration is a
// specialization of, where one written argument list fits an overload set.
//
// The list makes a specialization of every declaration of the name whose head
// it fits, and 14.8.2.6p1 says the declaration's own *type* is what tells those
// apart: this head specializes the one whose function type is the type its
// declarator wrote.  It is the same match 14.7.2p2 asks of an explicit
// instantiation, asked here of candidates the argument list already settled -
// so nothing is deduced, the type is read once, and the walk is one comparison
// per declaration of the name.
//
// A refusal from that reading is no answer about the head: the ordinary walk
// reads the same declaration next and makes the same refusal where it stands,
// so the reading is rolled back to no candidate rather than ending the program.
SemaEntity* Specialization::explicit_target(const AstNode& declared,
                                            const AstNode& declarator,
                                            const std::string& written,
                                            const SemaContext& ctx,
                                            const std::vector<SemaEntity*>& found)
{
	if (declared.children.empty())
	{
		return nullptr;
	}
	const QualifiedName spelled(written);
	SemaSpan span;
	span.begin = declared.begin;
	span.end = declared.end;
	TypeId declared_type = kNoType;
	TypeId member_type = kNoType;
	try
	{
		const Naming naming(analyzer_, analyzer_.naming_context(written, ctx));
		const DeclSpecifiers specifiers = analyzer_.read_specifiers(
			*declared.children[0], ctx, span, true, written);
		// 3.4.1p8: the rest of a declarator whose declarator-id is qualified is
		// read in the region that name reaches, which for a member of a class is
		// the class its nested-name-specifier names.
		SemaContext reached = ctx;
		if (spelled.qualified())
		{
			reached.scope = analyzer_.resolve_prefix(spelled, ctx);
			reached.dump = reached.scope->dump;
		}
		std::string ignored;
		declared_type = analyzer_.declarator_type(
			declarator, analyzer_.specifier_type(specifiers),
			spelled.qualified() ? reached : ctx, &ignored, nullptr,
			declares_object_member(specifiers));
		// 9.3.1p3: the object a member function is called on is no part of what
		// its declarator wrote and is part of the type its declaration has, so
		// both spellings are built here and each candidate is asked with the one
		// it carries.
		member_type =
			analyzer_.types_.kind(declared_type) == TypeKind::Function
				? analyzer_.with_object_parameter(declared_type, declarator,
				                                  reached, specifiers.is_static,
				                                  spelled.last(),
				                                  spelled.qualified())
				: declared_type;
	}
	catch (const std::runtime_error&)
	{
		return nullptr;
	}
	std::vector<SemaEntity*> candidates;
	// 14.7.3p11: how many declarations of the name this head could be a
	// specialization of at all, which is what says a type that fits none of them
	// is a program refused rather than a declaration this clause says nothing
	// about.
	std::size_t fits = 0;
	if (found.empty())
	{
		// 14.8.2.6p1: the head wrote no argument list, so the whole of it is what
		// the type deduces - one declaration of the name at a time, which is
		// 14.8.2.2's walk asked with the target the declarator wrote.
		fits = gather_deduced(written, declared_type, member_type, ctx, spelled,
		                      candidates);
	}
	else
	{
		// 14.8.1p2: an entry the list settled outright is a specialization whose
		// type is all there is left to compare, and one it left a place of is a
		// candidate 14.8.2.2 deduces the rest of - which is the same pair of arms
		// 14.7.2p2's explicit instantiation is matched through.
		for (std::size_t index = 0; index < found.size(); ++index)
		{
			SemaEntity& at = *found[index];
			const TypeId wanted =
				at.object_member ? member_type : declared_type;
			if (at.partial_of == nullptr &&
			    (at.primary == nullptr || at.primary->templated == nullptr))
			{
				continue;
			}
			++fits;
			SemaEntity* const one =
				at.partial_of != nullptr
					? Deduction(analyzer_).from_target(at, wanted)
					: (at.type == wanted ? &at : nullptr);
			if (one != nullptr)
			{
				candidates.push_back(one);
			}
		}
	}
	SemaEntity* chosen = nullptr;
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		SemaEntity& one = *candidates[index];
		if (one.primary == nullptr || one.primary->templated == nullptr ||
		    one.type != (one.object_member ? member_type : declared_type))
		{
			continue;
		}
		if (chosen == nullptr || chosen == &one)
		{
			chosen = &one;
			continue;
		}
		// 14.8.2.6p1: where the type fits two of them, 14.5.6.2's ordering is
		// what leaves one, and a pair it leaves unordered is 14.7.3p11's
		// "exactly one template" the program did not write.
		if (analyzer_.more_specialized(one, *chosen))
		{
			chosen = &one;
		}
		else if (!analyzer_.more_specialized(*chosen, one))
		{
			return nullptr;
		}
	}
	if (chosen == nullptr && fits > 0)
	{
		// 14.7.3p11: a template of this name could have been what the head
		// specialized and the type the declarator wrote is none of theirs, so
		// there is no template this is a specialization of - which is a program
		// refused rather than a declaration the ordinary walk reads as anything
		// else.
		throw std::runtime_error(
			"the explicit specialization " + written +
			" writes a type no template of that name is declared with");
	}
	return chosen;
}

// 14.8.2.6p1 where the head wrote no argument list: the specialization each
// declaration of the name makes of the type this declaration wrote, and how many
// declarations of it 14.7.3p11 could have been asked about at all.
//
// 3.4.1p8's walk reaches the declarations the name is bound to, and each of them
// that a head parameterises is asked for the arguments 14.8.2.2 deduces from the
// target - which is the same door 14.7.2p2's explicit instantiation goes
// through, and which discards a candidate whose substitution is ill formed
// rather than refusing the program.
std::size_t Specialization::gather_deduced(const std::string& written,
                                           TypeId declared_type,
                                           TypeId member_type,
                                           const SemaContext& ctx,
                                           const QualifiedName& spelled,
                                           std::vector<SemaEntity*>& out)
{
	SemaContext reached = ctx;
	if (spelled.qualified())
	{
		reached.scope = analyzer_.resolve_prefix(spelled, ctx);
		reached.dump = reached.scope->dump;
	}
	SemaEntity* const first =
		spelled.qualified()
			? analyzer_.model_.lookup_in(*reached.scope, spelled.last(),
			                             LookupKind::Any)
			: analyzer_.resolve(written, ctx, LookupKind::Any);
	std::size_t fits = 0;
	for (SemaEntity* at = first; at != nullptr; at = at->next)
	{
		if (at->kind != SemaKind::Function || at->template_parameters == nullptr ||
		    at->templated == nullptr)
		{
			continue;
		}
		++fits;
		SemaEntity* const one = Deduction(analyzer_).from_target(
			*at, at->object_member ? member_type : declared_type);
		if (one != nullptr)
		{
			out.push_back(one);
		}
	}
	return fits;
}

// 14.7.3p1 read in the source order the clause says nothing about: the program
// wrote its own definition of this member out for exactly these arguments
// *above* the template's own, so 14.7.1p1's reading of the pattern is not made
// for it at all.
//
// `supersede` and `note_object` are the same clause the other way round - a
// written definition arriving below the reading takes the claim away from it -
// and which definition the unit holds is what 14.7.3p1 is about rather than
// which of the two the program wrote first.  `PatternReading::record` reads a
// member definition again for every specialization already made, so the two
// orders differ only in whether the `template<>` stands above that definition
// or below it, and neither of them is 3.2p1's second definition.
//
// A member of a class is the only declaration this can be true of: nothing but
// an instantiation or a `template<>` defines one, and 11.3p6's friend a class
// template's body defines is declared in a namespace - where a second
// instantiation is still the redefinition 14.5.4p1 leaves it as, and where a
// definition the program wrote at namespace scope is still one 3.2p1 refuses a
// second of.
bool Specialization::holds_written_definition(const SemaEntity& member) const
{
	return analyzer_.instantiating_pattern_ > 0 &&
		!member.instantiated_definition && member.region != nullptr &&
		member.region->kind == ScopeKind::Class &&
		(member.defined || member.object_definition);
}

// 14.7.3p1: the definition this declaration held was 14.7.1p1's reading of the
// pattern, and the program has now written its own out for these arguments.
//
// So the body that reading put aside is no definition of anything: what it was
// read from is a pattern this argument list is no longer read from at all.  The
// mark on the declaration is what says so, and the queued entry carries the
// matching one - so this drops the one entry still reachable by name and leaves
// the end of the unit to skip whatever is already in its list, which costs one
// flag test per definition written there rather than a walk of the queue.
void Specialization::supersede(SemaEntity& function)
{
	function.instantiated_definition = false;
	analyzer_.held_definitions_.erase(function.id);
	if (function.templated != nullptr)
	{
		// 14.5.2p1 and 14.7.3p1: the declaration is a member *template*, and what
		// it held was the pattern the class pattern's own reading gave it - so the
		// definition arriving now is the pattern every specialization of this
		// member is read from, and `record_function_template` writes it where that
		// reading wrote the other.  A specialization already read from the old one
		// is 14.7.3p6's use written above the definition, which the clause leaves
		// undiagnosed.
		function.templated->pattern = nullptr;
	}
}

// 14.7.3p1 with 5.19p2: what 9.4.2p2's definition of a static data member of a
// class template specialization leaves a use of the name to read.
//
// `instantiated` says which definition this is: one 14.7.1p1 read from the
// template's own, or one the program wrote out for exactly these arguments.  The
// second is 14.7.3p1's, and it makes *which definition an argument list is read
// from* a question the program answers rather than the template - so a use of a
// specialization the pattern was read for reads the object 9.4.2p2's definition
// laid out and not the value that reading folded.  The one the program wrote is
// this unit's own definition, exactly as `supersede` leaves a function's.
//
// 5.19p2 is untouched by any of that: what a specialization's member is worth is
// what its own definition initialized it with, so `code<char>::value` is the
// integral constant expression it was before the program wrote a `template<>`
// for `code<int>::value`, and an array bound and a template argument each read it.
//
// The template is reached from the class the member belongs to, and the walk is
// over the specializations that class's template already has - once per
// `template<>` definition of a member, and nothing at all for the definitions of
// a template no such declaration was written for.
void Specialization::note_object(SemaEntity& member, bool instantiated)
{
	Scope* const region = member.region;
	if (region == nullptr || region->kind != ScopeKind::Class ||
	    region->owner == nullptr || region->owner->primary == nullptr ||
	    region->owner->primary->templated == nullptr)
	{
		return;
	}
	TemplateInfo& info = *region->owner->primary->templated;
	if (instantiated)
	{
		// 14.7.1p1: this is the template's definition read again for one argument
		// list, so a use of it is a read of the object while any other list has a
		// definition the program wrote.
		if (info.explicit_members.find(member.name) != info.explicit_members.end())
		{
			member.member_specialized = true;
		}
		return;
	}
	// 14.6.1p1 with 14.5.1.3p1: the template's *own* out-of-class definition
	// declares into the current instantiation of whichever body it was written
	// over, which is the class the head's own places make - so it is the one
	// definition every specialization is read from and not one argument list's.
	const std::unordered_map<std::uint32_t, std::size_t>::const_iterator wrote =
		info.patterns.find(region->owner->template_arguments);
	if (region->owner == info.current ||
	    (wrote != info.patterns.end() &&
	     info.partials[wrote->second].current == region->owner))
	{
		return;
	}
	// 14.7.3p1 at the object tier: the definition this declaration held was
	// 14.7.1p1's reading of the pattern, and the program has now written its own
	// out for exactly these arguments - so what the reading laid out is no
	// definition of anything and this unit's own is.  It is `supersede`'s first
	// line for a function, asked here of the storage rather than of a body: a
	// definition every unit that needs one may hold becomes one this unit owes
	// the program wherever the program reaches it or not.
	//
	// A body the reading held is dropped by name; a line it already wrote into
	// the dump is dropped by taking its claim to define anything away, which
	// leaves the definition below it the only line that lays this storage out.
	//
	// The claim taken away is 14.7.1p1's reading's alone, which is what the mark
	// says: a line another definition the *program* wrote laid the storage out
	// with is 3.2p1's second definition and no line this clause may drop.
	if (member.instantiated_definition)
	{
		member.instantiated_definition = false;
		const std::unordered_map<std::uint32_t, DumpNode*>::const_iterator
			wrote_line = analyzer_.object_definitions_.find(member.id);
		if (wrote_line != analyzer_.object_definitions_.end())
		{
			wrote_line->second->fact.object_definition = false;
		}
	}
	if (!info.explicit_members.insert(member.name).second)
	{
		return;
	}
	for (std::size_t index = 0; index < info.specializations.size(); ++index)
	{
		SemaEntity* const made = info.specializations[index];
		SemaEntity* const held = made->scope == nullptr
			? nullptr
			: analyzer_.model_.find(*made->scope, member.name, LookupKind::Any);
		if (held != nullptr && held != &member && held->instantiated_definition)
		{
			held->member_specialized = true;
		}
	}
}


// 3.2p1 with 14.7.3p1: a second definition of one special member, and the one
// kind of second definition the clause allows.
//
// 12's entry points are read exactly as `declare_function` reads every other
// member: an instantiation of the class reads the pattern's body for them, and
// what that body defines is the definition this unit holds only until the program
// writes one out for these arguments.  `template<> tag<int>::~tag() {}` is that
// definition, so it replaces the instantiated one; two written ones, and two
// instantiated, are still the redefinition 3.2p1 refuses.
//
// False where the definition arriving is the pattern's own read again *below*
// the one the program wrote out, which `holds_written_definition` is: 14.7.3p1
// leaves that reading unmade, so the caller reads no body rather than refusing
// the program.
bool Specialization::require_replaceable(SemaEntity& member,
                                         const std::string& spelled)
{
	if (holds_written_definition(member))
	{
		return false;
	}
	if (!member.instantiated_definition || analyzer_.instantiating_pattern_ > 0)
	{
		throw std::runtime_error(spelled + " is defined twice");
	}
	supersede(member);
	member.defined = false;
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
		held->templated->visible = analyzer_.model_.written_bound();
		return true;
	}
	analyzer_.template_patterns_.push_back(TemplateInfo());
	TemplateInfo& info = analyzer_.template_patterns_.back();
	info.region = ctx.scope;
	info.dump = ctx.dump;
	info.pattern = &declared;
	info.visible = analyzer_.model_.written_bound();
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

// 7.1.3p2 and 14.5.7p1: the alias template itself, which declares a name in the
// region its head stands in and no type anywhere.
//
// 7.1.3p2 makes the name a *template-name* and not a typedef-name: what it
// names is settled only once an argument list arrives, so nothing of the type-id
// is read here.  The declaration is bound as a type-name all the same, because
// that is the lookup a naming of it makes - `X<A…>` asks 3.4 for `X` before it
// has an argument list to give it - and 7.1.3p3's leniency about a second
// declaration of one typedef-name is what a header included twice needs.
bool Specialization::record_alias(const AstNode& clause,
                                  const AstNode& declared,
                                  const SemaContext& ctx)
{
	if (declared.kind != AstKind::AliasDeclaration ||
	    child_of(declared, AstKind::TypeId) == nullptr)
	{
		return false;
	}
	analyzer_.template_patterns_.push_back(TemplateInfo());
	TemplateInfo& info = analyzer_.template_patterns_.back();
	info.region = ctx.scope;
	info.dump = ctx.dump;
	info.pattern = &declared;
	info.visible = analyzer_.model_.written_bound();
	TemplateHead(analyzer_).read(clause, info);
	if (!info.supported)
	{
		// 14p1: a head this milestone gives no meaning to leaves the template
		// undeclared, exactly as it does over a class - what a naming of it then
		// gets is the refusal every unsupported head already earns.
		return false;
	}
	SemaEntity& entity = analyzer_.declare_type_alias(declared.text, kNoType,
	                                                  *ctx.scope);
	entity.templated = &info;
	entity.region = ctx.scope;
	return true;
}

// 7.1.3p2: the type a template-id over an alias template names.
//
// 7.1.3p2 makes an alias-declaration "another name for" the type its type-id
// wrote, so a template-id over one declares nothing of its own: it *is* the type
// the arguments substitute into the pattern, which is why 14.5.7p2 leaves two
// namings of one alias with one argument list one type and why 14.5.7p1 gives an
// alias template no specializations to write.  So the reading gives back the
// type the type-id already interned, and the declaration made here is a
// typedef-name of it - one per template and interned argument list, so an alias
// named n times is read once.
SemaEntity& Specialization::alias(SemaEntity& primary, const TemplateId& id,
                                  const SemaContext& ctx)
{
	std::vector<TypeId> arguments;
	TemplateHead(analyzer_).bind_arguments(primary, id.arguments(), ctx,
	                                       arguments);
	return alias_arguments(primary, arguments);
}

// 14.2p4: one component written after a prefix no argument list has settled,
// which the `template` keyword says is a template-id.
//
// The keyword is gone by the time the component reaches here - `part` strips it
// where every reader already splits - so what says the component is a
// template-id is the argument list it wrote, exactly as it does for a name
// written with no prefix at all.  The list is read *where the reading stands*,
// because that is the only region its arguments name anything in: they are the
// enclosing template's own places, and a substitution later builds them the way
// it builds every other argument.
//
// 14.1p4 has no head to ask which kind each argument is - the member template
// is a member of a class no argument list has named - so what is left is the
// spelling: a type-id is read as a type and anything else is 5.19's expression,
// which is the same order `SpelledTypeId` settles 5.4p2's ambiguity in.  The
// second reading is made only where the first ran out, and a spelling that is
// no type-id runs out on its first word.
SemaEntity& Specialization::member_component(TypeId prefix,
                                             const std::string& written,
                                             const SemaContext& ctx)
{
	const TemplateId id(written);
	if (!id.valid())
	{
		return analyzer_.dependent_member_name(prefix, written, nullptr);
	}
	std::vector<TypeId> arguments;
	arguments.reserve(id.arguments().size());
	for (std::size_t index = 0; index < id.arguments().size(); ++index)
	{
		std::string pattern;
		if (written_pack_expansion(id.arguments()[index], pattern))
		{
			// 14.5.3p4: a run the enclosing head bound is one argument per
			// element here as it is in every other list.
			PackReading(analyzer_).expand(pattern, ctx, kNoType, arguments);
			continue;
		}
		TypeId built = kNoType;
		try
		{
			const ReadingDepth probing(analyzer_.checking_);
			built = analyzer_.template_argument_type(id.arguments()[index], ctx);
		}
		catch (const std::exception&)
		{
			built = analyzer_.template_argument_value(id.arguments()[index],
			                                          kNoType, ctx);
		}
		arguments.push_back(built);
	}
	return analyzer_.dependent_member_name(prefix, id.name(), &arguments);
}

SemaEntity& Specialization::alias_arguments(
	SemaEntity& primary, const std::vector<TypeId>& arguments)
{
	const std::uint32_t list = analyzer_.types_.type_list(arguments);
	SemaEntity* const made = analyzer_.model_.specialization_of(primary, list);
	if (made != nullptr)
	{
		named_specialization(made->type);
		return *made;
	}
	TemplateInfo& info = *primary.templated;
	for (std::size_t at = 0; at < info.reading.size(); ++at)
	{
		// 14.5.7p1 with 3.2p1: the type-id is read in place of the name, so an
		// alias whose own type-id names this same argument list is a type
		// defined in terms of itself and there is nothing to hold in its place.
		if (info.reading[at] == list)
		{
			throw std::runtime_error("the type-id of a specialization of " +
			                         primary.name + " names that same "
			                         "specialization");
		}
	}
	SemaContext inner;
	inner.scope = &TemplateHead(analyzer_).open_bindings(info, arguments);
	inner.dump = info.dump;
	inner.node = nullptr;
	TypeId type = kNoType;
	{
		const ReadingList held(info.reading, list);
		// 14.6.4.2p1: the type-id is read here, wherever the naming stands, and
		// was written where the alias was - so what its names reach is what
		// stood there and not what stands at whichever reading arrived.
		const ReadingBound written_here(analyzer_.model_, info.visible);
		type = analyzer_.type_id_type(*child_of(*info.pattern, AstKind::TypeId),
		                              inner);
	}
	const TypeId discarded = discarded_arguments(primary, arguments, type);
	SemaEntity& entity = analyzer_.model_.create(
		SemaKind::Typedef, spelled(primary, arguments),
		discarded == kNoType ? type : discarded);
	entity.region = info.region;
	// 11p1 and 14.5.7p1: the alias template is the declaration a class gave an
	// access to, and this typedef-name is only the type one argument list makes
	// of it - so the access a qualified naming is refused by is the template's.
	entity.access = primary.access;
	analyzer_.model_.hold_specialization(primary, list, entity);
	named_specialization(entity.type);
	return entity;
}

void Specialization::named_specialization(TypeId type)
{
	if (analyzer_.checking_ != 0)
	{
		// 14.6p8: a name written in a template definition read where it stands
		// is no use of anything, which is the answer `instantiate_class` gives a
		// template-id written there too.
		return;
	}
	TypeTable& types = analyzer_.types_;
	TypeId bare = types.strip_cv(type);
	while (types.kind(bare) == TypeKind::Array)
	{
		// 3.9p5: an array of an incomplete class is one too, which is the walk
		// `require_complete_type` makes of the demand this answers.
		bare = types.strip_cv(types.target(bare));
	}
	if (types.kind(bare) != TypeKind::Class)
	{
		return;
	}
	SemaEntity* const owner = analyzer_.model_.type_owner(bare);
	if (owner != nullptr && owner->primary != nullptr)
	{
		analyzer_.asked_specialization(*owner);
	}
}

TypeId Specialization::substituted_alias(
	TypeId naming, const std::unordered_map<TypeId, TypeId>& bindings,
	std::unordered_map<TypeId, TypeId>& memo)
{
	TypeTable& types = analyzer_.types_;
	SemaEntity& alias = *const_cast<SemaEntity*>(types.alias_template(naming));
	const std::vector<TypeId> listed = types.template_arguments(naming);
	std::vector<TypeId> arguments;
	arguments.reserve(listed.size());
	for (std::size_t index = 0; index < listed.size(); ++index)
	{
		PackReading(analyzer_).substitute_entry(listed[index], bindings, memo,
		                                        arguments);
	}
	return alias_arguments(alias, arguments).type;
}

// 14.7.1p1: whether a substitution *builds* this argument or looks it up.
//
// `substituted`'s own answer, asked ahead of it.  A place a head declared is
// looked up: whatever list arrives binds it to a type that has already been
// built, so the lookup cannot refuse and rebuilding the naming around it can
// come to nothing new.  Every other dependent argument is one a reading makes
// again - `typename T::x` looks a member up, `S<T>` instantiates, `T *` derives,
// a decltype-specifier reads an expression a second time, a naming that
// discarded an argument of its own builds that one - and each of those is a
// reading 14.8.2p8 has something to fire on.  An expansion is its pattern read
// once per element and a settled run is its elements, so both are asked of what
// they hold.
bool Specialization::rebuilt(TypeId argument) const
{
	TypeTable& types = analyzer_.types_;
	const TypeId bare = types.strip_cv(argument);
	if (types.is_pack_expansion(bare))
	{
		return rebuilt(types.target(bare));
	}
	if (types.is_settled_run(bare))
	{
		const std::vector<TypeId>& held = types.pack_elements(bare);
		for (std::size_t index = 0; index < held.size(); ++index)
		{
			if (rebuilt(held[index]))
			{
				return true;
			}
		}
		return false;
	}
	if (!types.is_dependent(bare))
	{
		// An argument every list has settled was built where it was written.
		return false;
	}
	if (types.kind(bare) != TypeKind::TemplateParameter)
	{
		return true;
	}
	// The four readings a parameter-kind entry stands for, which are the four
	// arms `substituted` takes before its own lookup.
	return types.dependent_owner(bare) != kNoType ||
		types.applied_template(bare) != kNoType ||
		types.alias_named(bare) != kNoType ||
		analyzer_.dependent_.written.count(bare) != 0;
}

// 7.1.3p2 with 14.8.2p8: an argument the type-id does not name is still an
// argument, and building it is still what a substitution may fail at.
//
// The type-id is read here, wherever the naming stands, so a *settled*
// argument has already been built by the time this is asked - `void_t<int>` is
// `void`, and nothing about `int` is left to go wrong.  An argument a
// substitution builds again is the other case: it is built where 14.7.1p1 puts
// a list behind it, and a type-id that does not mention it gives that
// substitution nothing to build.  So the naming keeps one entry standing for
// itself, exactly as a dependent member of a prefix does, and what the
// substitution finds under it is the list to build before the type-id is read
// again.  `void_t<typename T::x>` is the whole of the detected idiom: a `T`
// with no `x` is a substitution that fails and a candidate 13.3 drops, and
// collapsing the naming to `void` where it stands leaves nothing to fail.
//
// 14.5.7p1 is the other half and bounds it: a template-id over an alias
// template *is* the associated type, so the entry is a second type standing for
// one the program can also write out - and two declarations of one template,
// one writing `void_t<T>` and one writing `void`, stop being declarations of
// one.  So the entry is kept only where an argument is one a substitution
// builds: `void_t<T>` and `void_t<Ts...>` name nothing that can go wrong and
// collapse where they stand, and what is left holding an entry is the readings
// SFINAE exists for.
//
// What it costs is one walk of the type the type-id named per naming with such
// an argument, which is the walk `substituted` would make of it anyway.
TypeId Specialization::discarded_arguments(SemaEntity& primary,
                                           const std::vector<TypeId>& arguments,
                                           TypeId type)
{
	TypeTable& types = analyzer_.types_;
	bool discards = false;
	for (std::size_t index = 0; index < arguments.size() && !discards; ++index)
	{
		discards = rebuilt(arguments[index]) &&
			!types.mentions(type, arguments[index]);
	}
	if (!discards)
	{
		return kNoType;
	}
	return types.discarded_alias_type(primary, type, arguments,
	                                  spelled(primary, arguments));
}

std::string Specialization::spelled(const SemaEntity& primary,
                                    const std::vector<TypeId>& arguments)
{
	std::string out = primary.name + "<";
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (index != 0)
		{
			out += ", ";
		}
		out += analyzer_.type_spelling(arguments[index]);
	}
	return out + ">";
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

// 14.7.1p1's cycle, held for as long as one choice is being made.
//
// The mark has to come off however the choice ends, because a pattern read
// against the list may refuse - which is 14.8.2p8's candidate discarded and no
// answer about this template at all - so a later naming of the same list is
// entitled to make the choice again.
class Choosing
{
public:
	Choosing(TemplateInfo& info, std::uint32_t list)
		: info_(info)
		, list_(list)
		, entered_(info.choosing.insert(list).second)
	{}

	~Choosing()
	{
		if (entered_)
		{
			info_.choosing.erase(list_);
		}
	}

	bool entered() const { return entered_; }

private:
	Choosing(const Choosing&);
	Choosing& operator=(const Choosing&);

	TemplateInfo& info_;
	std::uint32_t list_;
	bool entered_;
};

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
	// 14.7.1p1: the choice is made once per list, so a request for one whose
	// choice is already being made is that choice asking for itself.  There is
	// no answer to wait for, and the reading that asked has to be told so:
	// 14.8.2p8 discards the candidate that named it wherever a substitution was
	// what asked, and the program is refused wherever none was.
	Choosing opened(info, list);
	if (!opened.entered())
	{
		throw std::runtime_error(primary.name +
		                         " names a specialization whose own patterns "
		                         "are being read for it");
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
// 14.8.2p8 at this match.  14.5.5.1p1 answers "which pattern does this list
// match" by 14.8.2's deduction, and 14.8.2p8's sentence about substitution
// applies to it word for word: `void_t<typename T::iterator_category>` is a
// pattern whose read-back names a member only some arguments' class declares,
// and a list whose class does not is a list this pattern *does not match*
// rather than a program to refuse.  The whole detector idiom is that sentence -
// the primary answers, and its `value` is `false`.
//
// So the match is an attempt and not a query: `match_arguments` and the
// read-back both run inside one `Substitution`, and a failure of either that
// 14.8.2p8 leaves in the immediate context discards this pattern alone.  What
// it costs is one try-block per pattern per argument list, which `chosen`
// memoises, and nothing at all until a pattern actually fails.
bool Specialization::matches(const TemplateInfo& info, std::size_t index,
                            const std::vector<TypeId>& arguments,
                            std::vector<TypeId>& deduced)
{
	const TemplateInfo::Partial& partial = info.partials[index];
	std::unordered_map<TypeId, TypeId> bindings;
	Substitution attempt(analyzer_);
	try
	{
		if (!Deduction(analyzer_).match_arguments(partial.pattern, arguments,
		                                          bindings) ||
		    !substitution_agrees(partial.pattern, bindings, arguments))
		{
			return false;
		}
	}
	catch (const std::runtime_error& why)
	{
		if (!attempt.discards(why))
		{
			throw;
		}
		return false;
	}
	return took_places(info, index, bindings, deduced);
}

// 14.5.5p8.3: what the places the pattern's own head declared were deduced to.
//
// That clause makes every one of them deducible from the pattern, so a place
// the match left unbound is a declaration no argument list could ever fill
// rather than a list this pattern happens not to take - which is why the walk
// stands *outside* the attempt above.  A defect in the partial specialization's
// own declaration is no part of the immediate context of the use that named it,
// so it refuses the program however many candidates were waiting on the answer.
bool Specialization::took_places(const TemplateInfo& info, std::size_t index,
                                 const std::unordered_map<TypeId, TypeId>& bindings,
                                 std::vector<TypeId>& deduced)
{
	const std::vector<TemplateInfo::Parameter>& places =
		info.partials[index].head->parameters;
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
			throw Instantiated("a partial specialization declares the "
			                   "template parameter " + places[at].name +
			                   ", which its argument pattern does not deduce");
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

// 14.8.2.5p5 at 14.5.5.1p1's match: the pattern read back with each place
// standing for what it was deduced to, which has to be the list itself.
//
// A pattern that writes a qualified-id over one of its own places - `typename
// Type::value_type` - names a *non-deduced context*: the pair says nothing
// about what the prefix is, so the match walks past it and every such pattern
// takes every list.  What makes the list one pattern's rather than another's is
// this second reading, where the prefix is settled and the member it names is
// looked up.  Two patterns that differ only in the member they name then take
// different lists rather than making every list ambiguous.
//
// A pattern that named no such context substitutes back to what the match
// already paired entry for entry, so this reading confirms what it found and
// finds nothing new; it costs one substitution per candidate, which the
// `chosen` memo pays once per argument list.
bool Specialization::substitution_agrees(
	const std::vector<TypeId>& pattern,
	const std::unordered_map<TypeId, TypeId>& bindings,
	const std::vector<TypeId>& arguments)
{
	std::vector<TypeId> read;
	read.reserve(pattern.size());
	std::unordered_map<TypeId, TypeId> memo;
	for (std::size_t at = 0; at < pattern.size(); ++at)
	{
		PackReading(analyzer_).substitute_entry(pattern[at], bindings, memo,
		                                        read);
		if (read.size() > arguments.size())
		{
			return false;
		}
	}
	if (read.size() != arguments.size())
	{
		return false;
	}
	for (std::size_t at = 0; at < read.size(); ++at)
	{
		if (read[at] == arguments[at])
		{
			continue;
		}
		// 14.6.2p1: a prefix this substitution could not settle leaves the
		// member standing as it was written, which says nothing either way -
		// the match is what paired the two and there is nothing here to
		// contradict it.
		if (analyzer_.types_.is_dependent(read[at]))
		{
			continue;
		}
		return false;
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
	for (std::size_t at = 0; at < arguments.size(); ++at)
	{
		if (!analyzer_.types_.is_dependent(arguments[at]))
		{
			continue;
		}
		// 14.6.2p2: a template-id over a variable template whose argument list
		// an outer head has yet to settle is a *value-dependent* expression,
		// and there is no initializer to read: `enabled<T>` under
		// `template<class T>` is worth whatever the arguments make of it, and
		// folding it to the primary's own `false` is a different program read
		// silently - `enable_if_t<enabled<T>, int>` at a non-type place then
		// refuses where 14.8.2p8 only drops the candidate.
		//
		// So the naming stands for itself: the declaration it gives back holds
		// no constant and has a type an argument list has yet to settle, which
		// is what every reader of a name already asks - 14.6.2p2's stand-in for
		// the *expression* is the one whichever reading wrote the naming makes,
		// keyed by the spelling and the place it fills, and that reading is
		// where the substitution comes back to.  Nothing is registered here, so
		// the two never race to say which place the value is converted to.
		//
		// It is held against this same interned list, so a spelling written n
		// times is one stand-in.
		const std::string name = spelled(primary, arguments);
		SemaEntity& stood = analyzer_.model_.create(
			SemaKind::Variable, name,
			analyzer_.types_.template_parameter_type(
				analyzer_.model_.type_entity_id(), false, name));
		stood.region = primary.templated->region;
		stood.access = primary.access;
		analyzer_.model_.hold_specialization(primary, list, stood);
		return stood;
	}
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
	SemaEntity& made = analyzer_.model_.create(
		SemaKind::TemplateValue, spelled(primary, arguments), type);
	made.primary = &primary;
	made.template_arguments = list;
	made.region = primary.templated->region;
	// 11p1: the access travels from the variable template onto the declaration
	// one argument list makes of it, exactly as it does over a class and over
	// 14.5.7p1's alias template.
	made.access = primary.access;
	made.constant = true;
	made.value = folded.bits;
	analyzer_.model_.hold_specialization(primary, list, made);
	return made;
}
