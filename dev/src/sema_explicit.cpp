#include "sema_analyzer.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "ast_model.h"
#include "ast_tokens.h"
#include "sema_deduce.h"
#include "sema_pattern.h"
#include "sema_specialize.h"
#include "sema_template.h"
#include "sema_template_head.h"
#include "token_model.h"

// 14.7.2 and 14.7.3: the two clauses a program writes one argument list out
// with, and what each of them owns.
//
// Everything else in the template layer reads a pattern for a list some *use*
// named.  These two are the declarations the program wrote for a list itself:
// 14.7.3p1's `template<>`, which says what a specialization *is* - a class
// body, a function body, a variable's initializer or a member class - in place
// of whatever the pattern would have said, and 14.7.2's `template` and
// `extern template`, which say nothing about the meaning of a specialization
// and everything about which unit's object file owes its definitions.
//
// Both are recorded against the interned argument list the specialization is
// already found by, so the reading that makes one asks a hash lookup on a
// number it already holds and never scans what the program wrote.  Neither
// makes a declaration of its own: the specialization is the same one an
// instantiation of the pattern would have made, and what these clauses change
// is the body it is read from and the definitions this unit owes for it.

namespace
{

const AstNode* first_child(const AstNode& node, AstKind kind)
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

// 14.7.3p1: the one init-declarator a `template<>` declaration writes.  A head
// parameterises one declaration, so a simple-declaration that declared two names
// is no explicit specialization of either.
const AstNode* only_declarator(const AstNode& declared)
{
	const AstNode* const list =
		first_child(declared, AstKind::InitDeclaratorList);
	if (list == nullptr || list->children.size() != 1 ||
	    list->children[0]->children.empty() ||
	    list->children[0]->children[0]->kind != AstKind::Declarator)
	{
		return nullptr;
	}
	return list->children[0];
}

// 14.7.2p8: an explicit instantiation of a class template specialization is an
// explicit instantiation of each of its members, and of theirs in turn, so the
// walk is over the region the specialization opened rather than over one list
// of declarations.  A member a base class declares is left out, which the
// region already answers: 10.2p2's lookup reaches those through the base and
// the base's own region is what holds them.
//
// 14.7.2p10's form asks no walk at all: `instantiation_is_suppressed` walks the
// classes a member stands in where a use asks for the definition, because a
// member's own definition may be written below the declaration and a walk made
// where the declaration stands would read a class whose members are not yet
// defined.
//
// The bodies the instantiation put aside are handed back rather than asked for
// here, because the ask is `require_definition`'s and this walk has no analyzer.
void demand_member_definitions(SemaEntity& made,
                               std::vector<SemaEntity*>& demanded)
{
	if (made.scope == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < made.scope->declarations.size();
	     ++index)
	{
		SemaEntity& member = *made.scope->declarations[index];
		if (member.kind == SemaKind::Function && member.defined &&
		    !member.inline_function)
		{
			// 14.7.2p11: the explicit instantiation is one of only those
			// members that have been *defined* where it stands, so a member
			// the template gives a definition to further down the unit is left
			// to the use that requires it - which is 3.2p3, and which is also
			// where 9.3p2's member defined *in* its class stays.  An inline
			// definition belongs to every unit that needs one, so no unit is
			// asked to hold a copy nothing there reaches; a definition written
			// outside the class is one no unit would write for these arguments
			// at all, and this declaration is what makes this unit write it.
			// Either way the binding is unchanged: what this says is whether
			// the symbol is a root of this object file.
			member.explicitly_instantiated = true;
			demanded.push_back(&member);
		}
		else if (member.kind == SemaKind::Class &&
		         member.scope != nullptr && member.scope != made.scope)
		{
			demand_member_definitions(member, demanded);
		}
	}
}

// The one declarator 14.7.2p1's simple-declaration target writes.
const AstNode* instantiated_declarator(const AstNode& target)
{
	for (std::size_t index = 0; index < target.children.size(); ++index)
	{
		const AstNode& child = *target.children[index];
		if (child.kind != AstKind::InitDeclaratorList ||
		    child.children.empty() || child.children[0]->children.empty())
		{
			continue;
		}
		return child.children[0]->children[0];
	}
	return nullptr;
}

// 14.7.2p1: whether a name reaches through a class template specialization,
// which is what lets an ordinary member class of one be a thing an explicit
// instantiation names.  A specialization is written as a template-id, so a
// component before the last that is one is the whole of the question.
bool names_a_specialization_member(const QualifiedName& spelled)
{
	for (std::size_t index = 0; index + 1 < spelled.size(); ++index)
	{
		if (TemplateId(spelled.part(index)).valid())
		{
			return true;
		}
	}
	return false;
}

}

