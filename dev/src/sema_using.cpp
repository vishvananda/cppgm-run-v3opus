#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_derivation.h"

// 7.3.3 and 7.3.4: the two declarations that make a name one region declares
// reachable from another.
//
// A using-directive is recorded where the program wrote it, because 3.4.1p2's
// lookup reaches through it from there and from nowhere else; a
// using-declaration *declares* what it names, so what the region gets is a
// declaration of its own standing beside the ones written there - and for a
// member of a base class 10.3 and 13.1 then read that declaration exactly as
// they read one the derived class wrote, which is the whole of what
// `hide_using_members` settles.

namespace
{

// The child of `node` of a kind, or null.
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

}

void SemaAnalyzer::using_directive(const AstNode& node, const Context& ctx)
{
	const AstNode* target = child_of(node, AstKind::Target);
	SemaEntity& space =
		require(resolve(target->text, ctx, LookupKind::Space), target->text);
	// 7.3.4p2: the directive is recorded where the program wrote it, because
	// that is what the rule turns on at both ends - the names it nominates can
	// be used *in the scope the directive appears in*, and they appear at the
	// level of the nearest enclosing namespace holding both it and the
	// namespace it named.  A lookup asks the first question of the region chain
	// it stands in and the second of that chain's levels, which is one reading
	// in `SemaModel::lookup`.  Recording it at the level instead answers the
	// second and loses the first: a directive written in one function's block
	// would then reach every lookup in the namespace around it, so
	// `int g() { using namespace R; } int h() { return q; }` would find `R::q`
	// in `h`, and two functions each nominating a different namespace that
	// declares one name would make every use of that name ambiguous.
	model_.nominate(*ctx.scope, *model_.region_of(space));
}

// 12.9p1: the using-declaration whose name 3.4.3.1p2 turned into the
// constructors of the class its nested-name-specifier nominates.
//
// Which of the base's constructors are inherited is a question about the
// complete class - it leaves out the ones this class declares itself, and a
// member declaration standing after this one declares them just as one standing
// before it does.  So the class records that it asked, and 9.2p2's completion
// settles it.  14.6p8's reading of the pattern has no answer to give: 14.6.2p3
// leaves a dependent base off the class, so what the specialization derives
// from is what says whose constructors these are.
void SemaAnalyzer::inheriting_declaration(const QualifiedName& written,
                                          const Context& ctx)
{
	if (checking_ > 0 && ctx.scope->owner->bases.empty())
	{
		return;
	}
	Scope* const nominated = resolve_prefix(written, ctx);
	bool direct = false;
	const std::vector<BaseClass>& bases = ctx.scope->owner->bases;
	for (std::size_t index = 0; !direct && index < bases.size(); ++index)
	{
		direct = bases[index].entity == nominated->owner;
	}
	if (nominated->owner == nullptr || !direct)
	{
		throw std::runtime_error(
			"a using-declaration names the constructors of a class "
			"that is not a direct base of the one it is written in");
	}
	ctx.scope->inheriting_constructors = true;
}

