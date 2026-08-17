#include "sema_analyzer.h"

#include <stdexcept>
#include <utility>

#include "ast_model.h"
#include "ast_tokens.h"
#include "sema_access.h"
#include "sema_deduce.h"
#include "sema_operator.h"
#include "sema_pack.h"
#include "sema_pattern.h"
#include "sema_specialize.h"
#include "sema_template_head.h"
#include "token_model.h"

// Function templates, as far as one specialization of each.
//
// 14p1 makes a template a pattern rather than a declaration: the unit declares
// the functions its instantiations declare, and the output describes those.  So
// the layer has three steps, and each has one owner.  The template-argument
// list is read here, because 14.2 writes it inside a name and the name layer is
// what turns a spelling back into what was written.  Substitution is the type
// table's, because 14.3 binds an argument to a parameter and rebuilding a type
// with one replaced is a fact about types alone.  The specialization is the
// model's, because 14.7.1 makes it one declaration however many times it is
// named, which is the same interning a redeclaration asks for.
//
// A specialization is bound to no name.  It is reached from the template-id
// that wrote its arguments or from the call that deduced them, which is why
// ordinary lookup keeps finding the template and never a declaration the
// program did not write.
//
// PA19 adds the class tier on the same three steps.  14p1 makes a class
// template a pattern too, so the walk records what the template-declaration
// parameterises instead of reading it, and 14.7.1p1's instantiation is that
// same pattern read once more against a region that binds each parameter to
// its argument.  Nothing is substituted into syntax and no text is replayed:
// the bindings are typedef-names of the argument types, so every name the body
// writes is looked up with the arguments already in hand and the ordinary
// PA16-PA18 class machinery settles the specialization exactly as it settles a
// class the program wrote out.

namespace
{

}

SemaEntity& SemaAnalyzer::specialize(SemaEntity& primary,
                                     const std::vector<TypeId>& arguments)
{
	// 14.7.1p1: one specialization per template and argument list, however many
	// times it is named, so a second naming is a probe rather than a second
	// substitution.
	const std::uint32_t list = types_.type_list(arguments);
	SemaEntity* made = model_.specialization_of(primary, list);
	if (made == nullptr)
	{
		// 14.3p1: the arguments are bound to the parameters in order, and
		// 14.8.2 builds the declaration by substituting them into the type the
		// template-declaration made.
		std::unordered_map<TypeId, TypeId> bindings;
		const std::vector<SemaEntity*>& parameters =
			primary.template_parameters->declarations;
		// 14.5.3p1: the places take the arguments one for one, and the last
		// place takes the run of every argument the others did not - which is
		// what an earlier place binding a run wrote as one entry of its own.
		for (std::size_t index = 0; index < parameters.size(); ++index)
		{
			const TypeId took = place_argument(
				types_, arguments, index, parameters.size(),
				types_.is_template_pack(parameters[index]->type));
			if (took == kNoType)
			{
				continue;
			}
			bindings.insert(std::make_pair(parameters[index]->type, took));
		}
		std::unordered_map<TypeId, TypeId> memo;
		made = &model_.create(SemaKind::Function, primary.name,
		                      substituted(primary.type, bindings, memo));
		made->primary = &primary;
		made->region = primary.region;
		made->object_member = primary.object_member;
		// 11p1: the access a class gave a member was given to the *template*,
		// and this declaration is only what one argument list makes of it - so
		// the access a naming of it is refused by is the template's, which is
		// what the class tier and 14.5.7p1's alias template each already say.
		made->access = primary.access;
		// 14.7.1p1: the specialization is a declaration of the template's own
		// name, which is what the output calls it wherever it names a
		// declaration rather than repeating what a use wrote.
		made->dump_name = primary.dump_name;
		made->abi_name = primary.abi_name;
		// 14.5.6.1 and 15.4p13: the specialization's exception-specification is
		// the one the template's declarator wrote, and 5.3.7p3 asks about it
		// wherever a call names this declaration rather than the pattern.  It
		// is copied here because the pattern's declarator is read once, when
		// the template was declared, and this declaration is built from its
		// type and never from that declarator again.
		made->nonthrowing = primary.nonthrowing;
		made->wrote_exception_specification =
			primary.wrote_exception_specification;
		// 12.1p1 and 12.3.2p1 with 14.5.2p1: which special member of its class
		// the *template* declares is what one argument list makes a declaration
		// of - a constructor template's specialization is a constructor, which
		// is what says 12.6.2's mem-initializers are what a definition of it
		// runs and what the object file spells its entry with.
		made->special = primary.special;
		made->conversion_function = primary.conversion_function;
		// 12.3.1p2, 12.3.2p2 and 8.4.3p1: which initializations may choose this
		// declaration, and whether naming it is ill formed at all, are facts the
		// declarator wrote about the *template* - and no argument list changes
		// either - so they are what one argument list makes a declaration of.
		// 13.3 reads both off the declaration it chose, before any body of it
		// is instantiated, so a specialization that carried neither was an
		// implicit `A a = 3` through an `explicit` constructor template and a
		// call of a deleted one.
		made->explicit_function = primary.explicit_function;
		made->deleted = primary.deleted;
		// 14.2: the arguments that made it, which the object file writes after
		// the template's name and which no spelling of the declaration holds.
		made->template_arguments = list;
		model_.hold_specialization(primary, list, *made);
		// 13.5p6: a parameter written over a template parameter is of no type
		// until an argument list gives it one, so the clause is a question
		// about this declaration and not about the template that made it.  It
		// is asked where the declaration is made - once per argument list,
		// however many namings reach it - and the name says in one comparison
		// that nearly every specialization has nothing to answer.
		if (!made->object_member)
		{
			OperatorCall(*this).require_operand(
				primary.name, made->type,
				primary.region != nullptr &&
					primary.region->kind == ScopeKind::Class);
		}
	}
	return *made;
}

// 14.8.1p2: a template-id that writes fewer arguments than the head declared
// parameters, which names the template with those arguments already given and
// leaves the rest to the use.
//
// The written part is a fact of the name rather than of the use that reads it -
// a call deduces the rest from its arguments and 14.8.2.2's target type from a
// function type - so it is held on a declaration of its own, made once per
// template and written list, and every reader of an overload set finds it there.
SemaEntity& SemaAnalyzer::partial_template(SemaEntity& primary,
                                           const std::vector<TypeId>& arguments)
{
	const std::uint32_t list = types_.type_list(arguments);
	const std::uint64_t key =
		(static_cast<std::uint64_t>(primary.id) << 32) | list;
	SemaEntity*& held = partial_templates_[key];
	if (held == nullptr)
	{
		held = &model_.create(SemaKind::Function, primary.name, primary.type);
		held->partial_of = &primary;
		held->template_parameters = primary.template_parameters;
		held->region = primary.region;
		held->object_member = primary.object_member;
		// 11p1 as above: the list this declaration wrote is no part of what
		// the class said a naming of the member may reach.
		held->access = primary.access;
		held->dump_name = primary.dump_name;
		held->abi_name = primary.abi_name;
		held->template_arguments = list;
	}
	return *held;
}

SemaEntity* SemaAnalyzer::template_specializations(const std::string& spelling,
                                                   const Context& ctx,
                                                   std::vector<SemaEntity*>& found,
                                                   Scope* in)
{
	const QualifiedName written(spelling);
	const std::string component = written.last();
	const TemplateId id(component);
	if (!id.valid())
	{
		return nullptr;
	}
	// The template-id is the last component of the name, so what the
	// nested-name-specifier before it reaches is looked up as it is for any
	// other name.
	//
	// 5.2.5p1: a member named after `.` or `->` is looked up in the class of
	// the object expression instead, which is a region no spelling reaches and
	// the caller hands over.
	const std::string named = written.prefix() + id.name();
	SemaEntity* const primary =
		in != nullptr ? model_.lookup_in(*in, id.name(), LookupKind::Any)
		              : resolve(named, ctx, LookupKind::Any);
	if (primary == nullptr || primary->kind != SemaKind::Function)
	{
		return nullptr;
	}

	// 14.8.1p2 and 13.4p1: the argument list is written once and makes one
	// specialization of each declaration of the name it fits, which is still an
	// overload set for a target type or a call to choose from.  Each of them is a
	// declaration of its own that no region's chain holds, so the set the use
	// carries is what holds them.
	//
	// 14.1p4 leaves what an argument *is* a fact of the declaration it is bound
	// to: one spelling is a type-id at one head's place and a constant
	// expression at another's, so the list is read once per candidate.
	for (SemaEntity* at = primary; at != nullptr; at = at->next)
	{
		if (at->template_parameters == nullptr)
		{
			continue;
		}
		const std::vector<SemaEntity*>& places =
			at->template_parameters->declarations;
		// 14.5.3p1: a pack takes every argument the places before it did not,
		// so a list longer than the head fits only where the head declared one.
		const std::size_t fixed = function_pack_place(types_, places);
		const bool packed = fixed < places.size();
		if (!packed && places.size() < id.arguments().size())
		{
			continue;
		}
		std::vector<TypeId> arguments;
		arguments.reserve(id.arguments().size());
		for (std::size_t index = 0; index < id.arguments().size(); ++index)
		{
			const std::size_t what =
				arguments.size() < fixed ? arguments.size() : fixed;
			std::string pattern;
			if (written_pack_expansion(id.arguments()[index], pattern))
			{
				// 14.5.3p4: an expansion written in an explicit argument list
				// fills the places the run it stands for is long, which is how
				// `select<T...>` reaches a head of two fixed places.
				PackReading(*this).expand(
					pattern, ctx, TemplateHead(*this).place_type(places, what, arguments),
					arguments);
				continue;
			}
			if (what >= places.size())
			{
				break;
			}
			arguments.push_back(TemplateHead(*this).explicit_argument(places, what, arguments,
			                                      id.arguments()[index], ctx));
		}
		if (!packed && arguments.size() > places.size())
		{
			continue;
		}
		if (packed && fixed + 1 < places.size() && arguments.size() >= fixed)
		{
			// 14.8.1p2 with 14.1p11: a pack that is *not* the last place takes
			// every argument the list wrote past the places before it, and the
			// places after it are still the use's to deduce - so the run stands
			// as one entry of the list, which is what says where it ended.
			const std::vector<TypeId> run(arguments.begin() + fixed,
			                              arguments.end());
			arguments.resize(fixed);
			arguments.push_back(types_.pack_type(run));
			found.push_back(&partial_template(*at, arguments));
			continue;
		}
		// 14.8.1p2 with 14.5.3p1: a list that stopped at the pack place gave the
		// pack nothing, and a pack given nothing explicitly is still one a call
		// deduces a run for - so it is the list that *reached* the pack that
		// names a specialization outright.
		if (packed ? arguments.size() > fixed
		           : places.size() == arguments.size())
		{
			found.push_back(&specialize(*at, arguments));
			continue;
		}
		// 14.8.1p2: the trailing arguments the list left out are the use's
		// to deduce, so what this declaration contributes is a candidate
		// and not yet a specialization.
		found.push_back(&partial_template(*at, arguments));
	}
	if (found.empty())
	{
		throw std::runtime_error(spelling + " names no template its argument "
		                         "list fits");
	}
	return found[0];
}