// 14.7.3p1: the declarator-id a `template<>` head stands over.
//
// The head parameterises one declaration and the clause writes it in one of
// three shapes: 8.4p1's function definition, 7p1's simple-declaration, and 12's
// own node for a constructor, a destructor or a conversion function - which
// carries the spelling its declarator-id wrote on the node itself.  Empty where
// the declaration wrote no single declarator-id, which is a shape 14.7.3p1 says
// nothing about.
//
// 14.5.2p3 writes one head per class the member is nested in and the member's
// own last, so `template<> template<class U> void A<int>::g(U) {}` is a head
// standing over further heads - and the declarator-id the clause parameterises
// is the innermost declaration's, which is the same descent `record_template`
// makes to find it.
std::string SemaAnalyzer::specialized_declarator_id(const AstNode& node)
{
	const AstNode* innermost = &node;
	while (innermost->kind == AstKind::TemplateDeclaration &&
	       !innermost->children.empty())
	{
		innermost = innermost->children[innermost->children.size() - 1];
	}
	const AstNode& declared = *innermost;
	const AstNode* declarator = nullptr;
	if (declared.kind == AstKind::FunctionDefinition)
	{
		declarator = declared.children.size() > 1 ? declared.children[1] : nullptr;
	}
	else if (declared.kind == AstKind::SimpleDeclaration)
	{
		const AstNode* const init = only_declarator(declared);
		declarator = init == nullptr || init->children.empty()
			? nullptr
			: init->children[0];
	}
	else
	{
		return declared.text;
	}
	const AstNode* const id =
		declarator == nullptr ? nullptr
		                      : declarator_id(*declarator);
	return id == nullptr ? std::string() : id->text;
}

