#include "sema_pattern.h"

#include <utility>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_name.h"
#include "sema_specialize.h"
#include "sema_template.h"
#include "sema_template_head.h"

// 14.6p8's reading of a template definition, and 14.6.1p1's class it is read as.
//
// The whole of this file is one idea repeated over the three places a template
// definition can be written: the class body, a pattern beside it, and a member
// defined outside either of them.  Each of the three is read once where it
// stands, against a class whose arguments are the places its own head declared
// standing for themselves - and then again for each argument list, against a
// class whose arguments are that list.  The first reading is what says the
// definition is well-formed for every list; the second is what makes a
// declaration this unit has.
//
// So what varies between the three is only *which* head declares the places and
// *which* body declares the class.  14.5.5p1's pattern is a second head and a
// second body beside the primary's, and 14.5.1.3p1's member definition writes a
// head of its own that stands for one of them - so the fact travelling with the
// member is which body it was written over, and every reading below takes it as
// an index into `TemplateInfo::partials` with `kNoPartial` for the primary.

PatternReading::PatternReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

std::vector<TypeId> PatternReading::places(const TemplateInfo& head)
{
	std::vector<TypeId> arguments;
	arguments.reserve(head.parameters.size());
	for (std::size_t index = 0; index < head.parameters.size(); ++index)
	{
		// 14.6.1p1: the current instantiation is what the head's own name over
		// its own parameters denotes, which for a pack place is the expansion
		// `Ts...` a definition has to write - so the argument list this class
		// is made from is the one that spelling reads back to.
		arguments.push_back(head.parameters[index].pack
			                    ? analyzer_.types_.pack_expansion(
				                      head.parameters[index].self)
			                    : head.parameters[index].self);
	}
	return arguments;
}

// 14.6.1p1: the current instantiation of a class template, which is the class
// its own definition declares.
//
// A class template's body names its own class - a member of it, a pointer to
// it, the injected-class-name 14.6.1p1 binds - and what it names there is the
// specialization over the template's own parameters.  So the parameters are
// bound to types standing for themselves, in a region of their own, and the
// class is the specialization those types make: `S<T>` inside `S`'s definition
// and `S<T>` written anywhere else in the template are then one declaration,
// found the way every other specialization is.
//
// The region and the class are made once and kept, because 14.5.1.3p1's
// out-of-class member definitions are read against the same two however long
// after the class body they were written.
SemaEntity& PatternReading::current(SemaEntity& primary)
{
	TemplateInfo& info = *primary.templated;
	if (info.current != nullptr)
	{
		return *info.current;
	}
	TemplateHead(analyzer_).open_region(info);
	// The declaration is made before the body is read, so that a name the body
	// writes for its own class finds it rather than starting a second reading.
	info.current = &analyzer_.instantiate_class(primary, places(info));
	return *info.current;
}

// 14.6.1p1 with 14.5.5p1: the current instantiation of one partial
// specialization, which is the class *its* body declares over *its* own places.
//
// A pattern is a template of its own - 14.5.5p8.3 makes every place its head
// declared deducible from the pattern it wrote - so the class its body declares
// is the specialization that pattern names: `A<T&>` inside `template<class T>
// struct A<T&>` and `A<T&>` written after it are one declaration, exactly as
// `S<T>` is one for the primary.  What tells the two apart is nothing about the
// class: it is the argument list, which is the primary's places for one and the
// pattern for the other.
//
// So the class is registered under that list before its body is read, and
// `complete_specialization` reads it from this pattern rather than from the
// primary's - which is what makes an out-of-class definition of a member of
// this pattern declare into the class the program named.
SemaEntity* PatternReading::current_pattern(SemaEntity& primary, std::size_t at)
{
	TemplateInfo& info = *primary.templated;
	TemplateInfo::Partial& partial = info.partials[at];
	if (partial.current != nullptr)
	{
		return partial.current;
	}
	if (partial.head == nullptr || partial.body == nullptr)
	{
		return nullptr;
	}
	const FunctionReading held(analyzer_, nullptr, kNoType);
	const DialectReading dialect(analyzer_);
	SemaEntity& made = analyzer_.instantiate_class(primary, partial.pattern);
	partial.current = &made;
	info.patterns.insert(std::make_pair(made.template_arguments, at));
	analyzer_.complete_specialization(made);
	return partial.current;
}