namespace
{

// 14.7.1p1 and 14.7.3p1: whether this specialization has a definition to read -
// either the template's own pattern, or the body `template<>` wrote out for
// exactly this argument list.  A template that defines no pattern still defines
// the specializations its program wrote out, and those are definitions this
// unit owns.
bool has_written_definition(const SemaEntity& function)
{
	const SemaEntity& primary = *function.primary;
	const bool written = primary.templated != nullptr &&
		primary.templated->explicit_functions.find(function.template_arguments) !=
			primary.templated->explicit_functions.end();
	if (written)
	{
		return true;
	}
	// 14.7.3p1: a `template<>` declaration of exactly this argument list says the
	// specialization is not the pattern's to be read for - so a program that wrote
	// one and no body for it left the definition to another unit, and this one
	// writes the declaration.  The pattern's own body defines every *other* list.
	return primary.defined && !function.explicit_specialization;
}

}  // namespace

void SemaAnalyzer::instantiate(SemaEntity& function)
{
	if (function.instantiated)
	{
		return;
	}
	function.instantiated = true;
	if (has_written_definition(function))
	{
		instantiate_body(function);
		return;
	}
	// 14.6.4.1p1: a specialization named where its template has no definition
	// yet has a second point of instantiation at the end of the translation
	// unit, and that is where this one is settled - so what stands here is the
	// place the definition or the declaration will be written in, in the order
	// the specializations were asked for.
	Pending pending;
	pending.function = &function;
	pending.instantiation = true;
	pending_.push_back(pending);
}

// 14.7.1p1: instantiating a template that has a definition instantiates the
// definition, which is the body read again against the arguments rather than a
// declaration built from them.
void SemaAnalyzer::instantiate_body(SemaEntity& function)
{
	SemaEntity& primary = *function.primary;
	if (primary.templated == nullptr ||
	    (primary.templated->pattern == nullptr &&
	     primary.templated->explicit_functions.find(
		     function.template_arguments) ==
		     primary.templated->explicit_functions.end()))
	{
		throw std::runtime_error("a function template with a definition is "
		                         "instantiated, which this milestone does "
		                         "not describe");
	}
	const TemplateInfo& info = *primary.templated;
	// 14.7.3p1: the body an explicit specialization wrote is what this
	// declaration runs, in place of the pattern's.  It was written with the
	// arguments spelled out, so the bindings stand over it and say nothing.
	const std::unordered_map<std::uint32_t, const AstNode*>::const_iterator
		written = info.explicit_functions.find(function.template_arguments);
	const AstNode* const body = written != info.explicit_functions.end()
		? written->second
		: info.pattern;
	Context inner;
	inner.scope = &TemplateHead(*this).open_bindings(
		info, types_.type_list_at(function.template_arguments));
	inner.dump = info.dump;
	// 9.2p2 and 14.7.1p1: the definition is written where the output puts
	// one the program did not write - among the pending ones, at the end of
	// the unit - so it stands under no declaration's node.
	inner.node = nullptr;
	// 14.1p1: the bindings are what this reading has in place of the head the
	// definition was written under, so a qualified declarator-id reads its own
	// region with them standing over it exactly as the definition did.
	inner.template_head = inner.scope;
	// 14p1 and 3.2p3: every unit that names this specialization
	// instantiates the same definition from the same template, so the
	// definition is not this unit's to own - it binds the way an inline
	// one does and the program keeps one of them.
	//
	// 14.7.3p6 is the other way round for a body `template<>` wrote out: that
	// one is this unit's own source, so 3.2p3 makes it the program's one
	// definition, and whether it is `inline` is what its own decl-specifiers
	// say - which the reading of the definition below is what settles.
	function.inline_function = function.inline_function ||
		written == info.explicit_functions.end();
	SemaEntity* const enclosing = instantiating_;
	instantiating_ = &function;
	if (body->kind == AstKind::SpecialMemberDefinition ||
	    body->kind == AstKind::SpecialMemberDeclaration)
	{
		// 14.5.2p1 with 12.1p1: the pattern of a constructor template, or of
		// 12.3.2p1's conversion function template, is the syntax 12 writes a
		// member with no declared type in - so the reading that declares one is
		// the one that reads it, and 14.7.1p1's specialization is what it
		// gives the type and the body to.
		special_member(*body, inner);
	}
	else
	{
		function_definition(*body, inner);
	}
	instantiating_ = enclosing;
}

void SemaAnalyzer::write_instantiation(const Pending& pending)
{
	SemaEntity& asked = *pending.function;
	if (has_written_definition(asked))
	{
		// 14.6.4.1p1: the definition read here is the one the template had by
		// the end of the translation unit, which is a definition written after
		// the name that asked for this specialization as much as one written
		// before it.
		instantiate_body(asked);
		return;
	}
	const SemaEntity& function = asked;
	const SemaEntity& primary = *function.primary;
	// 14.7.1p1: the specialization stands for a declaration of the template's
	// own name, written with the types the arguments made of its parameters.
	DumpNode& line = model_.open_node(model_.unit(), "function-declaration " +
	                                  primary.dump_name + " " +
	                                  function_description(
		                                  function.type,
		                                  function.object_member));
	const std::unordered_map<std::uint32_t, std::vector<Parameter> >::const_iterator
		written = templates_.find(primary.id);
	const std::vector<TypeId>& parameters = types_.parameters(function.type);
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		// The names are the template's declarator's; the types are what the
		// substitution made of them.
		const bool named = written != templates_.end() &&
			index < written->second.size();
		model_.open_node(line, "parameter " +
		                 (named ? written->second[index].name : std::string()) +
		                 " " + types_.description(parameters[index]));
	}
}

// --- the class tier ------------------------------------------------------

namespace
{

// 9p1: which class-key a class-specifier or elaborated-type-specifier wrote.
ClassTag class_tag_of(const AstNode& node)
{
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind != AstKind::ClassKey)
		{
			continue;
		}
		if (child.token == KW_CLASS)
		{
			return ClassTag::Class;
		}
		return child.token == KW_UNION ? ClassTag::Union : ClassTag::Struct;
	}
	return ClassTag::Struct;
}

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