// 14.7.3p1: the declaration a `template<>` head wrote, which declares the
// specialization itself and no template.
//
// The specialization is the same declaration an instantiation of the pattern
// would have made - one per template and argument list - so it is reached the
// way every other naming reaches it, and what the explicit declaration adds is
// the *definition* it wrote: a class body read in place of the pattern's, and a
// function body run in place of the pattern's.  Both hang off the template
// under the interned argument list, which is the key the specialization is
// already found by, so an instantiation asks one lookup on a number it has.
//
// False where the head declares something outside the supported slice, which
// leaves the ordinary walk to read it as it did before.
bool SemaAnalyzer::record_explicit_specialization(const AstNode& declared,
                                                  const Context& ctx)
{
	if (declared.kind != AstKind::ClassSpecifier &&
	    declared.kind != AstKind::ClassForwardDeclaration)
	{
		// 14.7.3p5: whatever this head stands over, it stands over a member of
		// the class its declarator-id names - and a class the program wrote out
		// for itself has none the pattern declared.
		require_unspecialized_owner(specialized_declarator_id(declared), ctx);
		// 14.5.1p1: what a `template<>` head wrote over an object is the
		// specialization of a variable template, whose body is one
		// init-declarator rather than a class body or a function body.
		return Specialization(*this).record_explicit(declared, ctx) ||
			record_explicit_function(declared, ctx);
	}
	const QualifiedName spelled(declared.text);
	const TemplateId id(spelled.last());
	if (!id.valid())
	{
		// 14.7.3p1: the last component names no argument list of its own, so what
		// this head wrote out is a member *class* of the specialization its
		// nested-name-specifier names.
		return PatternReading(*this).record_explicit_member_class(
			declared, spelled, ctx);
	}
	Context target = ctx;
	if (spelled.qualified())
	{
		target.scope = resolve_prefix(spelled, ctx);
		target.dump = target.scope->dump;
	}
	SemaEntity* const primary =
		spelled.qualified()
			? model_.lookup_in(*target.scope, id.name(), LookupKind::Type)
			: model_.lookup(*target.scope, id.name(), LookupKind::Type);
	if (primary == nullptr || primary->kind != SemaKind::Class ||
	    primary->templated == nullptr)
	{
		throw std::runtime_error("an explicit specialization of " + id.name() +
		                         " names no class template");
	}
	std::vector<TypeId> arguments;
	TemplateHead(*this).bind_arguments(*primary, id.arguments(), ctx, arguments);
	TemplateInfo& info = *primary->templated;
	const std::uint32_t list = types_.type_list(arguments);
	if (declared.kind == AstKind::ClassSpecifier)
	{
		// 14.7.3p6: the specialization shall be declared before the first use of
		// it that would cause an implicit instantiation, and a class this unit has
		// already read the pattern for is exactly such a use - the declarations
		// that reading made are the pattern's and this body would give the same
		// class a second set.
		const SemaEntity* const already =
			model_.specialization_of(*primary, list);
		if (already != nullptr && already->defined &&
		    info.explicit_classes.find(list) == info.explicit_classes.end())
		{
			throw std::runtime_error("an explicit specialization of " + id.name() +
			                         " is written after the specialization it "
			                         "names was instantiated");
		}
		// The body is recorded before the declaration is reached, so that a
		// specialization the unit already named is completed from *this*
		// definition rather than from the pattern.
		info.explicit_classes[list] =
			WrittenBody(&declared, model_.written_bound());
	}
	SemaEntity& made = instantiate_class(*primary, arguments);
	if (declared.kind != AstKind::ClassSpecifier)
	{
		return true;
	}
	if (made.declared_only)
	{
		made.declared_only = false;
		--declared_only_;
	}
	require_specialization(made);
	ctx.dump->lines.push_back("type " + made.name + " " +
	                          (types_.class_tag(made.type) == ClassTag::Union
	                               ? "union "
	                               : (types_.class_tag(made.type) == ClassTag::Class
	                                      ? "class "
	                                      : "struct ")) +
	                          made.name);
	return true;
}

// 14.7.3p1 over a function template, where the declarator-id wrote the argument
// list the specialization is of.  14.8.2's deduction of an argument list the
// declaration left unwritten is a later milestone, so a head that wrote none
// leaves the declaration to the ordinary walk.
//
// The clause writes a *declaration* as readily as a definition, and what that
// declaration says is the whole of 14.7.3p1: this argument list is not the
// pattern's to be read for.  So a simple-declaration is taken here too and the
// body alone is what tells the two apart - `has_written_definition` reads the
// mark, and a specialization named where no `template<>` wrote a body is a
// declaration this unit writes and no instantiation of anything.
// 14.7.3p5: the definition of an explicitly specialized class is unrelated to
// the one a generated specialization would have had - its members need not have
// the same names or types at all - so its members are defined the way a normal
// class's are, and not with the `template<>` syntax.
//
// What the head would specialize is a member of the *pattern*, and a class the
// program wrote out for these arguments has no member the pattern declared: the
// body the class specifier wrote is the whole of it.  So the refusal is asked
// of the class the declarator-id's nested-name-specifier names, which is the
// same walk every other qualified declarator-id makes, and `explicit_classes`
// is the fact the class tier already recorded for exactly this argument list.
void SemaAnalyzer::require_unspecialized_owner(const std::string& written,
                                               const Context& ctx)
{
	const QualifiedName spelled(written);
	if (!spelled.qualified())
	{
		return;
	}
	// 14.7.3p5 is written about the class the *member* belongs to, and a
	// declarator-id may name a class nested below one the program wrote out:
	// `template<> void A<int>::in::f() {}` writes the head over a member of
	// `in`, whose own definition is the body the explicit specialization of
	// `A` wrote.  So every class the declarator-id walked through is asked,
	// which is the chain of regions the prefix it resolved stands in.
	for (const Scope* at = resolve_prefix(spelled, ctx);
	     at != nullptr && at->kind == ScopeKind::Class; at = at->parent)
	{
		const SemaEntity* const owner = at->owner;
		if (owner == nullptr || owner->primary == nullptr ||
		    owner->primary->templated == nullptr)
		{
			continue;
		}
		const TemplateInfo& info = *owner->primary->templated;
		if (info.explicit_classes.find(owner->template_arguments) !=
		    info.explicit_classes.end())
		{
			throw std::runtime_error("a member of the explicitly specialized "
			                         "class " + owner->name + " is defined with "
			                         "a template<> head, which 14.7.3p5 leaves "
			                         "to the members of a specialization the "
			                         "pattern was read for");
		}
	}
}