// 14.6p8: the reading a class template's definition gets where it stands.
//
// The body is read once, as the class 14.6.1p1 says it declares, in the PA11
// dialect: a type that depends on a parameter has no layout, no conversion and
// no overload set until an argument arrives, so what the definition can be
// asked is what its declarations say and 3.4p1's lookup of the names it writes.
// Nothing the reading leaves reaches the output - its lines stand in a dump
// nothing writes out, and 14.7.1p1 makes a template-id it names a declaration
// rather than a use requiring a definition.
void PatternReading::read_class(SemaEntity& primary)
{
	const TemplateInfo& info = *primary.templated;
	if (!info.supported || info.pattern == nullptr ||
	    info.pattern->kind != AstKind::ClassSpecifier)
	{
		// A head this milestone gives no meaning to declares a template no
		// specialization can be generated from, so 14.6p8 has nothing to read.
		return;
	}
	const FunctionReading held(analyzer_, nullptr, kNoType);
	const DialectReading dialect(analyzer_);
	analyzer_.complete_specialization(current(primary));
}

// 14.6p8 and 14.5.1.3p1: the reading an out-of-class member definition gets
// where it stands, which is against the same region and the same class the body
// it was written over was read against.
void PatternReading::read_member(SemaEntity& primary, const AstNode& clause,
                                 const AstNode& pattern, std::size_t at)
{
	const TemplateInfo& info = *primary.templated;
	const bool own = at == Specialization::kNoPartial;
	SemaEntity* const made = own ? info.current : current_pattern(primary, at);
	if (made == nullptr || !made->defined || made->scope == nullptr)
	{
		// 9.4.2p1 and 9.3p2: a definition outside the class declares into a
		// class the template has to have defined, and one whose definition this
		// milestone never read has no region to declare into.
		return;
	}
	const TemplateInfo& head = own ? info : *info.partials[at].head;
	// 14.5.1.3p1 and 14.1p2: the definition's own head declares parameters of
	// its own, which need not be spelled the way the head it stands for spelled
	// them - so the names this reading binds are the ones the definition wrote,
	// standing for the places that head declared.  They are bound as parameters
	// and not as typedef-names of them, because 14.6.1p6 refuses a declaration
	// of one anywhere in the template that declared it.
	Scope* const region = TemplateHead(analyzer_).open_member_parameters(
		*made->scope->parent, clause, places(head), SemaKind::TemplateType,
		head.reading_dump);
	if (region == nullptr)
	{
		return;
	}
	const EnclosedBy enclosed(made->scope, region);
	const FunctionReading held(analyzer_, nullptr, kNoType);
	const DialectReading dialect(analyzer_);
	SemaContext inner;
	inner.scope = region;
	inner.dump = head.reading_dump;
	inner.node = nullptr;
	const std::size_t mark = analyzer_.held_bodies_.size();
	read_declaration(pattern, inner);
	analyzer_.read_held_pattern_bodies(mark);
}