// 14.5.4p1 with 11.3p11: `template<class U> friend class W;` - a
// template-declaration whose declaration is a friend declaration with no
// declarator.  What the head parameterises is the elaborated-type-specifier the
// decl-specifier-seq wrote, so the class tier below reads that node and the
// grant is made to the template it settles on.  Null for every other
// declaration, including a friend one that writes a declarator: that one
// declares a function and 11.3p6 already owns it.
const AstNode* friend_class_declared(const AstNode& declared)
{
	if (declared.kind != AstKind::SimpleDeclaration ||
	    first_child(declared, AstKind::InitDeclaratorList) != nullptr)
	{
		return nullptr;
	}
	const AstNode* const specifiers =
		first_child(declared, AstKind::DeclSpecifierSeq);
	if (specifiers == nullptr)
	{
		return nullptr;
	}
	const AstNode* elaborated = nullptr;
	bool wrote_friend = false;
	for (std::size_t index = 0; index < specifiers->children.size(); ++index)
	{
		const AstNode& child = *specifiers->children[index];
		wrote_friend = wrote_friend || (child.kind == AstKind::DeclSpecifier &&
		                                child.token == KW_FRIEND);
		if (child.kind == AstKind::ClassForwardDeclaration)
		{
			elaborated = &child;
		}
	}
	return wrote_friend ? elaborated : nullptr;
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

// 14p1: records what a template-declaration parameterises rather than reading
// it.  A class template declares no class until 14.7.1p1 instantiates one, so
// the name the class-head wrote is bound in the region the template-declaration
// stands in - which is what a use of the template looks in - and the body is
// left as the syntax an instantiation reads.
bool SemaAnalyzer::record_template(const AstNode& node, const Context& ctx,
                                   bool member)
{
	const AstNode* clause = nullptr;
	const AstNode* declared = nullptr;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::TemplateParameterClause)
		{
			if (clause != nullptr)
			{
				// 14.5.2: a member template writes a second clause, which this
				// milestone leaves out.
				return false;
			}
			clause = &child;
			continue;
		}
		declared = &child;
	}
	if (clause == nullptr || declared == nullptr)
	{
		return false;
	}
	// 14.5.4p1 and 11.3p11: a friend declaration under a head names a class
	// *template* - the one the innermost enclosing namespace declares, or the
	// one declared there when it declares none yet - and grants it the reach
	// the class around the declaration has.  The class tier below is what
	// settles which template that is, so the elaborated-type-specifier stands
	// as this head's declaration and the region it is read against is the
	// namespace rather than the class.
	SemaEntity* granting = nullptr;
	if (const AstNode* const elaborated = friend_class_declared(*declared))
	{
		granting = Access(*this).granting_class(ctx);
		if (granting == nullptr)
		{
			throw std::runtime_error("a friend declaration is written outside a "
			                         "class definition");
		}
		declared = elaborated;
	}
	else if (!lowering() && declared->kind != AstKind::ClassSpecifier &&
	         declared->kind != AstKind::ClassForwardDeclaration &&
	         declared->kind != AstKind::AliasDeclaration)
	{
		// 14.6p8's reading of a class template's own definition is the PA11
		// dialect, which describes what the declarations it holds *say* and
		// declares nothing this unit has - so a head over a function or an
		// object records no template there and 14.1p1's own reading takes it.
		// 14.5.2p1's member *class* template is the exception, and 14.5.4p1's
		// friend above is another: what each declares is a class the names the
		// pattern writes have to reach - `outer<T>::inner` is looked up in the
		// current instantiation exactly as `outer<int>::inner` is in the class
		// one argument list makes - so the reading that leaves it undeclared
		// leaves 14.5.1.3p1's out-of-class definition of it naming nothing.
		//
		// 14.5.7p1's member alias template is the third: 7.1.3p2 makes the name
		// it declares a *template-name*, so `A<T>` written beside it in the same
		// body is a template-id the reading has to look `A` up for - and
		// 14.1p1's own reading declares a typedef-name of the type-id read on
		// the spot, which is a type the places have not settled and a name no
		// template-id reaches.
		return false;
	}
	if (first_child(*clause, AstKind::TemplateParameterList) == nullptr &&
	    record_explicit_specialization(*declared, ctx))
	{
		// 14.7.3p1: a head that declares no parameters parameterises nothing.
		// What it wrote is a declaration of the specialization itself, which
		// the template's own table answers for before any instantiation of the
		// pattern is made.
		return true;
	}
	// 14.5.2p3: an out-of-class definition of a *member template* writes one
	// head per class the member is nested in and then the member's own, so the
	// declarator-id whose nested-name-specifier names the owner is the innermost
	// declaration's.  The heads after this one parameterise that declaration and
	// are read where the arguments this one takes already stand, so the whole
	// nest travels as the member's pattern.
	const AstNode* innermost = declared;
	while (innermost->kind == AstKind::TemplateDeclaration &&
	       !innermost->children.empty())
	{
		innermost = innermost->children[innermost->children.size() - 1];
	}
	// 14.5.1.3p1: a definition written outside its class belongs to the
	// template that class is one of, and is read for a specialization after
	// the body - including for one the unit already made.  A class-head-name
	// with a nested-name-specifier is one of those, so the question is asked
	// before the class tier's.
	// 14.5.2p3: a head standing under one whose arguments already bind writes
	// no out-of-class definition of *this* template - the reading that opened
	// those bindings is what is reading this declaration, and the class its
	// declarator-id names is one that reading already settled.
	std::string wrote;
	SemaEntity* const owner =
		member ? nullptr : PatternReading(*this).owner(*innermost, ctx, &wrote);
	if (owner != nullptr)
	{
		// 14.5.5p1: the template that component named may hold several bodies,
		// and the arguments the declarator-id wrote say which of them this
		// definition is a member of - the primary's where they are the
		// template's own places, and a pattern's where they are that pattern.
		PatternReading(*this).record(
			*owner,
			Specialization(*this).member_pattern(*owner, wrote, *clause, ctx),
			*clause, *declared,
			ctx.scope->kind == ScopeKind::TemplateParameters ? ctx.scope
			                                                 : nullptr);
		return true;
	}
	// 14.5.5p1, 14.5.1p1 and 14.5.7p1: a head whose declaration writes an
	// argument *pattern* declares no template of that spelling but a second body
	// for the one it names, a head over an object declares a variable template,
	// and a head over an alias-declaration declares an alias template.  All
	// three are asked before the primary tier, because what tells them apart
	// from it is the declaration the head parameterises alone.
	if (Specialization(*this).record(*clause, *declared, ctx) ||
	    Specialization(*this).record_alias(*clause, *declared, ctx))
	{
		return true;
	}
	if (declared->kind != AstKind::ClassSpecifier &&
	    declared->kind != AstKind::ClassForwardDeclaration)
	{
		return false;
	}
	const QualifiedName spelled(declared->text);
	if (declared->text.empty())
	{
		return false;
	}
	const std::string name = spelled.last();
	const bool define = declared->kind == AstKind::ClassSpecifier;
	// 9.4.2p1 and 3.4.1p8: a class-head-name with a nested-name-specifier names
	// the template the region that name reaches already declared, wherever the
	// definition is written - so the declaration this head parameterises is
	// found there rather than made here, and 14.1p2's own parameter names stand
	// over that region for as long as the body is read, which the pattern's own
	// region already says.
	Context target = ctx;
	if (granting != nullptr && !spelled.qualified())
	{
		// 11.3p11 and 3.3.2p6: the class an *unqualified* friend
		// elaborated-type-specifier names belongs to the innermost enclosing
		// namespace and is no member of the class the declaration is written
		// in - which is where the non-template friend path already declares
		// one.  A qualified one names the template that region already
		// declares, exactly as every other qualified class-head-name does.
		target.scope = &friend_namespace(*ctx.scope);
		target.dump = target.scope->dump;
	}
	else if (spelled.qualified())
	{
		target.scope = resolve_prefix(spelled, ctx);
		target.dump = target.scope->dump;
	}

	// 9.1p2 and 14p1: a second template-declaration of one name declares the
	// same template, and the one that wrote a body is what an instantiation
	// reads.
	SemaEntity* entity =
		spelled.qualified()
			? model_.lookup_in(*target.scope, name, LookupKind::Type)
			: model_.find(*target.scope, name, LookupKind::Type);
	if (entity != nullptr &&
	    (entity->kind != SemaKind::Class || entity->templated == nullptr))
	{
		throw std::runtime_error("a template declaration of " + name +
		                         " redeclares a name that is not a class "
		                         "template");
	}
	if (entity == nullptr && spelled.qualified())
	{
		// 9.4.2p1: the region the nested-name-specifier reaches declares no
		// template of this name, so this head parameterises nothing there.
		throw std::runtime_error("a template definition of " + declared->text +
		                         " names no class template that region "
		                         "declares");
	}
	const bool first = entity == nullptr;
	if (entity == nullptr)
	{
		const std::uint32_t id = model_.type_entity_id();
		// The template itself names no type an object can be made of; the
		// spelling is what a diagnostic and a specialization's name are built
		// from.
		const TypeId type = types_.class_type(
			id, class_tag_of(*declared), dump_name(*target.scope, name),
			abi_name(*target.scope, name));
		entity = &model_.create(SemaKind::Class, name, type);
		own_type(type, *entity);
		// 14.6.1p6 and 3.3.10p2: a class template's name is a class-name bound
		// in a region like any other, and the region a template-declaration is
		// written in is inside the scope of the parameters its own head
		// declared.
		declare_type_name(name, *target.scope);
		model_.bind(*target.scope, name, *entity);
		model_.declare_in(*target.scope, *entity);
		template_patterns_.push_back(TemplateInfo());
		entity->templated = &template_patterns_.back();
		entity->templated->region = target.scope;
		entity->templated->dump = target.dump;
		TemplateHead(*this).read(*clause, *entity->templated);
		// 14.1p1 and 14.6.1p6: the head's parameters are declared in a region
		// enclosing this declaration, and this declaration's own name stands
		// inside it - but the region is opened after the name is bound and a
		// class template's is never opened at all, so the one name
		// `require_no_template_parameter` cannot reach through the regions is
		// asked of the head that declared them.
		for (std::size_t index = 0;
		     index < entity->templated->parameters.size(); ++index)
		{
			if (entity->templated->parameters[index].name == name)
			{
				throw std::runtime_error(name + " redeclares a template "
				                         "parameter within the scope of the "
				                         "template head that declared it");
			}
		}
	}
	else if (define && entity->templated->pattern != nullptr &&
	         entity->templated->pattern->kind == AstKind::ClassSpecifier)
	{
		throw std::runtime_error("a class template is defined twice");
	}
	if (!first)
	{
		// 14.1p10: the defaults available to a use are the ones the definition
		// and every declaration in scope wrote, merged - so a head that adds a
		// default to a place an earlier one left empty is read for that alone,
		// whether or not it is the one that writes the body.  14.1p2 leaves
		// each head spelling the places as it likes, so what the merged default
		// names them by travels with it.
		TemplateInfo head;
		// 14.1p1: the region this head's own places would be bound in is the
		// one the declaration stands in, which is what lets 14.5.6.1p5's
		// comparison below read what a value place of it names a value of.
		head.region = target.scope;
		head.dump = target.dump;
		TemplateHead(*this).read(*clause, head);
		if (head.parameters.size() != entity->templated->parameters.size())
		{
			throw std::runtime_error("two declarations of the class template " +
			                         name + " write different numbers of "
			                         "template parameters");
		}
		// 14.5.6.1p5: every declaration of one template writes equivalent
		// template-parameter-lists.  A second head that declares the same number
		// of places and not the same *places* - a value place where the first
		// wrote a type place, or a template place whose own head accepts other
		// arguments - redeclares nothing this name can stand for, and 14.1p2's
		// merge of the defaults below would be reading one head's positions off
		// another's.
		if (!TemplateHead(*this).heads_equivalent(*entity->templated, head))
		{
			throw std::runtime_error("two declarations of the class template " +
			                         name + " write template parameters that are "
			                         "not equivalent");
		}
		if (define)
		{
			// 14.1p2: the parameter names of the definition are the ones its
			// body wrote, whatever an earlier declaration called them.  What a
			// place *is* does not change with the spelling, so a region an
			// earlier naming already opened keeps the types it settled and
			// takes the new names beside the ones it was bound under.
			TemplateHead(*this).rename_parameters(*entity->templated, head.parameters);
		}
		entity->templated->supported =
			entity->templated->supported && head.supported;
		for (std::size_t index = 0; index < head.defaults.size(); ++index)
		{
			if (head.defaults[index].written != nullptr)
			{
				entity->templated->defaults[index] = head.defaults[index];
			}
		}
	}
	if (define)
	{
		entity->templated->pattern = declared;
		// 14.5.1.3p1 and 14.1p2: a member class template defined outside its
		// class is read under a head per class it is nested in, and 14.1p2 lets
		// this definition spell those classes' places with names of its own -
		// which the head above this one bound and the class binds nowhere.  So
		// 14.7.1p1's reading of the body opens its bindings there.  Every
		// definition written where its declaration is made stands in a class or
		// a namespace and records none.
		entity->templated->reading_region =
			ctx.scope->kind == ScopeKind::TemplateParameters ? ctx.scope
			                                                 : nullptr;
		// 14.6p8: the definition is read once where it stands, before any
		// specialization is completed from it, because what it says about the
		// names no template parameter stands in the way of is a fact about the
		// definition rather than about any argument list.
		PatternReading(*this).read_class(*entity);
		// 14.7.1p1: a specialization the unit named before the definition
		// arrived is an incomplete class until here, and the definition is
		// what completes it.
		for (std::size_t index = 0;
		     index < entity->templated->specializations.size(); ++index)
		{
			complete_specialization(
				*entity->templated->specializations[index]);
		}
	}
	if (granting != nullptr)
	{
		// 11.3p1 with 14.5.4p1: the grant is between the two templates, and
		// every specialization either of them makes carries the pattern it came
		// from - so one entry answers `friend class W;` for every `W<A...>`
		// this unit goes on to make, and nothing is re-recorded per argument
		// list.
		model_.befriend(*granting, *entity);
		return true;
	}
	// The dump names the template where it was written, as the declaration it
	// parameterises spells it.
	ctx.dump->lines.push_back("type " + name + " " +
	                          (class_tag_of(*declared) == ClassTag::Union
	                               ? "union "
	                               : (class_tag_of(*declared) == ClassTag::Class
	                                      ? "class "
	                                      : "struct ")) +
	                          name);
	return true;
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
		return false;
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
		info.explicit_classes[list] = &declared;
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
			->explicit_functions[chosen->template_arguments] = &declared;
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