bool SemaAnalyzer::record_explicit_function(const AstNode& declared,
                                            const Context& ctx)
{
	const bool defines = declared.kind == AstKind::FunctionDefinition;
	const AstNode* const init =
		declared.kind == AstKind::SimpleDeclaration ? only_declarator(declared)
		                                           : nullptr;
	if (!defines &&
	    (init == nullptr ||
	     first_child(*init, AstKind::Initializer) != nullptr))
	{
		// 14.5.1p1's variable template specialization writes an initializer, and
		// `Specialization::record_explicit` is what has already been asked about
		// it; a declaration with none is what a function's may be.
		return false;
	}
	const AstNode* const id = defines
		? (declared.children.size() > 1 ? declarator_id(*declared.children[1])
		                                : nullptr)
		: (init->children.empty() ? nullptr
		                          : declarator_id(*init->children[0]));
	if (id == nullptr || !TemplateId(QualifiedName(id->text).last()).valid())
	{
		return false;
	}
	std::vector<SemaEntity*> found;
	template_specializations(id->text, ctx, found);
	SemaEntity* chosen = nullptr;
	for (std::size_t index = 0; index < found.size(); ++index)
	{
		if (found[index]->primary != nullptr &&
		    found[index]->primary->templated != nullptr)
		{
			// 14.7.3p11: the declaration shall be a specialization of exactly
			// one template, which one written argument list over one overload
			// set leaves whenever the set holds one that fits it.
			if (chosen != nullptr)
			{
				return false;
			}
			chosen = found[index];
		}
	}
	if (chosen == nullptr)
	{
		return false;
	}
	if (defines)
	{
		chosen->primary->templated
			->explicit_functions[chosen->template_arguments] =
				WrittenBody(&declared, model_.written_bound());
	}
	// 14.7.3p6: what the program wrote out here is this unit's own definition
	// and no reading of the pattern, so what says whether it is `inline` is its
	// own declaration and not the template it specializes.  A declaration with no
	// body says the second half of that on its own: the pattern is not what this
	// argument list is read from, so nothing instantiates one for it and the
	// output writes the declaration alone.
	chosen->explicit_specialization = true;
	// 14.7.3p6: the definition is this unit's own wherever it is named, and a
	// use written above it has already asked for the pattern's - so the
	// instantiation is started again from what was just written.
	chosen->instantiated = false;
	if (chosen->definition_required)
	{
		instantiate(*chosen);
	}
	return true;
}