void SemaAnalyzer::using_declaration(const AstNode& node, const Context& ctx)
{
	const AstNode* target = child_of(node, AstKind::Target);
	// 7.3.3p1 and 14.6p2: `using typename X::y` says the name is a type, which
	// is what a nested-name-specifier that depends on a template parameter
	// needs said - and the name the declaration targets is the one after it.
	const std::string spelling =
		target->text.compare(0, 9, "typename ") == 0 ? target->text.substr(9)
		                                             : target->text;
	const QualifiedName written(spelling);
	if (written.names_a_template_id())
	{
		// 7.3.3p5: a using-declaration shall not name a template-id.
		throw std::runtime_error("a using-declaration names a template-id");
	}
	const std::string name = written.last();
	// 3.4.3.1p2's second arm: in a using-declaration that is a
	// member-declaration, a name that is the identifier - or the template-name
	// of the simple-template-id - the last component of its
	// nested-name-specifier was written with names the *constructors* of the
	// class that specifier nominates, and is not looked up in that class at
	// all.  So a typedef-name standing for a base declares the base's
	// constructors exactly as its injected-class-name below does, and
	// `Alias::Alias` reaches a name no member of `Alias` has.
	if (written.qualified() && ctx.scope->kind == ScopeKind::Class &&
	    ctx.scope->owner != nullptr)
	{
		const std::string component = written.part(written.size() - 2);
		const TemplateId id(component);
		if (name == (id.valid() ? id.name() : component))
		{
			inheriting_declaration(written, ctx);
			return;
		}
	}
	SemaEntity& entity =
		require(resolve(spelling, ctx, LookupKind::Any), spelling);
	if (ctx.scope->kind == ScopeKind::Class && ctx.scope->owner != nullptr)
	{
		// 7.3.3p1 read in a class: what the using-declaration makes is a
		// declaration of this class, not a second name for the base's.  The
		// access 11p1 gave it and the hiding 7.3.3p14 asks about are facts
		// about this class's declaration, and the base's is what a use of it
		// reaches.
		if (entity.kind == SemaKind::Class && written.qualified() &&
		    entity.scope == resolve_prefix(written, ctx))
		{
			// 3.4.3.1p2's first arm: the name looked up in the class the
			// nested-name-specifier nominates is that class's
			// injected-class-name, so what it names is the class's
			// constructors rather than a member of it.
			inheriting_declaration(written, ctx);
			return;
		}
		declare_using_members(entity, *ctx.scope, name);
		write_entity_line(*ctx.dump, entity);
		return;
	}
	// 14.6.1p6 is not asked here.  A using-declaration declares no name of its
	// own - 7.3.3p1 makes the declarations it names members of this region, and
	// the name they are found by is the one the region it came from gave them -
	// so `using N::T` inside a template whose head declared `T` redeclares
	// nothing, which is what both oracles say of it.  A class-scope
	// using-declaration is asked, because 7.3.3p1 there makes a declaration of
	// this class that `declare_using_member` creates.
	model_.bind(*ctx.scope, name, entity);
	model_.declare_in(*ctx.scope, entity);
	write_entity_line(*ctx.dump, entity);
}
// 7.3.3p14 hides a member function template of a base with a member function
// template of this class "with the same name, parameter-type-list,
// cv-qualification and ref-qualifier", and the two lists are written in places
// two different heads declared - `const K&` in the base and `const K&` in the
// derived class are two types.  14.5.6.1p5 is what makes them one list: each
// head's places stand for their positions, which is what `template_signature`
// substitutes them onto.  A head that declares a template-template parameter
// has no such form, and the declaration's own type is then the key it keeps -
// which matches nothing else, so the two declarations overload as they did.
std::uint32_t SemaAnalyzer::hiding_signature(const SemaEntity& function)
{
	if (function.template_parameters == nullptr)
	{
		return member_signature(function);
	}
	// One canonical form per declaration, held where 14.5.6.1p5's own
	// comparison already holds it: the class's declarations of one name are
	// walked three times over one using-declaration - once where it is
	// written and twice where 9.2p2 completes the class - and the
	// substitution that builds the form is what a walk would otherwise pay
	// for at every pass.
	bool held = false;
	TypeId canonical = signatures_.built(function.id, held);
	if (!held)
	{
		// The build makes places of its own, so the entry is written back
		// rather than filled through a reference the build could move.
		canonical =
			template_signature(*function.template_parameters, function.type);
		signatures_.built(function.id, held) = canonical;
	}
	// A template and a non-template of one spelling hide nothing of each
	// other's: the stand-ins stand in no type a declarator wrote, so the two
	// keys differ wherever one head was read and the other was not.
	return canonical == kNoType
		? member_signature(function)
		: member_signature(canonical, function.object_member);
}

void SemaAnalyzer::hide_using_members(Scope& where)
{
	for (std::size_t index = 0; index < where.using_names.size(); ++index)
	{
		const std::string& name = where.using_names[index];
		SemaEntity* const head = model_.find(where, name, LookupKind::Any);
		if (head == nullptr || head->kind != SemaKind::Function)
		{
			continue;
		}
		// 7.3.3p14: what this class declared itself hides what the base
		// declared with the same name and parameter-type-list rather than
		// conflicting with it, whichever of the two the class body wrote
		// first.  The declarations of the name are what the source wrote, so
		// one pass says which signatures the class has of its own and one more
		// takes the brought-in declarations of those signatures off the chain.
		std::unordered_set<std::uint32_t> declared;
		bool brought_in = false;
		for (SemaEntity* at = head; at != nullptr; at = at->next)
		{
			if (at->shadowed != nullptr)
			{
				brought_in = true;
			}
			else
			{
				declared.insert(hiding_signature(*at));
			}
		}
		if (!brought_in || declared.empty())
		{
			continue;
		}
		SemaEntity* kept = nullptr;
		SemaEntity* tail = nullptr;
		for (SemaEntity* at = head; at != nullptr;)
		{
			SemaEntity* const next = at->next;
			at->next = nullptr;
			if (at->shadowed == nullptr ||
			    declared.count(hiding_signature(*at)) == 0)
			{
				if (kept == nullptr)
				{
					kept = at;
				}
				else
				{
					tail->next = at;
				}
				tail = at;
			}
			at = next;
		}
		kept->tail = tail;
		if (kept == head)
		{
			continue;
		}
		// What the name was bound to was one of the declarations hidden, so
		// the name now binds what is left - and 13.1's index of the chain is
		// keyed by the declaration the name binds, so what stays is keyed
		// again under it.  A declaration a using-declaration brought in is one
		// 7.3.3p14 hides rather than one 13.1 redeclares, so it stays out of
		// that index exactly as it was when the using-declaration made it.
		model_.bind(where, name, *kept);
		for (SemaEntity* at = kept; at != nullptr; at = at->next)
		{
			if (at->shadowed == nullptr)
			{
				model_.hold_overload(
					*kept, declaration_signature(where, at->type,
					                             at->object_member), *at);
			}
		}
	}
}