// 9.1p2: the declaration a type was made by, recorded where the declaration is
// read.
//
// The model answers it for a walk that already has the type in hand.  The type
// table carries it because an object-file name encoded from a *use* of the
// type - a parameter, a template argument, the type a table is named after -
// has only the type, and the one spelling the type carries cannot say which of
// its components a template made.
void SemaAnalyzer::own_type(TypeId type, SemaEntity& entity)
{
	model_.own_type(type, entity);
	types_.set_declaration(type, &entity);
}

// 14.7.1p1: the class `arguments` makes of the class template `primary`.
//
// The specialization is one declaration however many times it is named, so the
// pattern is read once, against a region binding each parameter to the type its
// argument named.  It is held before the body is read: a class template whose
// body names its own specialization - a member of it, a pointer to it - has to
// find the declaration already made rather than start a second reading of it.
SemaEntity& SemaAnalyzer::instantiate_class(SemaEntity& primary,
                                            const std::vector<TypeId>& arguments)
{
	const std::uint32_t list = types_.type_list(arguments);
	SemaEntity* made = model_.specialization_of(primary, list);
	if (made != nullptr)
	{
		// 14.7.1p1: the declaration is made once, and the definition is owed
		// wherever the specialization is used in a way that requires it - so a
		// naming that left it a declaration does not keep the next one from
		// asking for it.
		if (checking_ == 0)
		{
			asked_specialization(*made);
		}
		return *made;
	}
	const TemplateInfo& info = *primary.templated;
	// 14.7.1p1: the spelling a specialization is named by, which the LowIR
	// symbol for a global it declares is built from.  A comma is written the
	// way a program writes one - with the space after it - because that
	// spelling is what a global name is compared between two compilers by, and
	// only a function's is masked before they are compared.
	std::string spelled = primary.name + "<";
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (index != 0)
		{
			spelled += ", ";
		}
		spelled += type_spelling(arguments[index]);
	}
	spelled += ">";

	const ClassTag tag = info.pattern != nullptr
		? class_tag_of(*info.pattern)
		: types_.class_tag(primary.type);
	const std::uint32_t id = model_.type_entity_id();
	const std::string qualified = abi_name(*info.region, spelled);
	const TypeId type = types_.class_type(id, tag,
	                                      dump_name(*info.region, spelled),
	                                      qualified);
	made = &model_.create(SemaKind::Class, spelled, type);
	made->primary = &primary;
	made->template_arguments = list;
	made->region = info.region;
	made->access = primary.access;
	own_type(type, *made);
	// The ABI writes the template's own name and then the arguments, which the
	// one spelling above cannot be split back into.
	// 14.5.3p1: and where its head declared a pack, the place its run begins
	// at - because the list above is one flat list and the name the object
	// file writes has to tell the run apart from the places before it.
	const std::size_t packed = pack_place(info);
	types_.set_template_arguments(
		type, abi_name(*info.region, primary.name), arguments,
		packed < info.parameters.size() ? static_cast<unsigned>(packed)
		                                : TypeTable::kNoPackPlace);
	model_.hold_specialization(primary, list, *made);
	if (checking_ == 0)
	{
		// 14.6p8 and 14.7.1p1: a name written in a template definition being
		// read where it stands is not a use that requires a definition; the
		// specialization is declared here and asked for where an instantiation
		// does.
		asked_specialization(*made);
	}
	return *made;
}

// 14.7.1p1: what naming a specialization comes to where the name stands.
//
// A class template specialization is implicitly instantiated where it is used
// in a context that requires a completely-defined object type **and nowhere
// else** - so writing the name is never that context.  7.1.3p1's typedef-name,
// 7.1.3p2's alias-declaration, 8.3.5p6's parameter and return type of a
// function nobody is defining, a pointer, a reference, a template argument, a
// cast's type-id and a condition that declares one are each a name and no
// demand, and telling them apart by where the walk happens to stand asks the
// question once per spelling the grammar has.  So the name marks the
// specialization as owing a definition, however many times it is written, and
// `require_complete_type` is the one demand every context that *does* require
// one makes of it - 3.9p5's list, read at the places this analysis already
// asks whether a class is complete.
void SemaAnalyzer::asked_specialization(SemaEntity& made)
{
	if (made.defined || made.declared_only)
	{
		return;
	}
	made.declared_only = true;
	++declared_only_;
}

// The demand itself, asked wherever 3.9p5 requires a complete type.  A unit
// that named no specialization pays one integer test here.
void SemaAnalyzer::require_complete_type(TypeId type)
{
	if (declared_only_ == 0 || checking_ > 0)
	{
		// 14.6p8: a definition read where it stands is no use of anything it
		// names, so the reading asks for no definition - which is the same
		// answer `instantiate_class` gives a template-id written there.  A
		// reading that completed one would read the pattern in the checking
		// dialect and leave a class with none of 12.1's members it is owed.
		return;
	}
	TypeId bare = types_.strip_cv(type);
	while (types_.kind(bare) == TypeKind::Array)
	{
		// 3.9p5: an array of an incomplete class is one too, and 12.6p1 builds
		// each of its elements.
		bare = types_.strip_cv(types_.target(bare));
	}
	if (types_.kind(bare) != TypeKind::Class)
	{
		return;
	}
	SemaEntity* const owner = model_.type_owner(bare);
	if (owner == nullptr || !owner->declared_only)
	{
		return;
	}
	owner->declared_only = false;
	--declared_only_;
	require_specialization(*owner);
}

// 10p1 and 14.6p8: a type a reading that asked for nothing requires to be
// complete anyway, which is one no argument list can still change.
//
// 14.6p8's reading asks for no definition, because a name written in a template
// definition is no use of anything; but a base class an argument list has
// already settled is a class the definition itself is laid out over, and the
// clause requires it complete where it stands.  So the reading is put aside for
// the demand - the specialization is instantiated as an instantiation makes it,
// with 12.1's members and its own layout - and taken up again after.
//
// `require_complete_type` cannot answer for such a type, and that is the whole
// difference between the two: its own demand reads the mark `instantiate_class`
// writes where a naming *was* a use, and no reading that asks for nothing wrote
// one.  So the question is asked of the type rather than of the mark, at every
// depth and not only inside 14.6p8's dialect - `sema_value_expression.cpp`
// probes a type-id in a reading of its own and keeps the answer, which leaves a
// specialization named by `sizeof`'s operand in exactly that state.
void SemaAnalyzer::require_settled_type(TypeId type)
{
	TypeId bare = types_.strip_cv(type);
	while (types_.kind(bare) == TypeKind::Array)
	{
		bare = types_.strip_cv(types_.target(bare));
	}
	SemaEntity* const owner =
			types_.kind(bare) == TypeKind::Class && !types_.is_dependent(bare)
		? model_.type_owner(bare)
		: nullptr;
	if (owner == nullptr || owner->primary == nullptr || owner->defined)
	{
		require_complete_type(type);
		return;
	}
	// 14.6p8's reading is put aside whole: the depth *and* the dialect it reads
	// in, because a specialization completed in the checking dialect would be
	// left with none of 12.1's members it is owed.
	const unsigned held = checking_;
	const SemaDialect spoken = dialect_;
	checking_ = 0;
	dialect_ = unit_dialect_;
	if (owner->declared_only)
	{
		owner->declared_only = false;
		--declared_only_;
	}
	try
	{
		require_specialization(*owner);
	}
	catch (...)
	{
		checking_ = held;
		dialect_ = spoken;
		throw;
	}
	checking_ = held;
	dialect_ = spoken;
}

// 14.7.1p1: an instantiation asks for this specialization.
//
// The declaration is made once however many times the name is written, and
// 14.6p8's reading of a template definition writes one of those names without
// asking for anything - so the specialization is made there for the reading to
// have a type, and joins the template's own list of them only when a use
// arrives.  That list is what the two readings a *later* declaration owes are
// driven by: 14.5.1.3p1's out-of-class member definition is read for every
// specialization already made, and so is the definition the template itself
// gets.  A specialization no instantiation ever asked for is on neither, which
// is what makes 14.6p8's reading leave nothing behind.
//
// 14.7.1p1 also leaves the specialization exists whether or not the template
// has a definition yet: a template the unit only declared makes an incomplete
// class, which is all a pointer or a reference to it needs, and the definition
// completes it where it arrives.
void SemaAnalyzer::require_specialization(SemaEntity& made)
{
	if (!made.instantiated)
	{
		made.instantiated = true;
		made.primary->templated->specializations.push_back(&made);
	}
	complete_specialization(made);
}