// 14.5.2p3: the declaration one out-of-class member definition writes, read in
// a region that already binds the class template's own head to its arguments.
//
// A member template writes a second head there, which parameterises the
// declaration exactly as any head parameterises the one under it - so what is
// left to do is open its region and read what it stands over.  The record was
// made where the whole nest was found, so this reading records nothing.
void PatternReading::read_declaration(const AstNode& node,
                                      const SemaContext& ctx)
{
	if (node.kind == AstKind::TemplateDeclaration)
	{
		// 14.5.2p1: what that head parameterises may be a member *class*
		// template, and 14.5.1.3p1 makes this its definition - so the class the
		// declarator-id names is one the enclosing class already declares and
		// the record is made against it, exactly as it is where the definition
		// stands inside the class body.  Every other declaration under a second
		// head is one 14.1p1's own reading takes.
		if (analyzer_.templating() && analyzer_.record_template(node, ctx, true))
		{
			return;
		}
		// 14.5.2p3: or it parameterises a member of a member template, whose
		// own places it declares - so the definition belongs to *that* template
		// and is read against the class its body declares.
		const AstNode* clause = nullptr;
		const AstNode* declared = nullptr;
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			const AstNode& child = *node.children[index];
			if (child.kind == AstKind::TemplateParameterClause)
			{
				clause = &child;
				continue;
			}
			declared = &child;
		}
		// 14.5.2p3: a member of a member of a member writes a head apiece, and
		// the declarator-id that says which template *this* one parameterises a
		// member of is the innermost declaration's - the heads after it travel
		// with the definition and are read where this one's arguments stand.
		const AstNode* innermost = declared;
		while (innermost != nullptr &&
		       innermost->kind == AstKind::TemplateDeclaration &&
		       !innermost->children.empty())
		{
			innermost = innermost->children[innermost->children.size() - 1];
		}
		SemaEntity* const nested =
			analyzer_.templating() && clause != nullptr && innermost != nullptr
				? nested_owner(*innermost, ctx) : nullptr;
		if (nested != nullptr)
		{
			record(*nested, Specialization::kNoPartial, *clause, *declared);
			return;
		}
		analyzer_.read_template_head(node, ctx);
		return;
	}
	analyzer_.declaration(node, ctx);
}

// 14.5.1.3p1: the name one out-of-class definition wrote, which is the
// class-head-name of a class, a constructor's own declarator-id, and the
// declarator-id of the one declarator every other such definition writes.
std::string PatternReading::member_spelling(const AstNode& node)
{
	if (node.kind == AstKind::ClassSpecifier ||
	    node.kind == AstKind::ClassForwardDeclaration ||
	    node.kind == AstKind::SpecialMemberDefinition ||
	    node.kind == AstKind::SpecialMemberDeclaration)
	{
		// 9.1p2 and 12.1p1: a class-head-name and a constructor's declarator-id
		// are the name the node itself carries.
		return node.text;
	}
	const AstNode* id = nullptr;
	if (node.kind == AstKind::FunctionDefinition && node.children.size() > 1)
	{
		id = SemaAnalyzer::declarator_id(*node.children[1]);
	}
	else
	{
		for (std::size_t index = 0; id == nullptr &&
		     index < node.children.size(); ++index)
		{
			const AstNode& child = *node.children[index];
			if (child.kind != AstKind::InitDeclaratorList)
			{
				continue;
			}
			for (std::size_t at = 0; id == nullptr &&
			     at < child.children.size(); ++at)
			{
				if (!child.children[at]->children.empty())
				{
					id = SemaAnalyzer::declarator_id(
						*child.children[at]->children[0]);
				}
			}
		}
	}
	return id == nullptr ? std::string() : id->text;
}

// 14.5.1.3p1: the class template a definition written outside its class is a
// member of.
//
// The declarator-id names it, and 14.5.1.3p1 makes the nested-name-specifier a
// template-id over the same parameters the head declared - so the owner is the
// template the first such component reaches.  Null where the declaration is a
// member of no template, which is every ordinary out-of-class definition.
//
// 14.5.5p1 leaves the template that component named holding several bodies, and
// which of them this definition is a member of is what the *arguments* it wrote
// say - so the component's own spelling travels back to the caller rather than
// being looked for a second time.
SemaEntity* PatternReading::owner(const AstNode& node, const SemaContext& ctx,
                                  std::string* wrote)
{
	const std::string spelling = member_spelling(node);
	if (spelling.empty())
	{
		return nullptr;
	}
	// 3.4.3p1: each component of the nested-name-specifier is looked up in the
	// region the one before it reached, and the first of them where the
	// declaration stands - so a class template written as `v::S<T>::` is found
	// wherever the namespace holding it is.
	const QualifiedName spelled(spelling);
	Scope* reached = nullptr;
	for (std::size_t index = 0; index + 1 < spelled.size(); ++index)
	{
		const TemplateId written(spelled.part(index));
		const std::string component =
			written.valid() ? written.name() : spelled.part(index);
		if (index == 0 && component.empty())
		{
			// 3.4.3p1: a name written `::x` names what the global namespace
			// declares, which is the region the component before it reached.
			reached = &analyzer_.model_.global();
			continue;
		}
		SemaEntity* const named =
			reached == nullptr
				? analyzer_.model_.lookup(*ctx.scope, component,
				                          LookupKind::Region)
				: analyzer_.model_.lookup_in(*reached, component,
				                             LookupKind::Region);
		if (named == nullptr)
		{
			return nullptr;
		}
		if (written.valid() && named->kind == SemaKind::Class &&
		    named->templated != nullptr)
		{
			if (wrote != nullptr)
			{
				*wrote = spelled.part(index);
			}
			return named;
		}
		// 3.4.3p1 again: a component may name a class through a typedef-name of
		// it, which is a declaration with no region of its own - so the region
		// walked into is `region_of`'s answer, exactly as it is for the same
		// prefix read by `resolve_prefix`.
		reached = analyzer_.model_.region_of(*named);
		if (reached == nullptr)
		{
			return nullptr;
		}
	}
	return nullptr;
}