// 14.7.2p1's other target: a declaration whose declarator names a function or
// an object rather than a class.  It declares nothing - the specialization it
// names was made by the template that has it - so the declaration is read for
// the type it writes and the answer is looked for among the specializations of
// its name, and what it changes is which unit's object file owes the
// definition.
void SemaAnalyzer::explicit_instantiation_declarator(const AstNode& target,
                                                     const Context& ctx,
                                                     bool owed)
{
	const AstNode* const declarator = instantiated_declarator(target);
	if (declarator == nullptr)
	{
		// 14.7.2p2: the declaration shall name a specialization, so one with no
		// declarator at all names nothing.
		throw std::runtime_error("an explicit instantiation writes a "
		                         "declaration that declares nothing");
	}
	if (!templating())
	{
		// PA11 and PA12 describe what a declaration says and instantiate
		// nothing, so the template layer has no specialization to answer with.
		return;
	}
	const AstNode* const id = declarator_id(*declarator);
	const std::string written = id == nullptr ? std::string() : id->text;
	const QualifiedName spelled(written);
	Span span;
	span.begin = target.begin;
	span.end = target.end;
	const Naming naming(*this, naming_context(written, ctx));
	const Specifiers specifiers =
		read_specifiers(*target.children[0], ctx, span, true, written);
	// 3.4.1p8: the rest of a declarator whose declarator-id is qualified is
	// read in the region that name reaches, which for a member of a class
	// template specialization is the class this names.
	Context reached = ctx;
	bool instantiated_region = false;
	if (spelled.qualified())
	{
		reached.scope = resolve_prefix(spelled, ctx);
		reached.dump = reached.scope->dump;
		// 14.7.2p1: what makes this declaration name a specialization is that
		// an instantiation is what declared it, and a class the pattern nests
		// inside its body is made by that same instantiation - so the question
		// is asked of the class the prefix named and of every class it stands
		// in, rather than of the innermost one alone.
		for (Scope* at = reached.scope;
		     at != nullptr && at->kind == ScopeKind::Class &&
		         !instantiated_region;
		     at = at->parent)
		{
			if (at->owner == nullptr || at->owner->primary == nullptr)
			{
				continue;
			}
			instantiated_region = true;
			// 14.7.1p1: a specialization holds no member until something asks
			// for its completion, and 14.7.2p2's declaration names one of them.
			require_specialization(*at->owner);
		}
	}
	std::string ignored;
	TypeId type = declarator_type(*declarator, specifier_type(specifiers),
	                              spelled.qualified() ? reached : ctx, &ignored,
	                              nullptr,
	                              declares_object_member(specifiers));
	// 9.3.1p3: the object a member function is called on is no part of what
	// its declarator wrote and is part of the type its declaration has, so
	// the declaration this names is matched by the type *it* was recorded
	// with - both spellings are built here and each candidate is asked with
	// the one it carries.  14.7.2p1's other declarator names a static data
	// member, which is called on nothing.
	const TypeId member =
		types_.kind(type) == TypeKind::Function
			? with_object_parameter(type, *declarator, reached,
			                        specifiers.is_static, spelled.last(),
			                        spelled.qualified())
			: type;
	SemaEntity* const made =
		instantiation_named(written, spelled.last(), type, member,
		                    spelled.qualified() ? reached : ctx,
		                    instantiated_region);
	if (made == nullptr)
	{
		throw std::runtime_error("an explicit instantiation names " + written +
		                         ", which is no specialization of a template");
	}
	// 14.7.2p8 and 3.2p3: the definition is still the program's rather than
	// this unit's, so what this says is that this object file owes it with no
	// use to point at.  A static data member's definition waits for no use, so
	// naming its class is the whole of what this asks for - which is where the
	// class form leaves one too.
	if (!owed)
	{
		// 14.7.2p10: p9's form says another unit owes the definition, so this
		// one instantiates none of it - and its note leaves an `inline`
		// function where the rest are at the object file, because what the
		// exception keeps is a body a call may be folded into rather than an
		// out-of-line copy this unit writes.  The declarator names a function
		// or a static data member and the answer is the same for both: a use
		// writes the symbol and no definition of it.  p11 puts a definition
		// this unit was asked for above such a declaration as much as below
		// it, so what this unit already owes it goes on owing.
		made->instantiation_suppressed = !made->explicitly_instantiated;
		return;
	}
	// 14.7.2p11: an explicit instantiation definition below p9's declaration
	// takes the suppression back, and this unit owes the definition again.
	made->instantiation_suppressed = false;
	if (made->kind != SemaKind::Function)
	{
		return;
	}
	made->explicitly_instantiated = true;
	// 14.7.1p1: the definition an instantiation put aside waits for the use
	// that names the member, and this declaration is the one demand 3.2p3 has
	// no use to point at - so it asks for the body here as a call would.
	require_definition(*made);
	if (made->primary != nullptr &&
	    made->primary->template_parameters != nullptr)
	{
		// 14.7.2p8: a function template's specialization is instantiated here,
		// where a member of a class template specialization was instantiated
		// with the class its prefix named.
		instantiate(*made);
	}
}