void SemaAnalyzer::complete_specialization(SemaEntity& made)
{
	const TemplateInfo& info = *made.primary->templated;
	// 14.6.1p1: the current instantiation is the one specialization over
	// dependent arguments that *is* read, because it is what the template's own
	// definition declares - so the reading that made it is what completes it.
	// 14.5.5p1 leaves one such class per body a naming may be read from, and
	// which of them this is, the argument list the class already carries says.
	const std::unordered_map<std::uint32_t, std::size_t>::const_iterator own =
		info.patterns.find(made.template_arguments);
	const std::size_t wrote = own != info.patterns.end() &&
			info.partials[own->second].current == &made
		? own->second : Specialization::kNoPartial;
	const bool pattern =
		&made == info.current || wrote != Specialization::kNoPartial;
	// 14.6.1p1 and 14.1p2: the head whose places this body writes over, which
	// for a pattern is its own and not the primary's.
	const TemplateInfo& head =
		wrote == Specialization::kNoPartial ? info : *info.partials[wrote].head;
	// 14.7.3p1: an explicit specialization of this argument list is what the
	// class *is*, so it is read in place of the template's pattern and against
	// no bindings at all: the body was written with the arguments spelled out.
	const std::unordered_map<std::uint32_t, const AstNode*>::const_iterator
		written = info.explicit_classes.find(made.template_arguments);
	const bool specialized =
		!pattern && written != info.explicit_classes.end();
	// 14.5.5.1p1: where no explicit specialization wrote this list out, the
	// class is read from whichever partial specialization's pattern the list
	// matches - and from the primary's own pattern where it matches none.
	std::vector<TypeId> deduced;
	const std::size_t partial =
		made.defined || pattern || specialized || info.partials.empty() ||
				types_.is_dependent(made.type)
			? Specialization::kNoPartial
			: Specialization(*this).chosen(
				  *made.primary, types_.type_list_at(made.template_arguments),
				  deduced);
	const bool matched = partial != Specialization::kNoPartial;
	const AstNode* const body = specialized ? written->second
		: (wrote != Specialization::kNoPartial ? info.partials[wrote].body
		   : (matched ? info.partials[partial].body : info.pattern));
	if (made.defined || body == nullptr ||
	    body->kind != AstKind::ClassSpecifier ||
	    (!pattern && !specialized && types_.is_dependent(made.type)))
	{
		// 14.6.2p1: a template-id written inside a template over that
		// template's own parameters names no class yet.  It is a declaration
		// of one - which is what deduction matches and what a reference or a
		// pointer to it needs - and the specialization the arguments make of
		// it is completed where they are known.
		return;
	}
	Context inner;
	inner.scope = specialized
		? info.region
		: (pattern ? head.parameter_region
		           : &TemplateHead(*this).open_bindings(
				         matched ? *info.partials[partial].head : info,
				         matched ? deduced
				                 : types_.type_list_at(made.template_arguments)));
	inner.dump = pattern ? head.reading_dump : info.dump;
	inner.node = nullptr;
	Span span;
	span.begin = body->begin;
	span.end = body->end;
	const std::string spelled = made.name;
	// 9.2p2: a member function body written in a class body is read where the
	// class is complete, so the bodies this reading holds are read once the
	// class-specifier has closed rather than where each stands.
	const std::size_t mark = held_bodies_.size();
	{
		// 14.7.1p1: the declarations this reading makes are the instantiation's
		// and so is every definition it writes, so each of the latter waits for
		// the use that names it.  A reading standing inside another - a body
		// naming a second specialization - is the same, which is why the fact
		// is a depth rather than a flag.
		const ReadingDepth instantiating(instantiating_class_);
		// 14.7.3p1: the body `template<>` wrote is this unit's own source, so the
		// definitions it holds are the program's and a second one of any of them is
		// 3.2p1's redefinition.  Only the pattern's reading makes a definition a
		// later written one replaces.
		const ReadingDepth reading(instantiating_pattern_, !specialized);
		class_declaration(*body, inner, span, true, std::string(), &made,
		                  &spelled);
		read_held_pattern_bodies(mark);
	}
	// 14.5.1.3p1: what the template's members were defined as outside its
	// class is read now that the class is complete, and a definition written
	// after this specialization was made is read for it where it stands.  The
	// current instantiation has none of them yet: a definition outside the
	// class can only be written after it, so `record_template` is where each
	// one's own reading stands.
	//
	// 14.5.5p1: the definitions this class holds are the ones written over the
	// body it was read from - the primary's where the list matched no pattern,
	// and that pattern's where it matched one.
	const std::vector<TemplateInfo::Member>& outside =
		matched ? info.partials[partial].members : info.members;
	for (std::size_t index = 0;
	     !pattern && !specialized && index < outside.size(); ++index)
	{
		// 14.7.3p1: a member the *template* defined outside its class is no
		// member of a specialization the program wrote out for itself.
		PatternReading(*this).instantiate(made, outside[index], partial);
	}
	// 10.3p10's table names a virtual member whatever it is defined by, so the
	// demand stands after the definitions written outside the class have been
	// read and not before them.
	require_table_definitions(made);
}

// 14.2: the specialization a name written as a template-id denotes.
//
// Ordinary lookup finds the template, because that is the name the program
// declared; the arguments the id wrote are what turns it into a declaration.
// Null for a name that is no template-id and for one whose template this
// milestone does not instantiate, so every caller falls back to what the
// earlier assignments did with it.
SemaEntity* SemaAnalyzer::template_id_entity(const std::string& component,
                                             const Context& ctx, Scope* in,
                                             LookupKind filter)
{
	if (!templating() || component.find('<') == std::string::npos)
	{
		return nullptr;
	}
	const TemplateId id(component);
	if (!id.valid())
	{
		return nullptr;
	}
	SemaEntity* primary =
		in != nullptr ? model_.lookup_in(*in, id.name(), LookupKind::Type)
		              : model_.lookup(*ctx.scope, id.name(), LookupKind::Type);
	if (primary != nullptr && types_.is_parameter_template(primary->type))
	{
		// 14.6.2p1: `C<A…>` where `C` is still the place its own head declared
		// names whatever the template an argument list binds to `C` makes of
		// the list, which is a type only that list settles.
		const TemplateInfo* const head = place_head(primary->type);
		std::vector<TypeId> arguments;
		arguments.reserve(id.arguments().size());
		for (std::size_t index = 0; index < id.arguments().size(); ++index)
		{
			std::string pattern;
			if (written_pack_expansion(id.arguments()[index], pattern))
			{
				PackReading(*this).expand(pattern, ctx, kNoType, arguments);
				continue;
			}
			arguments.push_back(
				head != nullptr
					? TemplateHead(*this).bound_argument(*head, arguments.size(),
					                                     id.arguments()[index],
					                                     arguments, ctx)
					: template_argument_type(id.arguments()[index], ctx));
		}
		return &dependent_template_name(primary->type, arguments, component);
	}
	if (primary != nullptr && primary->kind == SemaKind::Typedef &&
	    primary->templated != nullptr)
	{
		// 7.1.3p2: an alias template names no declaration of its own, so what
		// the id stands for is the type its arguments substitute into the
		// type-id the alias was declared with.
		return &Specialization(*this).alias(*primary, id, ctx);
	}
	if (primary != nullptr && primary->templated == nullptr &&
	    primary->primary != nullptr)
	{
		// 14.6.1p1: inside a specialization the injected-class-name is bound to
		// that specialization, and a template-argument-list written after it
		// names the template it was made of rather than the class it found.
		primary = primary->primary;
	}
	if (primary == nullptr || primary->kind != SemaKind::Class ||
	    primary->templated == nullptr)
	{
		return variable_template_entity(id, ctx, in, filter);
	}
	std::vector<TypeId> arguments;
	TemplateHead(*this).bind_arguments(*primary, id.arguments(), ctx, arguments);
	return &instantiate_class(*primary, arguments);
}

TemplateInfo* SemaAnalyzer::place_head(TypeId parameter) const
{
	const std::unordered_map<TypeId, TemplateInfo*>::const_iterator held =
		place_heads_.find(parameter);
	return held == place_heads_.end() ? nullptr : held->second;
}

// 14.6.2p1: the declaration `C<A…>` stands for while `C` is a place no argument
// list has settled.
//
// It is 3.4's answer to one spelling, so it is made once per place and argument
// list: a pattern that writes `C<T>` twice writes one type, and the
// substitution below runs once for it however many namings reached it.  A
// substitution that leaves the place a place still arrives here too, so the two
// readings of one naming - the one the pattern wrote and the one a partly
// settled list left - are one type and not two that spell the same.
SemaEntity& SemaAnalyzer::dependent_template_name(
	TypeId parameter, const std::vector<TypeId>& arguments,
	const std::string& spelling)
{
	const std::uint64_t key = (static_cast<std::uint64_t>(parameter) << 32) |
		types_.type_list(arguments);
	const std::unordered_map<std::uint64_t, SemaEntity*>::const_iterator held =
		dependent_templates_.find(key);
	if (held != dependent_templates_.end())
	{
		return *held->second;
	}
	const TypeId type = types_.dependent_template_id(
		model_.type_entity_id(), parameter, arguments, spelling);
	SemaEntity& entity = model_.create(SemaKind::Typedef, spelling, type);
	own_type(type, entity);
	dependent_templates_.insert(std::make_pair(key, &entity));
	return entity;
}

// 14.5.1p1: the specialization a template-id written over a variable template
// denotes, which is a constant rather than a type.
//
// The name it is written from is bound as an ordinary declaration and not as a
// type-name, so the lookup above - which asks for a type, because that is what
// nearly every template-id names - reaches nothing.  This is the second ask,
// made only where the context accepts a declaration of any kind at all: a
// nested-name-specifier and an elaborated-type-specifier each name a type, and
// an object never stands there.
SemaEntity* SemaAnalyzer::variable_template_entity(const TemplateId& id,
                                                   const Context& ctx, Scope* in,
                                                   LookupKind filter)
{
	if (filter != LookupKind::Any)
	{
		return nullptr;
	}
	SemaEntity* const primary =
		in != nullptr ? model_.lookup_in(*in, id.name(), LookupKind::Any)
		              : model_.lookup(*ctx.scope, id.name(), LookupKind::Any);
	if (primary == nullptr || primary->kind != SemaKind::Variable ||
	    primary->templated == nullptr)
	{
		return nullptr;
	}
	std::vector<TypeId> arguments;
	TemplateHead(*this).bind_arguments(*primary, id.arguments(), ctx, arguments);
	return &Specialization(*this).variable(*primary, arguments, ctx);
}