// 14.5.2p3 with 14.5.1.3p1: the member template a *second* head's declarator-id
// declares a member of.
//
// `template<class T> template<class U> void outer<T>::inner<U>::call()` writes
// one head per class the member is nested in, and a reading arrives here having
// bound every one of them but this.  So 3.4.3p1's walk over the components tells
// the two apart on its own: every component the region can walk *into* names a
// class the enclosing readings already settled, and the first it cannot is the
// one whose arguments this head declares - which is the template the definition
// is a member of.  The definition joins *its* members rather than the enclosing
// one's, and the heads after this one travel with it.
//
// Null wherever the specifier writes no such component, which is every
// definition whose head parameterises a member of the class the head above it
// already named.
SemaEntity* PatternReading::nested_owner(const AstNode& node,
                                         const SemaContext& ctx)
{
	// `QualifiedName` reads the spelling it was given rather than copying it, so
	// the string outlives the walk over its components.
	const std::string spelling = member_spelling(node);
	const QualifiedName spelled(spelling);
	if (spelled.size() < 3)
	{
		return nullptr;
	}
	Scope* reached = nullptr;
	for (std::size_t index = 0; index + 1 < spelled.size(); ++index)
	{
		const TemplateId id(spelled.part(index));
		const std::string component =
			id.valid() ? id.name() : spelled.part(index);
		SemaEntity* const named =
			reached == nullptr
				? analyzer_.model_.lookup(*ctx.scope, component,
				                          LookupKind::Region)
				: analyzer_.model_.lookup_in(*reached, component,
				                             LookupKind::Region);
		if (named == nullptr)
		{
			return nullptr;
		}
		const bool templated =
			id.valid() && named->kind == SemaKind::Class &&
			named->templated != nullptr;
		Scope* inside = nullptr;
		try
		{
			// 14.7.1p1: a component the region can settle names one class, and
			// the walk goes on inside it.  One this head has yet to declare the
			// places of settles nothing, which is what stops the walk.
			SemaEntity* const settled =
				templated ? analyzer_.template_id_entity(
					            spelled.part(index), ctx, reached,
					            LookupKind::Region)
				          : named;
			inside = settled == nullptr ? nullptr
			                            : analyzer_.model_.region_of(*settled);
		}
		catch (const std::exception&)
		{
			// 14.6.2p1: the arguments name something this region does not bind,
			// which is what the head standing over this declaration declares.
			inside = nullptr;
		}
		if (inside == nullptr)
		{
			return index != 0 && templated ? named : nullptr;
		}
		reached = inside;
	}
	return nullptr;
}