void SemaAnalyzer::declare_using_members(SemaEntity& named, Scope& where,
                                         const std::string& name)
{
	// 7.3.3p14: a member function this class declared itself hides the one the
	// base declared with the same name and parameter list rather than
	// conflicting with it, so what is brought in is what the class does not
	// already declare.  The declarations of the name in each region are what
	// the source wrote, so this costs one pass over them and no search.
	std::unordered_set<std::uint32_t> declared;
	for (SemaEntity* at = model_.find(where, name, LookupKind::Any);
	     at != nullptr && at->kind == SemaKind::Function; at = at->next)
	{
		declared.insert(hiding_signature(*at));
	}
	for (SemaEntity* at = &named; at != nullptr; at = at->next)
	{
		if (at->kind != SemaKind::Function)
		{
			// A name bound to anything else is bound to one declaration, so
			// there is no chain to walk and nothing of the class's own that it
			// could be hiding.
			declare_using_member(*at, where, name);
			return;
		}
		if (declared.insert(hiding_signature(*at)).second)
		{
			declare_using_member(*at, where, name);
		}
	}
}

SemaEntity& SemaAnalyzer::declare_using_member(SemaEntity& target, Scope& where,
                                               const std::string& name)
{
	require_no_template_parameter(name, where);
	SemaEntity& shadow = model_.create(target.kind, name, target.type);
	const std::uint32_t id = shadow.id;
	// The declaration says the same thing about the entity the base declared as
	// the base's own does - it is the same member of the same class, laid out
	// where the base laid it out - so it is copied whole, and what is a fact
	// about this declaration alone is written over it.
	shadow = target;
	shadow.id = id;
	shadow.name = name;
	shadow.next = nullptr;
	shadow.tail = &shadow;
	shadow.region = nullptr;
	shadow.access = kPublicAccess;
	shadow.shadowed =
		target.shadowed != nullptr ? target.shadowed : &target;
	name_in_region(shadow, where, name);
	if (shadow.kind == SemaKind::Function && shadow.object_member &&
	    where.owner != nullptr)
	{
		// 13.3.3.1p4: a non-conversion function a using-declaration brought into
		// a derived class is a member of that class where the type of the
		// implicit object parameter is concerned, which is what ranks it against
		// the class's own declarations on an object of the class.  9.3.1p3 put
		// that parameter in the type, so this is where the rule is written; what
		// the call passes is still the base subobject, because the function it
		// runs is the base's.
		const std::vector<TypeId>& written = types_.parameters(target.type);
		std::vector<TypeId> parameters;
		parameters.push_back(types_.pointer_to(types_.qualified(
			where.owner->type, types_.object_cv(types_.target(written[0])))));
		for (std::size_t index = 1; index < written.size(); ++index)
		{
			parameters.push_back(written[index]);
		}
		// 8.3.5p1: only the implicit object parameter is the derived class's;
		// the ref-qualifier written after the parameter-clause is part of the
		// function type the base declared, so it travels with the rest of it -
		// which is what 13.3.1p4 reads to say the brought-in declaration is
		// viable, and what 7.3.3p14 reads to say the derived class's own
		// declaration of the same spelling hides it.
		shadow.type = types_.ref_qualified_function(
			types_.function_of(types_.target(target.type), parameters,
			                   types_.variadic(target.type)),
			types_.function_ref_qualifier(target.type));
	}
	SemaEntity* const head = model_.find(where, name, LookupKind::Any);
	if (head != nullptr && head->kind == SemaKind::Function &&
	    shadow.kind == SemaKind::Function)
	{
		// 13.1: the declarations of one name in one region are one chain, and
		// 13.3 ranks what a using-declaration brought in beside the class's own.
		head->tail->next = &shadow;
		head->tail = &shadow;
	}
	else
	{
		model_.bind(where, name, shadow);
	}
	if (shadow.kind == SemaKind::Function)
	{
		// 7.3.3p14's hiding is asked of this name where the class is complete,
		// and this is what says the class has one to ask about.  A class writes
		// the using-declarations it writes, so holding them is what the source
		// wrote and no search.
		bool held = false;
		for (std::size_t index = 0;
		     !held && index < where.using_names.size(); ++index)
		{
			held = where.using_names[index] == name;
		}
		if (!held)
		{
			where.using_names.push_back(name);
		}
	}
	model_.declare_in(where, shadow);
	return shadow;
}