namespace
{

// 14.7.2p8: an explicit instantiation of a class template specialization is an
// explicit instantiation of each of its members, and of theirs in turn, so the
// walk is over the region the specialization opened rather than over one list
// of declarations.  A member a base class declares is left out, which the
// region already answers: 10.2p2's lookup reaches those through the base and
// the base's own region is what holds them.
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
	if (made->kind != SemaKind::Function || !owed)
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
	if (made == nullptr || !owed)
	{
		// 14.7.2p9: `extern template` says another unit owes the definitions,
		// which is what a specialization no use of this unit names already
		// leaves - so p2 above is the whole of what its form asks.
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

// 14p1: the pattern a function template's declaration was written from, taken
// from the walk that is reading the template-declaration.
//
// A function template's name is declared in the region around its parameter
// clause, so the declaration is made by the ordinary path and this only hands
// it what 14.7.1p1 needs to read the same syntax again: the syntax, the region
// its names are looked up from, and the parameters its head declared.
void SemaAnalyzer::record_function_template(SemaEntity& entity,
                                            Scope& parameters, Scope& region,
                                            Scope* reading)
{
	if (!lowering() || template_pattern_ == nullptr)
	{
		return;
	}
	// 14.7.3p1: a template that declares no pattern still has the
	// specializations `template<>` wrote out for it, and those are definitions
	// this unit owns - so the record is made for a declaration too, and the
	// definition read later is the pattern it had none of.
	// 14.5.2p1 with 12.1p1: a constructor template's pattern is the syntax 12
	// writes a constructor with, which reaches no function-definition - and
	// 12.3.2p1's conversion function template is written the same way.
	const AstNode* const pattern =
		template_pattern_->kind == AstKind::FunctionDefinition ||
		template_pattern_->kind == AstKind::SpecialMemberDefinition
			? template_pattern_
			: nullptr;
	if (entity.templated != nullptr && (pattern == nullptr ||
	                                    entity.templated->pattern != nullptr))
	{
		// The record already stands, and this declaration is not the definition
		// it was waiting for.
		return;
	}
	if (entity.templated == nullptr)
	{
		template_patterns_.push_back(TemplateInfo());
		entity.templated = &template_patterns_.back();
	}
	TemplateInfo& info = *entity.templated;
	// 14.5.6.1p5: an earlier declaration of this same template spelled its
	// parameters with names of its own, and what an instantiation reads is
	// *this* definition's syntax - so the head the record carries is this one's
	// and the specializations already written out for the template stay.
	info.parameters.clear();
	info.defaults.clear();
	info.supported = true;
	info.pattern = pattern;
	info.region = &region;
	info.reading_region = reading;
	info.dump = template_pattern_dump_;
	for (std::size_t index = 0; index < parameters.declarations.size(); ++index)
	{
		const SemaEntity& parameter = *parameters.declarations[index];
		// 14.1p2: a parameter this milestone does not bind leaves the pattern
		// unreadable, which an instantiation of it - and nothing else - finds.
		info.supported = info.supported &&
			(parameter.kind == SemaKind::TemplateType ||
			 parameter.kind == SemaKind::TemplateValue);
		// 14.1p4: a function template's head is read by the ordinary
		// declaration path, so the place already carries what it stands for and
		// the type of the value it names.
		TemplateInfo::Parameter place;
		place.name = parameter.name;
		place.self = parameter.type;
		place.value = parameter.kind == SemaKind::TemplateValue;
		place.pack = types_.is_template_pack(parameter.type);
		place.type = types_.parameter_value_type(parameter.type);
		info.parameters.push_back(place);
		info.defaults.push_back(TemplateInfo::Default());
	}
}

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

TypeId SemaAnalyzer::template_signature(const Scope& parameters, TypeId type)
{
	const std::vector<SemaEntity*>& declared = parameters.declarations;
	std::unordered_map<TypeId, TypeId> bindings;
	std::unordered_map<TypeId, TypeId> memo;
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
		const bool pack = types_.is_template_pack(place.type);
		if (place.kind == SemaKind::TemplateType)
		{
			bindings.insert(std::make_pair(
				place.type, signatures_.place(types_, model_, index, pack)));
			continue;
		}
		// 14.1p4: the type a value place binds a value of is written over the
		// places before it, so it is canonicalized with the bindings this walk
		// has made so far - which are exactly those places, and no place after
		// this one can stand in it.  That is what lets one memo serve every
		// substitution here and the whole type below.
		bindings.insert(std::make_pair(
			place.type,
			signatures_.value_place(
				types_, model_, index, pack,
				substituted(types_.parameter_value_type(place.type), bindings,
				            memo))));
	}
	return substituted(type, bindings, memo);
}

SemaEntity* SemaAnalyzer::equivalent_template(SemaEntity& head,
                                              Scope& parameters, TypeId type)
{
	const TypeId wanted = template_signature(parameters, type);
	if (wanted == kNoType)
	{
		return nullptr;
	}
	const std::size_t arity = parameters.declarations.size();
	for (SemaEntity* at = &head; at != nullptr; at = at->next)
	{
		if (at->template_parameters == nullptr ||
		    at->template_parameters == &parameters ||
		    at->template_parameters->declarations.size() != arity)
		{
			continue;
		}
		bool held = false;
		TypeId signature = signatures_.built(at->id, held);
		if (!held)
		{
			// The build makes places of its own, so the entry is written back
			// rather than filled through a reference the build could move.
			signature = template_signature(*at->template_parameters, at->type);
			signatures_.built(at->id, held) = signature;
		}
		if (signature == wanted)
		{
			return at;
		}
	}
	return nullptr;
}

// 14.6.2.2p1: whether an expression written in this region is type-dependent.
//
// It is exactly when a name it writes can reach a type an argument list has yet
// to say, and the regions that carry such a type are the template-parameter
// ones standing over the reading: a region binding each parameter to a type
// standing for itself is a *pattern* being read, and one binding the arguments
// of a specialization is not - so the same walk tells a definition apart from
// every reading of it that has the arguments, and a specialization over a
// dependent argument list from one over a written-out type.
bool SemaAnalyzer::dependent_reading(const Scope& scope)
{
	for (const Scope* at = &scope; at != nullptr; at = at->parent)
	{
		if (at->kind != ScopeKind::TemplateParameters)
		{
			continue;
		}
		for (std::size_t index = 0; index < at->declarations.size(); ++index)
		{
			if (types_.is_dependent(at->declarations[index]->type))
			{
				return true;
			}
		}
	}
	return false;
}

// 7.1.6.2p4 and 14.6p8: the type such a decltype-specifier names while the
// definition writing it is read.
//
// 3.4p1 still looks up the names the expression writes; what the expression is
// *worth* only an argument list says, so the specifier stands for a type of its
// own - dependent, so nothing built from it is read as a type this unit has -
// and the specifier and the region are kept beside it, because 14.7.1p1's
// instantiation answers the question by reading the same expression again.
// One type per specifier and region: a second reading of one declarator against
// one region asks about one type.
namespace
{

// Whether `c` may begin an identifier, which is what tells a name the spelling
// of a decltype-specifier writes from the punctuation around it.
bool starts_a_name(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
		c == '$';
}

// 14.4p1: which two readings of a decltype-specifier name one type.
//
// Nothing is substituted into the expression, so a reading of one can reach two
// things and no others: the specifier as it was written, and the declarations
// the names it writes look up to.  Those declarations are 14.1p1's head and
// 3.3.7p1's clause - the regions a template-declaration puts between the
// specifier and the region it stands in, each binding a name to a type - and
// that region itself, which every reading of this specifier shares.  So the key
// is the two, and two declarations of one entity build it alike: 14.5.1.3p1's
// out-of-class member definition is read against the class its declaration was,
// and 14.5.6.1p5's signature stands one head's parameters in the other's places
// before either is asked.  Keying by the reading instead leaves one function
// declared twice with two return types, which is a declaration and a definition
// that do not meet.
//
// The spelling stands last, because it is the one component a separator can
// stand inside.
std::string dependent_expression_key(const AstNode& node, const Scope& scope)
{
	std::string key;
	const Scope* at = &scope;
	for (; at != nullptr && (at->kind == ScopeKind::Prototype ||
	                         at->kind == ScopeKind::TemplateParameters);
	     at = at->parent)
	{
		for (std::size_t index = 0; index < at->declarations.size(); ++index)
		{
			const SemaEntity& declared = *at->declarations[index];
			// 14.1p2 and 14.5.6.1p5: each declaration of one template spells the
			// places its head declared as it likes, so what a parameter *is* is
			// the position its head gave it - which the type already stands for,
			// canonically where 14.5.6.1p5's signature is what asked.  A place
			// 3.3.7p1's clause bound is the other way round: its name is what the
			// expression writes, and a spelling that writes another one is
			// another spelling.
			if (at->kind == ScopeKind::Prototype)
			{
				key += declared.name;
			}
			key += ':';
			key += std::to_string(declared.type);
			key += ',';
		}
		key += ';';
	}
	key += '@';
	key += std::to_string(at == nullptr ? 0u : at->id);
	key += '#';
	// 14.1p2 again, inside the spelling: a name the specifier writes that a
	// template-parameter region binds stands here for what that head declared
	// rather than for how it spelled it, so `decltype(T() + a)` and
	// `decltype(U() + a)` are one specifier written by two declarations of one
	// template.  Every other run of the spelling is copied as it stands, which
	// is what makes a place's name and an operator part of the key.
	for (std::string::size_type read = 0; read < node.text.size();)
	{
		if (!starts_a_name(node.text[read]))
		{
			key += node.text[read];
			++read;
			continue;
		}
		const std::string::size_type start = read;
		while (read < node.text.size() &&
		       (starts_a_name(node.text[read]) ||
		        (node.text[read] >= '0' && node.text[read] <= '9')))
		{
			++read;
		}
		const std::string word = node.text.substr(start, read - start);
		const SemaEntity* named = nullptr;
		for (const Scope* region = &scope;
		     named == nullptr && region != nullptr &&
		         (region->kind == ScopeKind::Prototype ||
		          region->kind == ScopeKind::TemplateParameters);
		     region = region->parent)
		{
			if (region->kind != ScopeKind::TemplateParameters)
			{
				continue;
			}
			for (std::size_t index = 0; index < region->declarations.size();
			     ++index)
			{
				if (region->declarations[index]->name == word)
				{
					named = region->declarations[index];
					break;
				}
			}
		}
		key += named != nullptr ? ':' + std::to_string(named->type) : word;
	}
	return key;
}

}

TypeId SemaAnalyzer::dependent_expression_type(const AstNode& node,
                                               const Context& ctx)
{
	const std::string key = dependent_expression_key(node, *ctx.scope);
	const std::unordered_map<std::string, TypeId>::const_iterator held =
		dependent_expressions_.find(key);
	if (held != dependent_expressions_.end())
	{
		return held->second;
	}
	const TypeId type = types_.template_parameter_type(model_.type_entity_id(),
	                                                   false, node.text);
	DependentDecltype written;
	written.written = &node;
	written.region = ctx.scope;
	for (const Scope* at = ctx.scope;
	     at != nullptr && (at->kind == ScopeKind::Prototype ||
	                       at->kind == ScopeKind::TemplateParameters);
	     at = at->parent)
	{
		written.reach.push_back(at->declarations.size());
	}
	dependent_written_.insert(std::make_pair(type, written));
	dependent_expressions_.insert(std::make_pair(key, type));
	return type;
}

// 14.7.1p1: the region such a second reading is made against.
//
// Nothing was substituted into the expression, so what the substitution has to
// rebuild is what the names it writes reach: 14.1p1's parameters and 3.3.7p1's
// places are the two kinds of region a template-declaration puts between the
// expression and the region it was written in, and each name either binds
// stands for what the substitution makes of the type it had.  Every region
// outside those two is the one the definition was written in and is reached
// unchanged, so the walk stops at the first of them.
Scope& SemaAnalyzer::substituted_region(
	Scope& written, const std::vector<std::size_t>& reach, std::size_t level,
	const std::unordered_map<TypeId, TypeId>& bindings,
	std::unordered_map<TypeId, TypeId>& memo)
{
	if ((written.kind != ScopeKind::Prototype &&
	     written.kind != ScopeKind::TemplateParameters) ||
	    written.parent == nullptr)
	{
		return written;
	}
	Scope& enclosing =
		substituted_region(*written.parent, reach, level + 1, bindings, memo);
	Scope& region = model_.open(written.kind, enclosing, nullptr, written.dump);
	const SemaKind kind = written.kind == ScopeKind::Prototype
		? SemaKind::Parameter
		: SemaKind::Typedef;
	// 3.3.7p1: only the declarations that stood when the specifier was read.  A
	// place declared after it could not be named by it, and its own type may be
	// what this reading is answering - so rebuilding the whole region asks the
	// substitution for the type it is in the middle of computing.
	const std::size_t stood = level < reach.size()
		? reach[level]
		: written.declarations.size();
	for (std::size_t index = 0; index < stood; ++index)
	{
		const SemaEntity& declared = *written.declarations[index];
		if (declared.name.empty())
		{
			// 14.1p3 and 8.3.5p10: a place or a parameter with no identifier
			// binds nothing, and still stands for an argument.
			continue;
		}
		SemaEntity& made = model_.create(
			kind, declared.name, substituted(declared.type, bindings, memo));
		model_.bind(region, made.name, made);
		model_.declare_in(region, made);
	}
	return region;
}

TypeId SemaAnalyzer::substituted(
	TypeId type, const std::unordered_map<TypeId, TypeId>& bindings,
	std::unordered_map<TypeId, TypeId>& memo)
{
	// The memo is asked first: a type this substitution has already rebuilt is
	// what it was rebuilt into, whatever it is made of.
	const std::unordered_map<TypeId, TypeId>::const_iterator held =
		memo.find(type);
	if (held != memo.end())
	{
		return held->second;
	}
	if (!types_.is_dependent(type))
	{
		return type;
	}
	TypeId out = type;
	const unsigned cv = types_.cv(type);
	const TypeId bare = types_.strip_cv(type);
	switch (types_.kind(bare))
	{
	case TypeKind::Class:
	{
		// 14.7.1p1: the arguments of the specialization are what the bindings
		// reach, and the class they then name is one an instantiation makes.
		SemaEntity* const made = model_.type_owner(bare);
		if (made == nullptr || made->primary == nullptr)
		{
			break;
		}
		const std::vector<TypeId>& written = types_.template_arguments(bare);
		std::vector<TypeId> arguments;
		arguments.reserve(written.size());
		for (std::size_t index = 0; index < written.size(); ++index)
		{
			PackReading(*this).substitute_entry(written[index], bindings, memo,
			                                   arguments);
		}
		out = types_.qualified(
			instantiate_class(*made->primary, arguments).type, cv);
		break;
	}

	case TypeKind::Pointer:
		out = types_.qualified(
			types_.pointer_to(substituted(types_.target(bare), bindings, memo)),
			cv);
		break;

	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		out = types_.reference_to(
			substituted(types_.target(bare), bindings, memo),
			types_.kind(bare) == TypeKind::RValueReference);
		break;

	case TypeKind::Array:
		out = types_.array_of(substituted(types_.target(bare), bindings, memo),
		                      types_.bounded(bare), types_.bound(bare));
		break;

	case TypeKind::MemberPointer:
		out = types_.qualified(
			types_.member_pointer_to(
				substituted(types_.member_class(bare), bindings, memo),
				substituted(types_.target(bare), bindings, memo)),
			cv);
		break;

	case TypeKind::Function:
	{
		const std::vector<TypeId>& given = types_.parameters(bare);
		std::vector<TypeId> parameters;
		parameters.reserve(given.size());
		for (std::size_t index = 0; index < given.size(); ++index)
		{
			PackReading(*this).substitute_entry(given[index], bindings, memo,
			                                   parameters);
		}
		TypeId built = types_.function_of(
			substituted(types_.target(bare), bindings, memo), parameters,
			types_.variadic(bare));
		// 8.3.5p7 and 8.3.5p1: what a member function's declarator wrote after
		// its parameter-clause is part of the type and is not a qualifier
		// `qualified` would carry.
		built = types_.qualified_function(built, cv);
		out = types_.ref_qualified_function(built,
		                                    types_.function_ref_qualifier(bare));
		break;
	}

	default:
	{
		// 14.6.2p1 at a template place: `C<A…>` is the specialization the
		// template bound to `C` makes of the arguments this substitution makes
		// of the list.  Where the binding leaves `C` a place still - a template
		// place of the head being read, forwarded on - the naming stands as it
		// was for the substitution that follows.
		const TypeId place = types_.applied_template(bare);
		if (place != kNoType)
		{
			const std::vector<TypeId>& listed = types_.template_arguments(bare);
			std::vector<TypeId> arguments;
			arguments.reserve(listed.size());
			for (std::size_t index = 0; index < listed.size(); ++index)
			{
				PackReading(*this).substitute_entry(listed[index], bindings,
				                                    memo, arguments);
			}
			std::unordered_map<TypeId, TypeId> alone;
			const TypeId bound = types_.substitute(place, bindings, alone);
			SemaEntity* const named = types_.is_template_name(bound)
				? model_.type_owner(bound)
				: nullptr;
			out = named != nullptr && named->templated != nullptr
				? types_.qualified(instantiate_class(*named, arguments).type, cv)
				: types_.qualified(
					  dependent_template_name(bound, arguments,
					                          types_.user_name(bare)).type,
					  cv);
			break;
		}
		// 14.6.2p1 and 14.7.1p1: a name written after a prefix the definition
		// could not settle is a member of whatever class an argument list makes
		// of that prefix, so the substitution settles the prefix first and looks
		// the name up in the class it names.  A prefix the bindings leave
		// dependent - a specialization over another parameter - names no class
		// yet, and the member stands as it was for the substitution that follows.
		const TypeId prefix = types_.dependent_owner(bare);
		if (prefix != kNoType)
		{
			const TypeId owner = substituted(prefix, bindings, memo);
			out = dependent_member_type(owner, types_.dependent_member(bare), cv,
			                            type);
			break;
		}
		// 7.1.6.2p4 and 14.7.1p1: a decltype-specifier the definition left
		// standing is answered by reading its expression again, against the
		// regions the arguments make of the ones it was written in.  What comes
		// out may depend on a parameter still - a specialization over a
		// dependent argument list is one of these readings - so it is
		// substituted in its turn.
		const std::unordered_map<TypeId, DependentDecltype>::const_iterator
			expression = dependent_written_.find(bare);
		if (expression != dependent_written_.end())
		{
			Context inner;
			inner.scope = &substituted_region(*expression->second.region,
			                                  expression->second.reach, 0,
			                                  bindings, memo);
			inner.dump = inner.scope->dump;
			out = types_.qualified(
				decltype_type(*expression->second.written, inner), cv);
			break;
		}
		// A template parameter itself, which is what the bindings name.
		out = types_.substitute(type, bindings, memo);
		break;
	}
	}
	memo.insert(std::make_pair(type, out));
	return out;
}

// 14.6.2p1 at the substitution: the type `member` names in the class `owner`
// turned out to be, and `written` itself where the class is one no argument
// list has named yet or declares no such type.
//
// 3.4.3.1p1 asks the class for a definition first, because a member of a
// specialization is only there once the instantiation has made it.
TypeId SemaAnalyzer::dependent_member_type(TypeId owner,
                                           const std::string& member,
                                           unsigned cv, TypeId written)
{
	if (types_.is_dependent(owner) || !types_.is_class(types_.strip_cv(owner)))
	{
		return written;
	}
	require_complete_type(owner);
	SemaEntity* const named = model_.type_owner(types_.strip_cv(owner));
	Scope* const region = named == nullptr ? nullptr : model_.region_of(*named);
	SemaEntity* const found =
		region == nullptr ? nullptr
		                  : model_.lookup_in(*region, member, LookupKind::Type);
	return found == nullptr || !names_a_type(*found)
		? written
		: types_.qualified(found->type, cv | types_.cv(owner));
}

// 14.6p8: the reading a template definition's body gets where it stands.
//
// A template declares nothing until it is instantiated, but its definition is
// still a definition.  14.6p8 makes one ill-formed - no diagnostic required -
// if no valid specialization could be generated from it, and leaves to the
// instantiation only what depends on a template parameter; so the body is read
// once here, against the parameters its own head declared, and again for each
// specialization against the arguments bound in their place.
//
// What that first reading asks is what the definition can answer on its own:
// 3.3.3's regions the body opens, the declarations it makes in them, and 3.4p1's
// lookup of every name it writes that no template parameter stands in the way
// of.  It does not translate the body - a type that depends on a parameter has
// no layout, no conversion and no overload set until an argument arrives - so
// the reading is the PA11 one, which describes what a declaration says rather
// than what it does.  Nothing it finds reaches the output: its lines are
// written into a scope that is dropped with this call and no template-id it
// names is instantiated.
DialectReading::DialectReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
	, dialect_(analyzer.dialect_)
{
	analyzer_.dialect_ = SemaDialect::Types;
	++analyzer_.checking_;
}

DialectReading::~DialectReading()
{
	--analyzer_.checking_;
	analyzer_.dialect_ = dialect_;
}

void SemaAnalyzer::check_template_definition(
	const AstNode& node, const Context& inner,
	const std::vector<Parameter>& parameters, TypeId type,
	std::size_t implicit)
{
	if (!lowering())
	{
		// PA11 and PA12 describe what a template-declaration *says*, and its
		// body says nothing about the regions those assignments dump.
		return;
	}
	// The reading is of a pattern rather than of a declaration this unit has,
	// so its lines stand in a scope of their own that is dropped here.
	DumpScope scratch;
	Context reading = inner;
	reading.dump = &scratch;
	reading.node = nullptr;
	const FunctionReading held(*this, nullptr, types_.target(type));
	const DialectReading dialect(*this);
	declare_parameters(parameters, type, reading, nullptr, implicit);
	// 9.2p2: a class this body declares writes member functions whose bodies
	// are read where that class is complete, so this reading owns them the way
	// a class pattern's own reading owns the ones its body held.
	const std::size_t mark = held_bodies_.size();
	if (node.kind == AstKind::SpecialMemberDefinition)
	{
		// 12.1p1 and 12.3.2p1: a special member's declarator writes no type of
		// its own for a body to stand after, so the body is the last thing the
		// definition holds - and 12.6.2's mem-initializers name members whose
		// types an argument list is what settles, which is the instantiation's
		// to read rather than this reading's.
		statement(*node.children.back(), reading);
	}
	else
	{
		for (std::size_t index = 2; index < node.children.size(); ++index)
		{
			statement(*node.children[index], reading);
		}
	}
	read_held_pattern_bodies(mark);
}

// 14.5.1.3p1 and 14.1p2: the class one out-of-class member definition stands
// inside while it is read, which is the innermost one a template-parameter
// region encloses.
//
// `EnclosedBy` puts that region between the class and the one around it for as
// long as the definition is read, and the names the definition's own head wrote
// for the enclosing classes' places are bound there and nowhere in the class.
// The class the *declarator-id* names may be nested below it - `outer<A>::
// inner::f` declares into a nested class whose region is the class around it -
// so the pair is found by walking out from the region the body is read in
// rather than taken from the declaration, which is what makes it one answer for
// every tier such a definition can be written at.  Null for a body no head
// stands over, which is every definition read where it stands.
Scope* enclosed_by_a_head(Scope* from)
{
	for (Scope* at = from; at != nullptr; at = at->parent)
	{
		if (at->kind == ScopeKind::Class && at->parent != nullptr &&
		    at->parent->kind == ScopeKind::TemplateParameters)
		{
			return at;
		}
	}
	return nullptr;
}

// 14.7.1p1: the implicit instantiation of a class template specialization
// causes the implicit instantiation of the *declarations* of its members, and
// not of their definitions.
//
// So a body a specialization's reading of the pattern arrives at is put aside
// rather than written: `deferred_conversion<incomplete>` is a class whose
// layout an object needs and whose conversion function nothing calls, and only
// the body of that function names `sizeof(T)`.  The use that names the member
// is what asks for it, and asking twice writes one definition, so the entry is
// taken off the list where it is granted.  A body written for a class the
// program itself wrote out is the unit's whatever it names, which is what
// 9.2p2 already says: it is written at the end of the unit.
void SemaAnalyzer::queue_definition(Pending& pending)
{
	// 14.5.1.3p1 and 14.1p2: the link `EnclosedBy` is holding while this body is
	// put aside, which is the one that has to stand again where it is read.  It
	// is asked here, at the one door every body the program wrote is queued
	// through, because 12's three entry points reach the queue without ever
	// naming a class and a declarator-id may name one nested below the one the
	// head stands over.
	pending.stands_in = enclosed_by_a_head(pending.scope);
	pending.head =
		pending.stands_in != nullptr ? pending.stands_in->parent : nullptr;
	// 14.6.4.1p1: a specialization named above the definition its template has
	// by the end of the unit is instantiated there, so a definition read after
	// the use that asked for it has nothing left to wait for.
	if (instantiating_class_ == 0 || pending.function == nullptr ||
	    pending.function->definition_required)
	{
		pending_.push_back(pending);
		return;
	}
	held_definitions_.insert(
		std::make_pair(pending.function->id, pending));
}

// 14.7.1p1 and 3.2p3: a use naming `function` asks this unit for its body.
//
// The list the end of the unit walks is what a granted definition joins, and
// the walk is by index over a deque - so a use written inside a body being
// written there puts the definition it names after it and the same walk
// reaches it.  A member no use ever names stays where it was put, and the
// output has neither a definition nor a declaration of it.
void SemaAnalyzer::require_definition(SemaEntity& function)
{
	// The ask is a fact of the function, because a definition the program
	// writes below it is still one this unit owes: 14.6.4.1p1's second point of
	// instantiation is the end of the unit, and nothing asks again there.
	function.definition_required = true;
	if (held_definitions_.empty())
	{
		return;
	}
	const std::unordered_map<std::uint32_t, Pending>::iterator held =
		held_definitions_.find(function.id);
	if (held == held_definitions_.end())
	{
		return;
	}
	const Pending granted = held->second;
	held_definitions_.erase(held);
	pending_.push_back(granted);
}

// 14.7.1p1 and 14.2: whether the definition this unit holds of the function was
// made by an instantiation rather than written out by the program - which a
// specialization of a function template is, and so is every member of a class a
// template-id named, however deeply the classes it belongs to nest.  It is one
// walk of the regions standing over the declaration, which is the nesting the
// program wrote and not anything the instantiation makes.
bool instantiated_declaration(const SemaEntity& function, TypeTable& types)
{
	if (function.primary != nullptr &&
	    function.primary->template_parameters != nullptr)
	{
		return true;
	}
	for (const Scope* at = function.region; at != nullptr; at = at->parent)
	{
		if (at->kind == ScopeKind::Class && at->owner != nullptr &&
		    types.is_specialization(at->owner->type))
		{
			return true;
		}
	}
	return false;
}

void demand_object_storage(TypeId type, TypeTable& types, SemaModel& model)
{
	// 8.3.4p1: an array is as many objects as it has elements, so what is asked
	// about is the element; a pointer or a reference lays out no object of what
	// it names and asks for nothing.
	TypeId at = types.strip_cv(type);
	while (types.kind(at) == TypeKind::Array)
	{
		at = types.strip_cv(types.target(at));
	}
	SemaEntity* const owner = types.is_class(at) ? model.type_owner(at) : nullptr;
	if (owner == nullptr || owner->scope == nullptr || owner->storage_demanded)
	{
		return;
	}
	owner->storage_demanded = true;
	for (std::size_t index = 0; index < owner->bases.size(); ++index)
	{
		demand_object_storage(owner->bases[index].entity->type, types, model);
	}
	const std::vector<SemaEntity*>& members = owner->scope->declarations;
	for (std::size_t index = 0; index < members.size(); ++index)
	{
		SemaEntity& member = *members[index];
		if (member.kind != SemaKind::Variable)
		{
			continue;
		}
		if (member.region != nullptr && member.object_definition)
		{
			// 9.4.2p2: the definition written outside the class is what lays
			// the storage out, and this object is what reaches it.
			member.definition_required = true;
			continue;
		}
		// 9.2p1: a non-static data member is an object of every object of this
		// class, so the classes in *its* type are reached here too.
		demand_object_storage(member.type, types, model);
	}
}

// 3.2p3 and 14.7.1p1: naming a function inside a body an instantiation made is
// what makes this unit hold that function's definition, and the definition is
// held whatever the naming comes to.
//
// 12.8p15's transfer is the one naming with no call under it: carrying the
// value of one object into another is work whatever the member doing it comes
// to, so the initialization names the member where 12.8p12 leaves the bytes to
// stand for the call, while every other constructor the standard defines has
// 12.1p5's answer - its definition does nothing, so nothing names it.  A member
// the standard *declared* is no part of what an instantiation made either: the
// specialization's own class-specifier gives it, exactly as a class the program
// wrote out is given one.  And a use written outside every instantiation asks
// for nothing, because there was no definition to make.
void SemaAnalyzer::note_instantiated_transfer(SemaEntity& constructor)
{
	if (instantiated_body_ == 0 || constructor.instantiated_use ||
	    constructor.implicit_declaration ||
	    (constructor.transfer != kCopyConstructorTransfer &&
	     constructor.transfer != kMoveConstructorTransfer) ||
	    !instantiated_declaration(constructor, types_))
	{
		return;
	}
	constructor.instantiated_use = true;
}

// 10.3p10 and 14.7.1p1: the table of a class that has one names its virtual
// members, and 3.2p3 has no expression to point at for that use - so the class
// being complete is what asks for them, which is the note the clause hangs on
// an implementation instantiating a virtual member eagerly.
//
// A table is a fact of every class this instantiation completed and not of the
// specialization alone: a class the pattern nests inside its body is made here
// too, and its own table names its own members.  So the ask is over the region
// each class opened rather than over one list of declarations, which is the
// walk 14.7.2p8's explicit instantiation already takes.
void SemaAnalyzer::require_table_definitions(SemaEntity& made)
{
	if (made.scope == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < made.scope->declarations.size();
	     ++index)
	{
		SemaEntity& member = *made.scope->declarations[index];
		if (member.kind == SemaKind::Function && member.virtual_function)
		{
			require_definition(member);
		}
		else if (member.kind == SemaKind::Class && member.scope != nullptr &&
		         member.scope != made.scope)
		{
			// 9p2's injected-class-name is the class itself, which the region
			// declares and which no walk of it descends into again.
			require_table_definitions(member);
		}
	}
}

// 14.6.2p3: the base-specifier named a type a template parameter is what
// settles, so no unqualified name written in the class it belongs to is looked
// up in it.
//
// The clause is the program's own syntax and a reading of it does not change
// what it says, so the answer is a fact of that clause: the definition's own
// reading is where it is found, and every specialization the arguments make -
// and every class an instantiated body declares - is read against it again
// rather than asked the question a second time, which an argument list has
// already answered the other way.
void SemaAnalyzer::note_dependent_base(const AstNode& specifier)
{
	dependent_bases_.insert(&specifier);
}

bool SemaAnalyzer::wrote_dependent_base(const AstNode& specifier) const
{
	return dependent_bases_.find(&specifier) != dependent_bases_.end();
}

void SemaAnalyzer::hold_pattern_body(const AstNode& node, const Context& inner,
                                     const std::vector<Parameter>& parameters,
                                     TypeId type)
{
	HeldPatternBody held;
	held.node = &node;
	held.inner = inner;
	held.parameters = parameters;
	held.type = type;
	held_bodies_.push_back(held);
}

// 9.2p2: the member function bodies of one class body, read once that class is
// complete.
//
// Reading one can ask for another class to be read, whose own bodies are held
// above this reading's mark and are that reading's to take - so the entries
// this call owns are taken off the list before any of them is read.
//
// Reading one can also *hold* another: 9.4p2's class declared in a body writes
// member functions of its own, and 9.2p2 leaves each of those where its own
// class is complete, which is above this mark again.  So the list is drained
// back to the mark rather than walked once, and each body is read exactly
// where the class it was written in closed.
void SemaAnalyzer::read_held_pattern_bodies(std::size_t from)
{
	while (held_bodies_.size() > from)
	{
		std::vector<HeldPatternBody> mine(held_bodies_.begin() + from,
		                                  held_bodies_.end());
		held_bodies_.resize(from);
		for (std::size_t index = 0; index < mine.size(); ++index)
		{
			const HeldPatternBody& held = mine[index];
			const FunctionReading reading(*this, nullptr,
			                              types_.target(held.type));
			declare_parameters(held.parameters, held.type, held.inner, nullptr);
			for (std::size_t at = 2; at < held.node->children.size(); ++at)
			{
				statement(*held.node->children[at], held.inner);
			}
		}
	}
}