// 14.7.2p1: an explicit instantiation, which names a specialization where no
// use of it stands.
//
// 14.7.1p1 leaves an instantiated definition to the use requiring it, and this
// is the one declaration that requires one without writing a use: p8 makes it
// an explicit instantiation of every member the template gave the
// specialization, so this unit owes the object file each definition the
// template has for one.  p9's `extern template` is the other form and asks for
// nothing at all - it says another unit owes them, which is what a
// specialization no use of this unit names already leaves.
void SemaAnalyzer::explicit_instantiation(const AstNode& node,
                                          const Context& ctx)
{
	if (node.children.empty())
	{
		return;
	}
	// 14.7.2p9: `extern template` differs from p8's form in what it asks of
	// this unit and in nothing else - p2's requirement that the declaration
	// name a specialization is the same requirement written the same way - so
	// the target is read either way and only p8's demand for the definitions
	// is left out.
	const bool owed = node.kind == AstKind::ExplicitInstantiationDefinition;
	const AstNode& target = *node.children[0];
	if (target.kind == AstKind::SimpleDeclaration)
	{
		explicit_instantiation_declarator(target, ctx, owed);
		return;
	}
	std::string written = target.text;
	if (target.kind == AstKind::SpecialMemberDeclaration)
	{
		// 12.1p1 and 14.7.2p1: a constructor has no type a declarator can be
		// read for, so a declaration naming one is written the way 12 writes
		// it and reaches no simple-declaration.  The specialization p2 asks
		// about is its *prefix*, which is the same elaborated name the class
		// form writes on its own.
		const std::string prefix = QualifiedName(target.text).prefix();
		if (prefix.size() < 2)
		{
			throw std::runtime_error("an explicit instantiation names " +
			                         target.text +
			                         ", which is no member of a class template "
			                         "specialization");
		}
		written = prefix.substr(0, prefix.size() - 2);
	}
	else if (target.kind != AstKind::ClassForwardDeclaration)
	{
		// 14.7.2p2: a class-specifier defines a class, and an explicit
		// instantiation names one the template already gave a definition to.
		throw std::runtime_error("an explicit instantiation defines a class "
		                         "rather than naming a specialization");
	}
	SemaEntity* const made = instantiated_class(written, ctx);
	if (made == nullptr)
	{
		return;
	}
	if (made->primary != nullptr)
	{
		// 14.7.2p1 over a *member* class of a specialization: the class its
		// prefix named is what a template made, and this member is an ordinary
		// declaration of it that the same instantiation already completed.
		require_specialization(*made);
	}
	if (target.kind == AstKind::SpecialMemberDeclaration)
	{
		// 14.7.2p1 names one member and not the class, and the member 12.1p1
		// writes this way is a constructor - which 14.5.2's constructor
		// template is what gives a definition to, so the definitions this unit
		// owes for the whole class are not what this declaration asked for.
		return;
	}
	if (!owed)
	{
		// 14.7.2p10: naming the class names each of its members, so p9's form
		// is one fact on the specialization and no walk of what it declares -
		// a member is asked about where a use asks for its definition, which
		// is where the definition it may not have been given yet stands.  p11
		// leaves a definition this unit was asked for standing, whichever side
		// of the declaration the program wrote it on.
		made->instantiation_suppressed = !made->explicitly_instantiated;
		return;
	}
	// 14.7.2p11: this unit owes the definitions from here on, however far above
	// this an `extern template` for the same specialization stands.
	made->explicitly_instantiated = true;
	made->instantiation_suppressed = false;
	// 14.7.2p8 and 3.2p3: this declaration is the one demand with no use to
	// point at, so a member whose definition the instantiation put aside is
	// asked for it here exactly as a call would ask.
	std::vector<SemaEntity*> demanded;
	demand_member_definitions(*made, demanded);
	for (std::size_t index = 0; index < demanded.size(); ++index)
	{
		require_definition(*demanded[index]);
	}
}