// 14.5.1.3p1: the definition joins the body it was written over, is read where
// it stands, and is read again for every specialization already made from that
// body.
void PatternReading::record(SemaEntity& primary, std::size_t at,
                            const AstNode& clause, const AstNode& declared)
{
	TemplateInfo& holder = *primary.templated;
	std::vector<TemplateInfo::Member>& members =
		at == Specialization::kNoPartial ? holder.members
		                                 : holder.partials[at].members;
	members.push_back(TemplateInfo::Member(&clause, &declared));
	// 14.6p8: the definition is read where it stands too, against the class
	// that body's own definition declares.
	read_member(primary, clause, declared, at);
	for (std::size_t index = 0; index < holder.specializations.size(); ++index)
	{
		// 10.3p10's table asked for every virtual member of a class already
		// made when that class was completed, so a definition arriving here is
		// one `definition_required` already says this unit owes: nothing
		// re-walks the specialization's members for it.
		instantiate(*holder.specializations[index], members.back(), at);
	}
}

// 14.7.1p1 with 14.5.1.3p1: the member definition `member` read again for the
// specialization `made`.
//
// 14.5.5.1p1 makes the body one argument list is read from the pattern it
// matched, and a member defined outside *that* body is a member of the class
// that body declares alone - so a specialization read from another body holds
// no declaration this definition could make, and the reading is skipped rather
// than made against places it never bound.  The choice is the one `chosen`
// already memoized for this list, so the test is a hash lookup on a number the
// specialization carries and a template no head partially specialized pays a
// test of an empty vector.
void PatternReading::instantiate(SemaEntity& made,
                                 const TemplateInfo::Member& member,
                                 std::size_t at)
{
	SemaEntity& primary = *made.primary;
	const TemplateInfo& info = *primary.templated;
	if (made.scope == nullptr)
	{
		// 14.6.2p1: a specialization over a dependent argument list is a
		// declaration and no class, so it holds no member for a definition
		// written outside the class to declare into.  The one the arguments
		// make of it reads this definition where they are known.
		return;
	}
	const std::vector<TypeId>& arguments =
		analyzer_.types_.type_list_at(made.template_arguments);
	std::vector<TypeId> deduced;
	if (!info.partials.empty() &&
	    Specialization(analyzer_).chosen(primary, arguments, deduced) != at)
	{
		return;
	}
	if (info.partials.empty() && at != Specialization::kNoPartial)
	{
		return;
	}
	// 14.5.1.3p1 and 14.1p2: what the definition's own head declared stands for
	// what *its* body's head declared, which for a pattern is the list the match
	// deduced and for the primary is the specialization's own arguments.
	const bool own = at == Specialization::kNoPartial;
	const TemplateInfo& head = own ? info : *info.partials[at].head;
	Scope* const enclosing = made.scope->parent;
	Scope* const region =
		enclosing != nullptr && enclosing->kind == ScopeKind::TemplateParameters
			? TemplateHead(analyzer_).open_member_parameters(
				  *enclosing, *member.clause, own ? arguments : deduced,
				  SemaKind::Typedef, info.dump)
			: nullptr;
	SemaContext inner;
	inner.scope = region != nullptr
		? region
		: &TemplateHead(analyzer_).open_bindings(head, own ? arguments : deduced);
	inner.dump = info.dump;
	// 9.4.2p1 and 9.3p2: the definition declares into the class the
	// declarator-id names, and writes its lines where that class writes them.
	inner.node = &analyzer_.model_.unit();
	// 14.7.1p1: this reading instantiates the *declaration* the definition
	// writes and not the definition, exactly as the reading of the class's own
	// body does - so the body is put aside for the use that names the member.
	// A definition written outside the class is otherwise the one member body
	// every specialization reads whether or not anything ever calls it, which
	// is both what the clause forbids and what makes n such definitions over n
	// specializations n^2 bodies for the ones a program calls.
	const ReadingDepth instantiating(analyzer_.instantiating_class_);
	if (region == nullptr)
	{
		// A head 14.5.5 parameterises names no member of this template, so the
		// declarator is read against the class's own parameter names and fails
		// on whatever it wrote.
		read_declaration(*member.declaration, inner);
		return;
	}
	const EnclosedBy enclosed(made.scope, region);
	// 14.7.1p1: this head parameterises the definition, so what the reading
	// lays out is a definition no unit wrote for these arguments.  14.7.3p1's
	// `template<>` declares no place at all and is read above, where it is the
	// definition the program itself wrote.
	inner.instantiated_member = true;
	read_declaration(*member.declaration, inner);
}