// 14.7.2p2: which declaration the type an explicit instantiation wrote names.
// 14.8.1's explicit argument list makes the specialization outright and the
// type is what tells the declarations of the name it fits apart; where none was
// written, 14.8.2.2 deduces the arguments from that same type.  A member of a
// class template specialization is a third answer: it is no template of its
// own - the class its prefix named declared it as an ordinary member - so what
// says it is a specialization is the region it stands in, and the type is all
// there is to match.
SemaEntity* SemaAnalyzer::instantiation_named(const std::string& written,
                                              const std::string& name,
                                              TypeId declared, TypeId member,
                                              const Context& ctx,
                                              bool instantiated_region)
{
	if (TemplateId(name).valid())
	{
		std::vector<SemaEntity*> found;
		template_specializations(written, ctx, found);
		for (std::size_t index = 0; index < found.size(); ++index)
		{
			const TypeId wanted =
				found[index]->object_member ? member : declared;
			// 14.8.1p2: a list that wrote only a leading part of the arguments
			// leaves the rest to be deduced here as much as at a call, so what
			// this declaration names is 14.8.2.2's answer over the type the
			// declaration wrote - which is the arm below, asked of a name that
			// wrote no list at all.
			SemaEntity* const one =
				found[index]->partial_of != nullptr
					? Deduction(*this).from_target(*found[index], wanted)
					: (found[index]->type == wanted ? found[index] : nullptr);
			if (one != nullptr)
			{
				return one;
			}
		}
		return nullptr;
	}
	SemaEntity* first = resolve(written, ctx, LookupKind::Any);
	if (first == nullptr && name != written)
	{
		// 3.4.3.1p1: a name written behind a class template specialization is a
		// member of the class that specialization is, which is a region no
		// spelling of the template's own name reaches.
		first = resolve(name, ctx, LookupKind::Any);
	}
	SemaEntity* chosen = nullptr;
	for (SemaEntity* at = first; at != nullptr; at = at->next)
	{
		// 9.3.1p3's object parameter is part of what a declaration says, so
		// the type this matches is the one *that* declaration was recorded
		// with: a member function carries it and a namespace-scope one does
		// not.
		const TypeId wanted = at->object_member ? member : declared;
		SemaEntity* const one = at->template_parameters != nullptr
			? Deduction(*this).from_target(*at, wanted)
			: ((instantiated_region || at->primary != nullptr) &&
			   at->type == wanted
				? at
				: nullptr);
		if (one == nullptr)
		{
			continue;
		}
		if (chosen != nullptr && chosen != one)
		{
			// 14.7.2p2 leaves the declaration naming one specialization, and two
			// that both deduce this type name no one of them.
			return nullptr;
		}
		chosen = one;
	}
	return chosen;
}


// 14.7.2p2: the class template specialization an explicit instantiation names,
// which is the one requirement p8's form and p9's each write the same way - so
// it is read off the elaborated name either form wrote, and only p8's demand
// for the definitions is left out.  Null is no answer but PA11's and PA12's,
// which describe what a declaration says and instantiate nothing.
SemaEntity* SemaAnalyzer::instantiated_class(const std::string& written,
                                             const Context& ctx)
{
	const QualifiedName spelled(written);
	Scope* const in =
		spelled.qualified() ? resolve_prefix(spelled, ctx) : nullptr;
	SemaEntity* made =
		template_id_entity(spelled.last(), ctx, in, LookupKind::Type);
	if (made == nullptr && in != nullptr && !TemplateId(spelled.last()).valid())
	{
		// 14.7.2p1's other class target: a *member* class of a class template
		// specialization, which is no specialization of a template of its own
		// - the class its prefix named declared it as an ordinary member.  p2
		// is therefore asked of the prefix, which `resolve_prefix` above has
		// just had to settle, and the member is looked up in it as any
		// qualified name is.
		made = model_.lookup(*in, spelled.last(), LookupKind::Type);
	}
	if (made == nullptr && !templating())
	{
		return nullptr;
	}
	if (made == nullptr || made->kind != SemaKind::Class ||
	    (made->primary == nullptr && !names_a_specialization_member(spelled)))
	{
		// 14.7.2p2: the elaborated-type-specifier shall name a class template
		// specialization, or a member class of one.  A name that is no
		// template-id and a template-id over a name no template declares
		// answer nothing at all; 7.1.3p2's `X<int>` over 14.5.7p1's alias
		// template answers a typedef-name that 14.5.7p1 leaves no
		// specialization behind, and 14.6.2p1's naming over a place no list
		// has settled is the same answer.
		throw std::runtime_error("an explicit instantiation names " + written +
		                         ", which is no class template specialization");
	}
	return made;
}
