#include "sema_analyzer.h"

#include "sema_template_signature.h"

#include <stdexcept>
#include <unordered_set>

#include "ast_model.h"
#include "sema_access.h"
#include "sema_constexpr.h"
#include "sema_derivation.h"
#include "sema_pack.h"
#include "sema_specialize.h"

namespace
{

std::string decimal(unsigned long long value)
{
	std::string digits;
	unsigned long long rest = value;
	while (rest != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (rest % 10)));
		rest /= 10;
	}
	return digits.empty() ? std::string("0") : digits;
}

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

// 8.3.5p5: the cv-qualifier-seq a declarator wrote after its parameter-clause,
// which is what 9.3.1p3 qualifies the object parameter by.  The parser hangs it
// on the declarator beside the declarator-id, so what tells it from a qualifier
// the specifiers wrote is that it stands after that id.
unsigned declarator_function_cv(const AstNode& declarator)
{
	unsigned cv = kCvNone;
	bool after_id = false;
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& part = *declarator.children[index];
		if (part.kind == AstKind::Identifier ||
		    part.kind == AstKind::NestedDeclarator)
		{
			after_id = true;
			continue;
		}
		if (after_id && part.kind == AstKind::CvQualifier)
		{
			cv |= part.token == KW_CONST ? kCvConst : kCvVolatile;
		}
	}
	return cv;
}

// 8.3.5p1: the ref-qualifier written there, for the two declarations that build
// their own function type rather than taking the one an ordinary declarator
// walk built - 12.3.2p1's conversion function, whose declarator writes no type,
// and 12.1/12.4's constructor and destructor, which write no return type.
RefQualifier declarator_ref_qualifier(const AstNode& declarator)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& part = *declarator.children[index];
		// The grammar spells an exception-specification with the same node, so
		// which one this is is what it was written as.
		if (part.kind != AstKind::FunctionQualifier)
		{
			continue;
		}
		if (part.text == "&")
		{
			return RefQualifier::LValue;
		}
		if (part.text == "&&")
		{
			return RefQualifier::RValue;
		}
	}
	return RefQualifier::None;
}

}  // namespace

// What a class is, what its objects hold, and when their lifetimes end.
//
// This is the object-model half of the analysis: 10p1's base-clause says what
// an object of a class is made of - 9.2p13's layout over that is
// `sema_layout.cpp`'s; 11's access specifiers say
// who may name each part; 12.1 and 12.4 say which special member functions the
// class has and what their bodies do; 12.6.2 and 12.4p8 say in what order the
// subobjects of one object are constructed and destroyed; and 3.7.1 and 3.8p1
// say which region ends the lifetime of an object a declaration named.
//
// They belong together because each is settled from the same one fact - what
// the class's own region declares, in the order it declares it - and because a
// question about a subobject is asked in the same words wherever the subobject
// stands: a base, a member, or an object of its own.  The declaration walk in
// `sema_analyzer.cpp` reads the syntax and hands each class to this file once,
// where the class is complete.

// 12.1p1 and 12.4p1: the type a constructor or destructor declarator writes.
// Neither has a return type, and both take the object 9.3.1p3 makes the first
// parameter of a member function's type - so the declaration is read against
// the class it belongs to wherever it is written, which 3.4.1p8 is what puts
// that class in force for a definition written outside it.
TypeId SemaAnalyzer::special_member_type(const AstNode& node,
                                         const Context& ctx,
                                         const SemaEntity& owner,
                                         bool destructor,
                                         std::vector<Parameter>& parameters,
                                         bool& variadic)
{
	const AstNode* const declarator = child_of(node, AstKind::Declarator);
	const AstNode* const clause =
		declarator == nullptr ? nullptr
		                      : child_of(*declarator, AstKind::ParameterClause);
	if (clause != nullptr)
	{
		read_parameters(*clause, ctx, parameters, variadic, nullptr);
	}
	if (declarator != nullptr)
	{
		// 12.1p4 and 12.4p2: neither a constructor nor a destructor writes a
		// cv-qualifier-seq or a ref-qualifier, because what 9.3.1p3's object
		// parameter names is an object whose lifetime is beginning or ending
		// rather than one an expression could have named a category for.
		if (declarator_ref_qualifier(*declarator) != RefQualifier::None &&
		    semantics())
		{
			throw std::runtime_error(
				owner.name + " declares a " +
				(destructor ? "destructor" : "constructor") +
				" with a ref-qualifier, which 8.3.5p6 does not allow");
		}
	}
	std::vector<TypeId> types;
	// 12.4p12 lets a destructor be invoked for any cv-qualified version of its
	// class, which no cv-qualifier-seq of its own says.
	types.push_back(types_.pointer_to(
		destructor ? types_.qualified(owner.type, kCvConst | kCvVolatile)
		           : owner.type));
	for (std::size_t index = 0; index < parameters.size(); ++index)
	{
		// 8.3.5p5: an array or function parameter contributes a pointer, and
		// top level cv-qualification is dropped.
		types.push_back(types_.adjust_parameter(parameters[index].type));
	}
	if (destructor && parameters.size() != 0)
	{
		throw std::runtime_error("a destructor is declared with parameters");
	}
	return types_.function_of(types_.fundamental(FT_VOID), types, variadic);
}

// 12.1p1 and 12.4p1: the unqualified name a special member declarator wrote,
// checked against the class it belongs to.  A name that is not the class's own
// names something else, and what this milestone describes is the class the
// program wrote.
std::string SemaAnalyzer::special_member_name(const std::string& written,
                                              const SemaEntity& owner)
{
	const std::string spelled =
		QualifiedName(types_.user_name(owner.type)).last();
	const bool destructor = !written.empty() && written[0] == '~';
	const std::string named = destructor ? written.substr(1) : written;
	// 14.6.1p1 and 9p2: inside a class template the injected-class-name is the
	// template's own name, and 12.1p1's declarator writes that - so a
	// specialization's constructor is the one its pattern spelled with the
	// template-name, whatever the template-id calls the class.
	if (named != spelled &&
	    (owner.primary == nullptr || named != owner.primary->name))
	{
		// 12.3.2: a conversion function, and 13.5 an operator function written
		// with no return type.  Neither is part of this milestone's slice, and
		// what the output would describe without it is not the class the
		// program wrote.
		throw std::runtime_error(spelled + " declares " + written +
		                         ", which is a special member function this "
		                         "milestone does not describe");
	}
	return spelled;
}

// 12.3.2p1: the name a region binds a conversion function under.
//
// A conversion function is named by the type it converts to, so two
// declarations that spell that type differently declare one function -
// `operator adaptor_type` and `operator runtime_traits` where the first names a
// typedef of the second - and a use written either way names it.  The one
// spelling every one of them agrees on is the type's own, so that is what the
// region binds and what every later reader asks for.
std::string SemaAnalyzer::member_id_name(const AstNode& id, const Context& ctx)
{
	const AstNode* const carried = child_of(id, AstKind::CarriedTypeId);
	if (carried == nullptr || carried->children.empty())
	{
		// 14.2p4: `a.template f<A>` writes the keyword to say the name is a
		// template, and the class is asked for the name without it.
		return without_template_keyword(id.text);
	}
	// 3.4.5p1: the conversion-type-id is looked up in the class of the object
	// expression and in the region the whole expression stands in; the second
	// is what a typedef-name of an enclosing class or namespace is found by,
	// and it is the region every other name in this expression is read in.
	return conversion_name(type_id_type(*carried->children[0], ctx));
}

std::string SemaAnalyzer::conversion_name(TypeId type) const
{
	const std::string described = types_.description(type);
	std::string spelled = "operator";
	for (std::size_t index = 0; index < described.size(); ++index)
	{
		if (described[index] == ' ')
		{
			continue;
		}
		if (described[index] == ':' && index + 1 < described.size() &&
		    described[index + 1] == ':')
		{
			// A `::` inside the name would read as a nested-name-specifier
			// everywhere a qualified name is split - the region a member access
			// looks in, the regions the object file encodes - and this one is
			// part of one component and not a boundary between two.  No type is
			// spelled with a `.`, so writing one there keeps the name one
			// component and still tells two types apart.
			spelled += '.';
			++index;
			continue;
		}
		spelled += described[index];
	}
	return spelled;
}

// 12.3.2p1: the declaration a conversion function's declarator makes in the
// class `target` names.  9.3.1p3's object parameter carries the
// cv-qualifier-seq written after the parameter clause, so `operator int()
// const` and `operator int() const volatile` are two declarations of one name
// that 13.1 tells apart exactly as it tells any two member functions apart.
SemaEntity& SemaAnalyzer::declare_conversion(const AstNode& node,
                                             const Context& target,
                                             const AstNode& carried,
                                             SemaEntity* as)
{
	const AstNode* const declarator = child_of(node, AstKind::Declarator);
	if (declarator == nullptr || carried.children.empty())
	{
		throw std::runtime_error("a conversion function is declared with no "
		                         "declarator");
	}
	const AstNode* const clause =
		child_of(*declarator, AstKind::ParameterClause);
	std::vector<Parameter> parameters;
	bool variadic = false;
	if (clause != nullptr)
	{
		read_parameters(*clause, target, parameters, variadic, nullptr);
	}
	if (!parameters.empty() || variadic)
	{
		// 12.3.2p1: a conversion function takes no arguments, because the one
		// object it acts on is the one it is called on.
		throw std::runtime_error("a conversion function is declared with "
		                         "parameters, which 12.3.2p1 does not allow");
	}
	const TypeId converted = type_id_type(*carried.children[0], target);
	if (types_.kind(converted) == TypeKind::Function ||
	    types_.kind(converted) == TypeKind::Array)
	{
		// 12.3.2p1: the conversion-type-id shall not define a function or an
		// array type, which the grammar's ptr-operators alone cannot spell but
		// a typedef-name can.
		throw std::runtime_error("a conversion function converts to a function "
		                         "or array type, which 12.3.2p1 does not allow");
	}
	const std::string name = conversion_name(converted);
	// 12.3.2p1: a conversion function's declarator writes no type of its own,
	// so its function type is built here rather than by the walk that reads an
	// ordinary declarator - and 8.3.5p1's ref-qualifier is as much a part of it
	// as it is of any other member function's.
	const TypeId written = types_.ref_qualified_function(
		types_.function_of(converted, std::vector<TypeId>(), false),
		declarator_ref_qualifier(*declarator));
	const TypeId type = with_object_parameter(written, *declarator, target,
	                                          false, name, false);
	if (type == written)
	{
		// 9.3p1: a conversion function is a non-static member function, so a
		// declarator that reached no class declares nothing this milestone has
		// a meaning for.
		throw std::runtime_error(name + " is declared where no class declares "
		                         "a member");
	}
	SemaEntity& entity = declare_function(name, type, target, false, false,
	                                      true, false, as);
	entity.object_member = true;
	entity.conversion_function = true;
	// 15.4p1: a conversion function's declarator writes an
	// exception-specification where every other member function's does, and
	// what it says is what 15.2p2 reads to leave a handler out around a call.
	entity.nonthrowing =
		entity.nonthrowing || declarator_nonthrowing(*declarator, target);
	ConstexprReading(*this).defer_specification(entity, *declarator, target);
	entity.wrote_exception_specification =
		entity.wrote_exception_specification ||
		declarator_writes_exception_specification(*declarator);
	return entity;
}

// 12.3.2: a conversion function declared in a class body.  It is a member
// function like any other - 9.3.1p3's object parameter, 13.1's overload chain,
// 11p1's access, 9.2p2's deferred body - and the one thing about it that is not
// ordinary is that its name is a type.
void SemaAnalyzer::conversion_function(const AstNode& node, const Context& ctx,
                                       const AstNode& carried,
                                       SemaEntity* specializing)
{
	SemaEntity& entity = declare_conversion(node, ctx, carried, specializing);
	// 14.1p1 and 14.5.2p1: the head standing over this declaration, which makes
	// it a conversion function *template* whose body 14.6p8 reads where it
	// stands.  A reading for one argument list stands in such a region too and
	// is no head: what it binds is arguments, and the declaration is made.
	Scope* const head =
		specializing == nullptr && &declaring_region(*ctx.scope) != ctx.scope
			? ctx.scope
			: nullptr;
	const AstNode* const specifiers = child_of(node, AstKind::MemberSpecifiers);
	for (std::size_t index = 0;
	     specifiers != nullptr && index < specifiers->children.size(); ++index)
	{
		// 12.3.2p2: `explicit` says which initializations may choose this
		// conversion, exactly as 12.3.1p2 says of a constructor.
		if (specifiers->children[index]->text == "explicit")
		{
			entity.explicit_function = true;
		}
		if (specifiers->children[index]->token == KW_INLINE)
		{
			entity.inline_function = true;
		}
		// 7.1.5p1: a conversion function declared `constexpr` is what lets an
		// object of the class stand where 14.1p4's value place asks for one.
		// 7.1.5p2 makes it implicitly inline besides, exactly as 9.3p2 makes a
		// member function defined in its class body one.
		if (specifiers->children[index]->token == KW_CONSTEXPR)
		{
			entity.constexpr_function = true;
			entity.inline_function = true;
		}
		// 10.3p1: a conversion function is an ordinary member function, so it
		// takes a slot like any other one declared `virtual`.
		if (specifiers->children[index]->token == KW_VIRTUAL)
		{
			entity.virtual_function = true;
		}
	}
	if (entity.constexpr_function)
	{
		// 7.1.5p3: a conversion function is a function like any other, so its
		// return type - which 12.3.2p1 spells as its name - and its parameter
		// types shall be literal types.  It is asked here because a conversion
		// function reaches neither `function_definition` nor `special_member`.
		ConstexprRequirement(*this).require_function(entity, entity.type,
		                                            entity.name);
	}
	// 7.1.2p3 and 9.3p2: a member function defined in its class body is inline.
	entity.inline_function = entity.inline_function ||
		node.kind == AstKind::SpecialMemberDefinition;
	const AstNode* const initializer = child_of(node, AstKind::Initializer);
	{
		const AstNode* const written_declarator =
			child_of(node, AstKind::Declarator);
		if (written_declarator != nullptr)
		{
			read_virt_specifiers(entity, *written_declarator, initializer);
		}
	}
	if (initializer != nullptr && !initializer->children.empty() &&
	    !entity.pure_virtual)
	{
		// 8.4.3: `= delete` declares a function every use of is ill formed.
		entity.deleted = initializer->children[0]->text == "delete";
		entity.defaulted = !entity.deleted;
		require_template_special_member(entity.name, head, false,
		                                entity.defaulted);
		entity.defined = false;
		entity.inline_function = true;
		return;
	}
	entity.user_provided = true;
	if (node.kind != AstKind::SpecialMemberDefinition)
	{
		return;
	}
	const std::vector<Parameter> none;
	open_special_member_body(node, entity, ctx, entity.name, none, head);
}

// 12.3.2 and 9.3p2: a conversion function defined outside its class.  3.4.3p3
// makes the declarator-id name the class the definition belongs to, so the
// class is the region the conversion-type-id and the body alike are read
// against, and what it defines is the declaration that class already made.
bool SemaAnalyzer::conversion_function_definition(const AstNode& node,
                                                  const Context& ctx)
{
	const AstNode* const carried = child_of(node, AstKind::CarriedTypeId);
	if (carried == nullptr)
	{
		return false;
	}
	if (carried->text.empty())
	{
		throw std::runtime_error(node.text + " is defined where no class "
		                         "declares it");
	}
	Scope& region = *resolve_prefix(QualifiedName(carried->text + "operator"),
	                                ctx);
	if (region.kind != ScopeKind::Class || region.owner == nullptr)
	{
		throw std::runtime_error(node.text + " defines a conversion function of "
		                         "what is not a class");
	}
	// 7.1.2p1, 9.2p8 and 8.4p2: a conversion function defined outside its class
	// repeats neither `virtual` nor the virt-specifiers, exactly as every other
	// member function defined outside one does.
	require_special_virtual_placement(node, region, true, node.text);
	Context target = ctx;
	target.scope = &region;
	target.dump = region.dump;
	// 14.5.2p1 and 3.4.1p8: the head this definition wrote stands inside the
	// region its declarator-id names, so the conversion-type-id `U` and the body
	// alike read the places it declared with the class's members behind them.
	Scope* const head = ctx.template_head == ctx.scope ? ctx.scope : nullptr;
	// 14.5.1.3p1: and the head standing outside it, which binds the names this
	// definition wrote for the class template's own places.
	Scope* const enclosing_head = enclosing_template_head(head);
	target.template_head = head;
	const StandingIn stood(head, region);
	if (head != nullptr)
	{
		target.scope = head;
	}
	// 13.1: the declaration this definition defines is the one of the class's
	// own whose name and object parameter agree, which the chain the name heads
	// answers in one probe.  A definition that matches none of them would
	// declare a second member of the class, which 9.2p1 does not allow written
	// outside its body - so what says the definition defines nothing is that
	// the class gained a declaration here.
	const std::size_t declared = region.declarations.size();
	SemaEntity& entity = declare_conversion(node, target, *carried);
	if (region.declarations.size() != declared)
	{
		throw std::runtime_error(node.text + " defines a conversion function "
		                         "its class does not declare");
	}
	if (entity.defined &&
	    !Specialization(*this).require_replaceable(entity, node.text))
	{
		// 14.7.3p1: the program wrote this member's definition out for exactly
		// these arguments above the template's own, so the reading of the
		// pattern defines nothing here.  The definition is still this reading's
		// to have read, which is what the answer says.
		return true;
	}
	const AstNode* const specifiers = child_of(node, AstKind::MemberSpecifiers);
	for (std::size_t index = 0;
	     specifiers != nullptr && index < specifiers->children.size(); ++index)
	{
		if (specifiers->children[index]->token == KW_INLINE)
		{
			entity.inline_function = true;
		}
	}
	entity.user_provided = true;
	// 9.3p2 and 3.2p4: a conversion function is a member function defined
	// outside its class like any other, so the object file holds the definition
	// the program wrote here whether or not a conversion here names it.
	entity.out_of_class_definition = holds_written_definitions(region);
	entity.out_of_line_definition = true;
	if (head != nullptr)
	{
		// 14.5.6.1p5 and 14p1: the definition of the conversion function
		// template the class body declared, whose syntax is what an
		// instantiation of it reads.
		record_function_template(entity, *head, region, enclosing_head);
	}
	const std::vector<Parameter> none;
	open_special_member_body(node, entity, target, entity.name, none, head);
	return true;
}

// 12.1 and 12.4: a constructor or a destructor declared in a class body.  Both
// are functions of the class whose name no lookup reaches: an object of the
// class asks the class for them, so they are chained on the class rather than
// bound to a name in it.
//
// 14.5.2p1's member template is the same declaration with a head over it: what
// the class declares is a function *template* whose specializations 14.8.2
// deduces, and the only thing the head changes is that the declarator's names
// are read in its region while the member is still the class's.  So `region`
// below is the class either way, and `ctx.scope` is where the declarator reads.
void SemaAnalyzer::special_member(const AstNode& node, const Context& ctx)
{
	// 14.7.1p1: a reading of the pattern for one argument list declares nothing
	// - the specialization was made where the call deduced it - so this reading
	// gives that declaration the type the arguments made of the pattern and the
	// body below, exactly as `function_definition` does for every other one.  A
	// definition read inside this one is the ordinary declaration it is.
	SemaEntity* const specializing = instantiating_;
	instantiating_ = nullptr;
	const AstNode* const carried = child_of(node, AstKind::CarriedTypeId);
	if (carried != nullptr)
	{
		// 12.3.2p1: a conversion function is an ordinary member function whose
		// name is a type, so what it declares is read from the type and not
		// from the name the grammar flattened.
		conversion_function(node, ctx, *carried, specializing);
		return;
	}
	Scope& region = declaring_region(*ctx.scope);
	// 14.1p1 and 14.5.2p1: the head standing over this declaration, which is
	// the region the walk is reading in wherever that is not the class itself.
	// A reading for one argument list stands in such a region too and is no
	// head: what it binds is arguments, and the declaration is already made.
	Scope* const head =
		specializing == nullptr && &region != ctx.scope ? ctx.scope : nullptr;
	// 14.5.1.3p1: and the head standing outside it, which is one only where the
	// definition was written outside the class under a head per class it is
	// nested in.
	Scope* const enclosing_head = enclosing_template_head(head);
	SemaEntity& owner = *region.owner;
	const std::string written = QualifiedName(node.text).last();
	const bool destructor = !written.empty() && written[0] == '~';
	const std::string spelled = special_member_name(written, owner);
	require_template_special_member(spelled, head, destructor, false);
	std::vector<Parameter> parameters;
	bool variadic = false;
	const TypeId type = special_member_type(node, ctx, owner, destructor,
	                                        parameters, variadic);
	SemaEntity* entity = specializing;
	if (specializing != nullptr)
	{
		specializing->type = type;
	}
	else if (destructor)
	{
		if (owner.destructor != nullptr)
		{
			throw std::runtime_error(spelled + " declares two destructors");
		}
		entity = &model_.create(SemaKind::Function, written, type);
		entity->tail = entity;
		owner.destructor = entity;
		entity->special = kDestructorFunction;
		model_.bind(region, written, *entity);
	}
	else
	{
		// 13.1: the constructors of a class are its declarations of one name,
		// so a second one joins the chain the first heads and 13.3.1.3 walks it.
		// A constructor has no name a lookup binds, so the chain the class holds
		// is what the parameter list indexes - and a class with n of them is
		// declared in n steps rather than n^2 comparisons.
		// 14.5.6.1p5 with 9.2p1: two heads that declared the same places over
		// the same type declare *one* constructor template, whatever each
		// called its places - and two heads that declared *different* places
		// over one type declare two, which the list a declarator wrote says
		// neither of.  So the chain's index is keyed by the canonical form of
		// whichever head the declaration was written under.
		if (owner.constructor != nullptr &&
		    (model_.overload_of(*owner.constructor,
		                        types_.signature(TemplateSignature(*this).indexed(head, type))) !=
		         nullptr ||
		     (head != nullptr &&
		      TemplateSignature(*this).equivalent(*owner.constructor, *head, type) != nullptr)))
		{
			throw std::runtime_error(spelled +
			                         " declares one constructor twice");
		}
		entity = &model_.create(SemaKind::Function, written, type);
		entity->special = kConstructorFunction;
		entity->tail = entity;
		if (owner.constructor == nullptr)
		{
			owner.constructor = entity;
		}
		else
		{
			owner.constructor->tail->next = entity;
		}
		owner.constructor->tail = entity;
		model_.hold_overload(*owner.constructor,
		                     types_.signature(TemplateSignature(*this).indexed(head, type)),
		                     *entity);
	}
	if (specializing == nullptr)
	{
		name_in_region(*entity, region, written);
	}
	entity->object_member = true;
	if (head != nullptr)
	{
		// 14p1 and 14.5.2p1: this declares a template rather than a function,
		// and the parameters it is written over are what a deduction binds -
		// which is the pair of facts `declare_function` writes for every other
		// member template, asked here because a constructor and a conversion
		// function reach neither it nor `function_definition`.
		entity->template_parameters = head;
		record_function_template(*entity, *head, region, enclosing_head);
	}
	// 15.4p1: a constructor and a destructor carry an exception-specification
	// the same way every other member function does, and it is written on the
	// same declarator - so what says the function throws nothing is read here
	// too, and 15.2p2's handler is left out around a call of one that does not.
	{
		const AstNode* const written_declarator =
			child_of(node, AstKind::Declarator);
		entity->nonthrowing =
			written_declarator != nullptr &&
			declarator_nonthrowing(*written_declarator, ctx);
		if (written_declarator != nullptr)
		{
			ConstexprReading(*this).defer_specification(*entity,
			                                            *written_declarator, ctx);
		}
		entity->wrote_exception_specification =
			entity->wrote_exception_specification ||
			(written_declarator != nullptr &&
			 declarator_writes_exception_specification(*written_declarator));
	}
	// 7.1.2p3 and 9.3p2: a special member function *defined* in its class body
	// is inline, so its definition belongs to every translation unit that needs
	// one.  One the class only declares is defined elsewhere or nowhere, and
	// binds like any other function the program names once.
	entity->inline_function = node.kind == AstKind::SpecialMemberDefinition;
	const AstNode* const specifiers = child_of(node, AstKind::MemberSpecifiers);
	for (std::size_t index = 0;
	     specifiers != nullptr && index < specifiers->children.size(); ++index)
	{
		// 12.3.1p2: `explicit` says which initializations may choose this
		// constructor, which is a fact about the declaration.
		if (specifiers->children[index]->text == "explicit")
		{
			entity->explicit_function = true;
		}
		// 7.1.2p2: a declaration written with `inline` declares an inline
		// function, whether or not it is the declaration the body is written
		// on - so a constructor or destructor the class declares `inline` and
		// a later definition gives a body belongs to every unit that needs
		// one, exactly as one defined in the class body does.
		if (specifiers->children[index]->text == "inline")
		{
			entity->inline_function = true;
		}
		// 7.1.5p1: a constructor declared `constexpr` is what lets 5.2.3p2 and
		// p3 build an object of the class where a constant expression stands,
		// exactly as one on a conversion function is what lets that object
		// reach 14.1p4's value place.  7.1.5p2 makes it implicitly inline
		// besides, as it does every other function it stands on.
		if (specifiers->children[index]->token == KW_CONSTEXPR)
		{
			entity->constexpr_function = true;
			entity->inline_function = true;
		}
		// 10.3p1 and 12.4p9: a destructor declared `virtual` is dispatched
		// through the object's own class, which is what makes `delete` of a
		// base pointer end the whole object.  12.1p4 leaves a constructor no
		// object to dispatch on, and the class refuses one written this way.
		if (specifiers->children[index]->text == "virtual")
		{
			entity->virtual_function = true;
		}
	}
	{
		// 10.3p4/p5 and 10.4p2: a destructor's declarator writes the same three
		// things any other member function's does.
		const AstNode* const written_declarator =
			child_of(node, AstKind::Declarator);
		if (written_declarator != nullptr)
		{
			read_virt_specifiers(*entity, *written_declarator,
			                     child_of(node, AstKind::Initializer));
		}
	}
	if (entity->constexpr_function)
	{
		// 7.1.5p4: each parameter type of a constexpr constructor shall be a
		// literal type, which is the same requirement 7.1.5p3 puts on every
		// other constexpr function and is asked by the same reading.  9.4.2p2's
		// definition written outside the class repeats no `constexpr`, so this
		// declaration is where the program wrote it.
		ConstexprRequirement(*this).require_function(*entity, type, spelled);
	}
	record_declared_parameters(*entity, parameters, ctx.scope);
	if (specializing == nullptr)
	{
		model_.declare_in(region, *entity);
	}
	if (!destructor)
	{
		// 12.9p8: an inheriting using-declaration in a class derived from this
		// one declares a constructor from this one, and the definition it is
		// given writes the parameters this declaration named.
		std::vector<Parameter> named = parameters;
		constructor_parameters_[entity->id].swap(named);
	}

	const AstNode* const initializer = child_of(node, AstKind::Initializer);
	if (initializer != nullptr && !initializer->children.empty() &&
	    !entity->pure_virtual)
	{
		// 8.4.2 and 8.4.3: `= default` asks for the definition 12.1p5 or
		// 12.4p3 would have given, and `= delete` for a declaration every use
		// of is ill formed.  Neither is a definition the program wrote, so
		// 8.5.1p1 leaves the class an aggregate.
		entity->deleted = initializer->children[0]->text == "delete";
		entity->defaulted = !entity->deleted;
		require_template_special_member(spelled, head, false, entity->defaulted);
		entity->defined = false;
		// 8.4.2p1: a function explicitly defaulted on its first declaration is
		// implicitly inline, so the definition the standard gives it belongs to
		// every unit that needs one, exactly as 12.1p5's does.
		entity->inline_function = true;
		return;
	}
	// 8.4.2p4 and 12.1p4: a special member the program declared and did not
	// default or delete on that declaration is user-provided, wherever the body
	// it is given is written - which is what stops the class from being an
	// aggregate, and what 8.5p7 asks before it zeroes a value-initialized
	// object.  The declaration says it, so a constructor defined outside the
	// class says it too.
	entity->user_provided = true;
	if (node.kind != AstKind::SpecialMemberDefinition)
	{
		return;
	}
	open_special_member_body(node, *entity, ctx, written, parameters, head);
}

// 12.6.2 and 9.2p2: the body a special member's definition gives it, read where
// the class is complete rather than where the definition stands.  One
// description serves the definition written in the class body and the one
// written outside it, because the only thing that differs is where the
// declarator-id was written.
void SemaAnalyzer::open_special_member_body(
	const AstNode& node, SemaEntity& entity, const Context& ctx,
	const std::string& written, const std::vector<Parameter>& parameters,
	Scope* head)
{
	entity.defined = true;
	// 14.7.1p1 with 14.7.3p1: whether this body is the pattern's read again for
	// one argument list, which is what 14.7.3p1 lets a definition written out for
	// exactly those arguments replace.
	entity.instantiated_definition = instantiating_pattern_ > 0;
	// 2.2p1: which file this definition was read from, which is what says
	// whether the object file owes the ABI's entry points for it or only the
	// ones this unit's own code named.  It is read here for the same reason
	// 12.4p8 is: the definition is in front of the walk exactly once.
	entity.own_source_definition = own_source(node);
	// 12.4p8: whether the definition writes any statement is what says whether
	// running this function comes to anything.  It is read here for the
	// definition written in the class body; one written outside it stands
	// wherever the program put it, and `note_definition_body` asks the same
	// question of the same node out of the syntax, so a body reading earlier
	// gets the same answer.
	entity.empty_body = writes_no_statement(node);
	if (checking_ > 0)
	{
		// 14.6p8: a reading of a template's own definition has no definition to
		// write, and 12.6.2's mem-initializers name members whose types an
		// argument list is what settles - so what this reading takes from the
		// definition is what its declarator said, which is already read.
		return;
	}

	DumpScope& dump = model_.open_dump(*ctx.dump, "scope function " + written);
	Scope& inner = model_.open(ScopeKind::Function, *ctx.scope, &entity, &dump);
	SemaEntity& self =
		model_.create(SemaKind::Parameter, "this", this_type(entity));
	model_.bind(inner, self.name, self);
	model_.declare_in(inner, self);
	if (head != nullptr)
	{
		// 14p1 and 14.6p8: a template declares no function until it is
		// instantiated, so the output has no definition to write here and the
		// body is not read against the types it has none of yet.  What it says
		// about the names no template parameter stands in the way of is settled
		// where it stands, exactly as `function_definition` settles it for every
		// other member template - and 14.7.1p1's reading for one argument list
		// is the one that writes a body.
		Context reading = ctx;
		reading.scope = &inner;
		reading.dump = &dump;
		check_template_definition(node, reading, parameters, entity.type, 1);
		return;
	}
	// 9.2p2: the body and the mem-initializers are read where the class is
	// complete, which is the end of the translation unit.
	Pending pending;
	pending.function = &entity;
	pending.self = &self;
	pending.body = &node;
	pending.scope = &inner;
	pending.parameters = parameters;
	pending.initializers = child_of(node, AstKind::CtorInitializer);
	// 14.7.3p1: 12's entry points are read from the pattern exactly as every
	// other member is, so a body queued here is one a definition the program
	// writes out for these arguments replaces - and a use that asked for it
	// before that definition was written left this entry where the end of the
	// unit reaches it, which is what the mark is read for there.
	pending.from_pattern = instantiating_pattern_ > 0;
	// 12.6.2p2: the members a mem-initializer-id may name are the class's, and
	// this reading may stand a region below it - 14.5.2p1's head over a
	// definition written outside the class, and 14.7.1p1's bindings a
	// specialization of a constructor template is read against are both such a
	// region - so the class is asked for rather than taken from where the
	// reading happens to stand.
	pending.members = &declaring_region(*ctx.scope);
	if (entity.constexpr_function)
	{
		// 7.1.5p3: what a fold of a call of this member reads - the body, and
		// the region opened just above for its places.  9.2p2 holds the body
		// until the class closes and a fold may ask before that, so the two are
		// recorded here rather than where the body is walked.
		entity.constexpr_body = &node;
		entity.constexpr_region = &inner;
	}
	queue_definition(pending);
}

// 9.3p2 and 12.1p1: a constructor or a destructor defined outside its class.
// 3.4.3p3 makes the declarator-id name the class the definition belongs to, so
// the class is the region the whole definition is read against - its parameters,
// its mem-initializers and its body alike - and what it defines is the
// declaration that class already made rather than a second one.  A class declares
// its special members once, where 9.2p2 completes it, so a definition that
// matches none of them defines nothing and is refused rather than dropped.
void SemaAnalyzer::special_member_definition(const AstNode& node,
                                             const Context& ctx)
{
	if (conversion_function_definition(node, ctx))
	{
		return;
	}
	const QualifiedName spelled(node.text);
	const std::string written = spelled.last();
	if (!spelled.qualified())
	{
		throw std::runtime_error(written + " is defined where no class declares "
		                         "it, which 12.1p1 gives no meaning");
	}
	Scope& region = *resolve_prefix(spelled, ctx);
	if (region.kind != ScopeKind::Class || region.owner == nullptr)
	{
		throw std::runtime_error(node.text + " defines a special member of what "
		                         "is not a class");
	}
	SemaEntity& owner = *region.owner;
	const bool destructor = !written.empty() && written[0] == '~';
	const std::string spelled_class = special_member_name(written, owner);
	// 14.5.2p1 and 3.4.1p8: a head standing over a definition whose
	// declarator-id is qualified stands *inside* the region that name reaches
	// for as long as the declarator and the body are read - so the places
	// `template<class U> A::A(U)` declared are where its parameter clause looks
	// and the class's own members are still behind them.  What such a head may
	// not stand over is the same question wherever it was written.
	Scope* const head = ctx.template_head == ctx.scope ? ctx.scope : nullptr;
	// 14.5.1.3p1: and the head standing outside it, taken before `StandingIn`
	// below moves the nest into the class - which is where `template<class Tp>
	// template<class Up> holder<Tp>::holder(Up)` bound `Tp`.
	Scope* const enclosing_head = enclosing_template_head(head);
	const AstNode* const written_default = child_of(node, AstKind::Initializer);
	require_template_special_member(
		spelled_class, head, destructor,
		written_default != nullptr && !written_default->children.empty() &&
			written_default->children[0]->text == "default");
	// 7.1.2p1, 9.2p8 and 8.4p2: a definition written outside the class repeats
	// neither `virtual` nor the virt-specifiers, which is the same question the
	// declarator of every other member function is asked.
	require_special_virtual_placement(node, region, true, spelled_class);
	Context target = ctx;
	target.scope = &region;
	target.dump = region.dump;
	target.template_head = head;
	const StandingIn stood(head, region);
	Context looked_up = target;
	if (head != nullptr)
	{
		looked_up.scope = head;
	}
	std::vector<Parameter> parameters;
	bool variadic = false;
	const TypeId type = special_member_type(node, looked_up, owner, destructor,
	                                        parameters, variadic);
	// 13.1: the declaration this definition defines is the one of the class's
	// own whose parameter-type-list agrees, which is one probe of the chain the
	// class holds.  12.1p5 and 12.4p3's implicitly declared members are not
	// declarations the program wrote, so a definition never names one.
	SemaEntity* entity =
		destructor ? owner.destructor
		           : (owner.constructor == nullptr
		              ? nullptr
		              : model_.overload_of(
		                    *owner.constructor,
		                    types_.signature(TemplateSignature(*this).indexed(head, type))));
	if (entity == nullptr && head != nullptr && owner.constructor != nullptr)
	{
		// 14.5.6.1p5 with 14.5.2p1: two declarations of one constructor template
		// write types that differ, because each head declared places of its own
		// - so the chain's index of parameter type lists cannot answer this and
		// the declaration is the one whose head declared the same places.
		entity = TemplateSignature(*this).equivalent(*owner.constructor, *head, type);
	}
	if (entity == nullptr || entity->defaulted || entity->deleted ||
	    entity->inherited != nullptr)
	{
		throw std::runtime_error(spelled_class + " declares no " +
		                         (destructor ? "destructor" : "constructor") +
		                         " this definition defines");
	}
	if (entity->defined &&
	    !Specialization(*this).require_replaceable(*entity, node.text))
	{
		// 14.7.3p1: the program wrote this entry point's definition out for
		// exactly these arguments above the template's own, so the reading of
		// the pattern defines nothing here.
		return;
	}
	// 15.4p1: the definition is a declaration of the function like the one the
	// class body wrote, so the two shall write the same exception-specification
	// or neither shall write one.
	{
		const AstNode* const own = child_of(node, AstKind::Declarator);
		require_matching_exception_specification(
			*entity, own != nullptr &&
			declarator_writes_exception_specification(*own),
			own != nullptr && declarator_nonthrowing(*own, ctx), spelled_class);
	}
	// 7.1.2p3: a definition written outside the class is inline only where the
	// program says so, which is what makes it the one definition of the
	// function rather than one every unit that needs it may hold.
	const AstNode* const specifiers = child_of(node, AstKind::MemberSpecifiers);
	for (std::size_t index = 0;
	     specifiers != nullptr && index < specifiers->children.size(); ++index)
	{
		if (specifiers->children[index]->token == KW_INLINE)
		{
			entity->inline_function = true;
		}
	}
	const AstNode* const explicitly = child_of(node, AstKind::Initializer);
	if (explicitly != nullptr && !explicitly->children.empty())
	{
		// 8.4.2p2: `= default` written on a declaration outside the class is a
		// definition of the function, and what it defines is the one the
		// standard describes - so 12.1p5's triviality and 12.8p12's copy are
		// asked again of a class that now has that definition.  7.1.2p2 leaves
		// the definition non-inline, so this unit is the one that holds it
		// rather than every unit that needs one.
		entity->deleted = explicitly->children[0]->text == "delete";
		entity->defaulted = !entity->deleted;
		// 2.2p1: `= default` is a definition like a body, so which file it was
		// read from is read here as `open_special_member_body` reads it there.
		entity->own_source_definition = own_source(node);
		entity->out_of_class_definition =
			holds_written_definitions(*target.scope);
		entity->out_of_line_definition = true;
		resettle_defaulted_member(*entity);
		// 8.3.5p10: `= default` is a declaration of the constructor like any
		// other, so the names its declarator wrote are the function's from here
		// on - and 12.8p28's definition, which reads those objects, is written
		// from the parameters a declaration wrote rather than from a declarator
		// of its own.
		record_declared_parameters(*entity, parameters, target.scope);
		if (!destructor)
		{
			std::vector<Parameter> named = parameters;
			constructor_parameters_[entity->id].swap(named);
		}
		if (!entity->deleted)
		{
			// 3.2p4: the definition this unit was told to write is written
			// whether or not anything here names the function, because another
			// unit's use of it is what it is for.
			demand_constructor_definition(*entity);
		}
		return;
	}
	// 8.3.5p10: the definition is a declaration of the function like the one the
	// class body wrote, so a parameter it left unnamed is named by whichever
	// declaration of the constructor named it.
	record_declared_parameters(*entity, parameters, target.scope);
	if (node.kind == AstKind::SpecialMemberDeclaration)
	{
		// 14.7.3p1 with 12.1p1: a `template<>` head over a constructor of a class
		// template specialization writes a *declaration* of the member that
		// specialization already has, and no body - so what it says is that the
		// declaration is the program's own for these arguments and there is
		// nothing else to read.  Every other special-member-declaration written
		// outside a class body wrote 8.4p2's `= default` or `= delete`, which the
		// arm above already took.
		return;
	}
	entity->out_of_class_definition = holds_written_definitions(*target.scope);
	entity->out_of_line_definition = true;
	if (head != nullptr)
	{
		// 14.5.6.1p5 and 14p1: this is the definition of the template the class
		// body declared, so what an instantiation reads is *this* syntax and the
		// places this head wrote - which is what `declare_function` records for
		// every other member template on the definition that gives it a body.
		record_function_template(*entity, *head, region, enclosing_head);
	}
	open_special_member_body(node, *entity, looked_up, written, parameters, head);
}

// 12.9p1: how many parameters the shortest constructor in the candidate set a
// declaration contributes takes, which is the ones no default-argument has
// reached.  8.3.6p4 says a default-argument stands on every parameter after the
// first one that has one, so the count is where they begin.
std::size_t SemaAnalyzer::required_parameters(const SemaEntity& function) const
{
	const std::size_t declared = types_.parameters(function.type).size();
	const std::unordered_map<std::uint32_t, std::vector<ParameterRecord> >::const_iterator
		found = defaults_.find(function.id);
	if (found == defaults_.end())
	{
		return declared;
	}
	for (std::size_t index = 0; index < declared; ++index)
	{
		if (index < found->second.size() &&
		    found->second[index].initializer.written != nullptr)
		{
			return index;
		}
	}
	return declared;
}

// 12.9p1: a using-declaration that names the constructors of a direct base
// class declares a constructor of this class from each constructor in the
// base's candidate set.  That set is the base's own constructors and, for each
// one a default-argument reached, the shorter parameter lists that result from
// omitting the ellipsis and then those parameters from the end - so what is
// inherited is a parameter-type-list rather than a declaration, and 12.9p2's
// constructor characteristics hold no default-argument of their own.  12.9p3
// leaves out the two an object of this class already has of its own - the
// base's default constructor and its copy and move constructors - and 12.9p1
// leaves out one whose parameters a constructor this class declared itself
// already takes.  What each of them does is 12.9p8's initialization of the base
// subobject, so what the declaration has to carry is the base's constructor and
// the names its parameters were declared with.
void SemaAnalyzer::inherit_constructors(SemaEntity& base, Scope& where)
{
	SemaEntity& derived = *where.owner;
	const std::string spelled =
		QualifiedName(types_.user_name(derived.type)).last();
	for (SemaEntity* at = base.constructor; at != nullptr; at = at->next)
	{
		const std::vector<TypeId>& written = types_.parameters(at->type);
		const std::size_t least = required_parameters(*at);
		for (std::size_t count = written.size(); count + 1 > least && count > 1;
		     --count)
		{
			inherit_constructor(*at, base, count, count == written.size(),
			                    derived, where, spelled);
		}
	}
}

// One constructor of that candidate set: the first `taken` parameters of the
// base's declaration, with the ellipsis kept only where none was omitted.
void SemaAnalyzer::inherit_constructor(SemaEntity& from, const SemaEntity& base,
                                       std::size_t taken, bool whole,
                                       SemaEntity& derived, Scope& where,
                                       const std::string& spelled)
{
	const std::vector<TypeId>& written = types_.parameters(from.type);
	if (taken <= 1)
	{
		// 12.9p3: a constructor with no parameters is not inherited, and
		// 12.1p5 gives this class one of its own.
		return;
	}
	if (taken == 2 &&
	    bare_type(types_.is_reference(written[1]) ? types_.target(written[1])
	                                              : written[1]) ==
		types_.strip_cv(base.type))
	{
		// 12.9p3: neither is the base's copy or move constructor, which would
		// make an object of this class out of a base subobject.
		return;
	}
	std::vector<TypeId> parameters;
	// 9.3.1p3: the object the constructor runs on is one of this class.
	parameters.push_back(types_.pointer_to(derived.type));
	for (std::size_t index = 1; index < taken; ++index)
	{
		parameters.push_back(written[index]);
	}
	const TypeId type =
		types_.function_of(types_.fundamental(FT_VOID), parameters,
		                   whole && types_.variadic(from.type));
	// 12.9p1: a base constructor whose parameters a constructor of this class
	// already takes is not inherited, and neither is one two members of the
	// candidate set agree on.  13.1's index of the chain answers both in one
	// probe, so a base with n constructors costs n.
	if (derived.constructor != nullptr &&
	    model_.overload_of(*derived.constructor,
	                       types_.signature(type)) != nullptr)
	{
		return;
	}
	SemaEntity& entity = model_.create(SemaKind::Function, spelled, type);
	name_in_region(entity, where, spelled);
	entity.object_member = true;
	entity.special = kConstructorFunction;
	entity.inherited = &from;
	entity.explicit_function = from.explicit_function;
	entity.deleted = from.deleted;
	// 12.9p6 and 7.1.2p3: the definition is one the standard gives it and this
	// unit generates where a use asks for one, as 12.1p5's is, so it belongs to
	// every translation unit that needs one.
	entity.defaulted = true;
	entity.inline_function = true;
	entity.tail = &entity;
	if (derived.constructor == nullptr)
	{
		derived.constructor = &entity;
	}
	else
	{
		derived.constructor->tail->next = &entity;
	}
	derived.constructor->tail = &entity;
	model_.hold_overload(*derived.constructor, types_.signature(type), entity);
	model_.declare_in(where, entity);
	// 12.9p8: the definition writes the parameters, so the names the base's
	// declaration gave them are part of what is inherited.  A parameter the
	// candidate set omitted is one this declaration does not have; the base's
	// own default-argument is what fills it where 12.9p8's call is written.
	const std::unordered_map<std::uint32_t,
	                         std::vector<Parameter> >::const_iterator names =
		constructor_parameters_.find(from.id);
	if (names != constructor_parameters_.end())
	{
		std::vector<Parameter> kept(
			names->second.begin(),
			names->second.begin() +
				static_cast<std::ptrdiff_t>(
					taken - 1 < names->second.size() ? taken - 1
					                                 : names->second.size()));
		constructor_parameters_[entity.id].swap(kept);
	}
}

// 12.1p5 and 12.9p3: whether this class declares a constructor of its own,
// which an inherited one is not - 12.9 declares it from the base's rather than
// from anything written here, and leaves the class the default constructor it
// would have had.
// 12.1p5 and 12.9p3: whether this class declares a constructor of its own,
// which an inherited one is not - so a class that only inherits still has the
// default constructor 12.1p5 gives it.
bool declares_own_constructor(const SemaEntity& entity)
{
	for (const SemaEntity* at = entity.constructor; at != nullptr; at = at->next)
	{
		if (at->inherited == nullptr)
		{
			return true;
		}
	}
	return false;
}

// 9.2p2: the special members a class has where its definition ends, which is
// where the class is complete and where every subobject they act on is known.
// 12.1p5 gives a class with no constructor of its own a default one, 12.4p3
// gives one with no destructor a destructor, 12.9p4 settles what access the
// constructors a using-declaration inherited have, and 12.1p5/12.4p3 say which
// of them do nothing at all.
void SemaAnalyzer::declare_special_members(SemaEntity& entity, Scope& scope)
{
	for (std::size_t index = 0;
	     scope.inheriting_constructors && index < entity.bases.size(); ++index)
	{
		// 12.9p1: a using-declaration named a base's constructors, and which of
		// them are inherited is settled here, where the class the standard
		// calls complete holds every constructor it declares itself.
		inherit_constructors(*entity.bases[index].entity, scope);
	}
	// 12.8p2/p3/p17/p19: which of the four value-transfer members the program
	// itself declared, read before anything is added to the class - because
	// p9 and p20 make the answer decide whether the class has the other two at
	// all, and p7 and p18 whether the ones it does have are deleted.
	note_transfers(entity, scope);
	collect_conversions(entity, scope);
	const bool wrote_destructor = entity.destructor != nullptr;
	if (!declares_own_constructor(entity))
	{
		declare_constructor(entity, scope);
	}
	if (!wrote_destructor)
	{
		declare_destructor(entity, scope);
	}
	declare_transfer_members(entity, scope, wrote_destructor);
	// 10.3p2 and 10.3p10: the class has every member it will ever have here -
	// 12.4p3's destructor among them, which is virtual where the base's is - so
	// this is where its table is settled and where 10.3p4, p5 and p7 are asked
	// of the declarations that took a slot in it.  It stands before the
	// triviality below, because 12.1p5 and 12.4p5 each ask whether the class
	// dispatches before they say a member of it does nothing.
	settle_virtual_members(entity, scope);
	// 8.4.2p2: and the answers below are asked again wherever a definition
	// written later in the unit changes one, so the class is recorded here in
	// the order its answers were first settled - which is the order a subobject
	// is settled before whatever holds it.
	settled_classes_.push_back(&entity);
	settle_class_answers(entity, scope);
}

// 9.2p2 and 12.1p5: the answers a *complete* class carries - whether each
// member the standard defines does anything, whether it can be defined at all,
// what it allows to be thrown, and whether the bytes of an object are its copy.
//
// Each is one walk of the class's subobjects, and every one of them reads what
// those subobjects' own answers already are, so they are settled where the
// class-specifier closes.  8.4.2p2's `= default` written outside the class is
// the one thing that arrives later, and it asks these again rather than
// patching one: what changed is what the standard's definition of one member
// comes to, and each of these is an answer about the whole class.
void SemaAnalyzer::settle_class_answers(SemaEntity& entity, Scope& scope)
{
	for (SemaEntity* at = entity.constructor; at != nullptr; at = at->next)
	{
		if (at->inherited != nullptr)
		{
			// 12.9p4: an inherited constructor has the access the one it was
			// declared from has, whatever access-specifier the using-declaration
			// that brought it in stood under.
			at->access = at->inherited->access;
		}
		// 12.1p5 and 8.4.2p1: a special member written `= default` does what
		// the implicitly declared one would, and what that is, is known only
		// here, where the class is complete.
		if (at->defaulted && types_.parameters(at->type).size() == 1)
		{
			at->trivial = trivial_default_construction(scope);
			// 12.1p5: a default constructor the standard writes has nothing to
			// initialize a member of const-qualified or reference type with, so
			// the class has none - which is what 8.5p6's default-initialization
			// of an object of the class then names.
			at->deleted = at->deleted || undefinable_default(scope);
			if (!at->wrote_exception_specification)
			{
				// 15.4p14: the specification of a member the standard defines is
				// what the members its definition directly invokes allow, and
				// this is the one of the six that is settled here rather than
				// beside 12.8p15's transfer members - the class is complete, so
				// every subobject's own constructor has been settled.
				at->nonthrowing = default_construction_nonthrowing(scope);
			}
		}
	}
	if (entity.destructor->defaulted)
	{
		entity.destructor->trivial = trivial_destruction(scope);
	}
	// 12.4p3 and 15.4p14: a destructor declared with no exception-specification
	// of its own has the one an implicit declaration would - which is what the
	// destructors it directly invokes allow - whether the program wrote the
	// declaration or the standard did.  It is settled here, where the class is
	// complete and every subobject's own destructor has been settled.
	if (!entity.destructor->wrote_exception_specification)
	{
		entity.destructor->nonthrowing = destruction_nonthrowing(scope);
	}
	// 12.4p3: whether the program declared a destructor anywhere below this
	// class is a fact of the class like the four above it, settled once here
	// and read as one field by every end of a lifetime the translation writes.
	// The walk is 12.4p8's own - the base subobject and the non-variant
	// members, each of which already holds its answer - so a class asks its
	// subobjects once and a chain n deep costs n reads and not n walks.
	types_.settle_declared_destruction(
		types_.strip_cv(entity.type),
		!entity.destructor->implicit_declaration ||
			subobject_declares_destruction(entity, scope));
	settle_transfers(entity, scope);
	// 3.9p10 and 12.1p5: whether an object of this class is one a constant
	// expression may build, which is the last of these because it reads the
	// destructor's triviality, the constructors 12.1p5 just settled and
	// 8.5.1p1's aggregate all at once.
	ConstexprRequirement(*this).settle_class(entity, scope);
}

// 8.4.2p2 and 12.8p12: a definition written outside a class settles what the
// standard's definition of that member comes to, and the classes already
// settled *over* it read the answer it changed - so every class this unit
// completed is asked again, in the order it was completed, which is the order
// each one's subobjects were settled before it.
//
// The pass runs once, where the whole unit has been read, and only where a
// definition of the sort was written: `settle_class_answers` is the walk the
// closing brace already cost, so a program that writes none pays nothing and
// one that writes any pays that walk a second time.
void SemaAnalyzer::resettle_completed_classes()
{
	if (!resettle_classes_)
	{
		return;
	}
	resettle_classes_ = false;
	// 12.4p8: what running a destructor comes to is held per type, and one of
	// the answers below is what it was read from, so the held ones are dropped
	// and asked again as the classes are.
	vacuous_.clear();
	for (std::size_t index = 0; index < settled_classes_.size(); ++index)
	{
		SemaEntity& entity = *settled_classes_[index];
		if (entity.scope != nullptr)
		{
			settle_class_answers(entity, *entity.scope);
		}
	}
}

// 12.8p2, p3, p17 and p19: which of the four value-transfer special members a
// declaration of `owner` declares, read from the parameter list 9.3.1p3 gave
// the function's type rather than from the declarator that wrote it.  A
// constructor is one where its first written parameter is a reference to its
// own class and every parameter after it has a default argument; `operator=` is
// one where it takes exactly that one parameter.  Which of the copy and the
// move it is, is which reference the parameter is - and a parameter of the
// class itself is 12.8p17's copy assignment taking its argument by value.
unsigned char SemaAnalyzer::transfer_kind(const SemaEntity& function,
                                          const SemaEntity& owner)
{
	if (function.shadowed != nullptr || function.inherited != nullptr ||
	    !function.object_member)
	{
		// 12.9p3 and 7.3.3p3: a constructor inherited from a base and a member
		// a using-declaration brought in are declarations of the base's
		// function, which is a member of the base and not of this class.
		return kNotTransfer;
	}
	const bool constructor = function.special == kConstructorFunction;
	if (!constructor && function.name != "operator=")
	{
		return kNotTransfer;
	}
	const std::vector<TypeId>& parameters = types_.parameters(function.type);
	if (parameters.size() < 2)
	{
		return kNotTransfer;
	}
	for (std::size_t index = 2; index < parameters.size(); ++index)
	{
		// 12.8p2: a constructor whose later parameters all have default
		// arguments is still a copy constructor, because a call with one
		// argument reaches it.  12.8p17 gives an assignment operator exactly
		// one parameter, so any second one leaves it an ordinary overload.
		if (!constructor || !has_default_argument(function, index))
		{
			return kNotTransfer;
		}
	}
	const TypeId written = parameters[1];
	const bool rvalue = types_.kind(written) == TypeKind::RValueReference;
	const bool reference = types_.is_reference(written);
	if (constructor && !reference)
	{
		// 12.8p2: a constructor taking its own class by value is not a copy
		// constructor - it could never be called - and 12.1p6 makes it
		// ill formed, which the declaration path is where to say.
		return kNotTransfer;
	}
	const TypeId bare =
		types_.strip_cv(reference ? types_.target(written) : written);
	if (bare != types_.strip_cv(owner.type))
	{
		return kNotTransfer;
	}
	if (constructor)
	{
		return rvalue ? kMoveConstructorTransfer : kCopyConstructorTransfer;
	}
	return rvalue ? kMoveAssignmentTransfer : kCopyAssignmentTransfer;
}

// 8.3.6p1: whether the declarations of `function` gave the parameter at `index`
// in its type a default argument, which is what lets a call leave it out.
bool SemaAnalyzer::has_default_argument(const SemaEntity& function,
                                        std::size_t index)
{
	const std::unordered_map<std::uint32_t, std::vector<ParameterRecord> >::const_iterator
		found = defaults_.find(function.id);
	return found != defaults_.end() && index < found->second.size() &&
		found->second[index].initializer.written != nullptr;
}

// 12.8: the four value-transfer members the program itself declared, recorded
// on the class.  Constructors are the chain the class already holds and
// assignment operators are the declarations of one name in its own region, so
// both are one walk and neither searches.
void SemaAnalyzer::note_transfers(SemaEntity& entity, Scope& scope)
{
	for (SemaEntity* at = entity.constructor; at != nullptr; at = at->next)
	{
		note_transfer(entity, *at);
	}
	for (SemaEntity* at = own_assignments(scope); at != nullptr; at = at->next)
	{
		if (at->kind == SemaKind::Function)
		{
			note_transfer(entity, *at);
		}
	}
}

// 13.5.3p1: the declarations of `operator=` this class's own region holds.
//
// 12.8 asks about the declarations of one class, so what answers it is the
// binding that class's region has and not 3.4's lookup for the name: 10.2 would
// reach a base's `operator=` and 3.4.1 an enclosing class's, and both of those
// are declarations of another class, whose facts belong to that class alone.
SemaEntity* SemaAnalyzer::own_assignments(Scope& scope)
{
	return model_.find(scope, "operator=", LookupKind::Any);
}

// 12.3.2p1 and 13.3.1.5p1: the conversion functions an object of this class
// has.
//
// They are the ones this class declared and the ones a base declared that
// 10.2p2 does not hide - a conversion this class declares to the same type
// hides the base's, exactly as any member of that name would.  The list is
// built once, where 9.2p2 completes the class, out of this class's own
// declarations and the list its base already holds: a hierarchy of n classes
// each declaring one conversion costs one step per class rather than one walk
// of the chain per conversion asked for.
void SemaAnalyzer::collect_conversions(SemaEntity& entity, Scope& scope)
{
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity* const at = scope.declarations[index];
		if (at->conversion_function)
		{
			entity.conversions.push_back(at);
		}
	}
	entity.conversions_above.clear();
	if (!entity.conversions.empty())
	{
		entity.conversions_above.push_back(&entity);
		return;
	}
	// 10.2p2: this class declares none, so what an object of it converts by is
	// what its bases carry - the nearest classes above *them* that declare any.
	// A class with one base holds one entry however deep the derivation goes,
	// which is what keeps 13.3.1.5p1's candidate set one walk of the few classes
	// that declare a conversion rather than one walk of every base.
	for (std::size_t index = 0; index < entity.bases.size(); ++index)
	{
		const std::vector<SemaEntity*>& above =
			entity.bases[index].entity->conversions_above;
		entity.conversions_above.insert(entity.conversions_above.end(),
		                                above.begin(), above.end());
	}
}

// 12.3.2p1 and 13.3.1.5p1: the conversion functions an object of `owner` has,
// which are the ones its class declares and the ones a base declares that
// 10.2p2 does not hide.
//
// The classes that declare any are the roots each class holds, so this walks
// those and not every base - and a class writes its candidates once per question
// asked rather than once per class derived from it.
void SemaAnalyzer::gather_conversions(const SemaEntity& owner,
                                      std::vector<SemaEntity*>& out)
{
	out.clear();
	// The roots left to walk, which a class declaring no conversion at all -
	// nearly every class - leaves empty and allocates nothing for.  A question
	// asked while this one is being answered gets a walk of its own, because
	// 13.3.1.5's candidate set is read while a conversion is being chosen.
	std::vector<SemaEntity*> pending(owner.conversions_above);
	for (std::size_t root = 0; root < pending.size(); ++root)
	{
		const SemaEntity* const at = pending[root];
		for (std::size_t index = 0; index < at->bases.size(); ++index)
		{
			// 10.2p2 again, one level further out: what this class's own bases
			// declare stands behind what it declares itself.
			const std::vector<SemaEntity*>& above =
				at->bases[index].entity->conversions_above;
			pending.insert(pending.end(), above.begin(), above.end());
		}
		const std::size_t nearer = out.size();
		for (std::size_t index = 0; index < at->conversions.size(); ++index)
		{
			SemaEntity* const candidate = at->conversions[index];
			bool hidden = false;
			for (std::size_t here = 0; here < nearer && !hidden; ++here)
			{
				// 10.2p2: a conversion to the same type declared in a class
				// nearer the object hides this one, exactly as any member of
				// that name would.
				hidden = out[here]->name == candidate->name;
			}
			if (!hidden)
			{
				out.push_back(candidate);
			}
		}
	}
}

void SemaAnalyzer::note_transfer(SemaEntity& entity, SemaEntity& function)
{
	const unsigned char kind = transfer_kind(function, entity);
	function.transfer = kind;
	if (kind != kNotTransfer && entity.transfers[kind - 1] == nullptr)
	{
		entity.transfers[kind - 1] = &function;
	}
}

// 12.8p7, p9, p18 and p20: the value-transfer members a class that declared
// none of its own has.  The copy constructor and the copy assignment operator
// are always there; the move constructor and the move assignment operator only
// where the program declared no copy member, no move member and no destructor -
// because a class that wrote any one of those said how its objects are carried,
// and the standard does not add a second answer beside it.
void SemaAnalyzer::declare_transfer_members(SemaEntity& entity, Scope& scope,
                                            bool wrote_destructor)
{
	const bool wrote_copy_constructor =
		entity.transfers[kCopyConstructorTransfer - 1] != nullptr;
	const bool wrote_move_constructor =
		entity.transfers[kMoveConstructorTransfer - 1] != nullptr;
	const bool wrote_copy_assignment =
		entity.transfers[kCopyAssignmentTransfer - 1] != nullptr;
	const bool wrote_move_assignment =
		entity.transfers[kMoveAssignmentTransfer - 1] != nullptr;
	// 12.8p9 and p20: the one condition both of the move members are declared
	// under, which any copy member, any move member of the other kind, or a
	// destructor the program wrote takes away.
	const bool moves = !wrote_copy_constructor && !wrote_copy_assignment &&
		!wrote_destructor;
	// 12.8p7 and p18: a class that said how its objects move is one whose
	// implicitly declared copy members are deleted.
	const bool deleted_copies = wrote_move_constructor || wrote_move_assignment;
	if (!wrote_copy_constructor)
	{
		declare_transfer_member(entity, scope, kCopyConstructorTransfer,
		                        deleted_copies);
	}
	if (!wrote_move_constructor && moves)
	{
		declare_transfer_member(entity, scope, kMoveConstructorTransfer, false);
	}
	if (!wrote_copy_assignment)
	{
		declare_transfer_member(entity, scope, kCopyAssignmentTransfer,
		                        deleted_copies);
	}
	if (!wrote_move_assignment && moves)
	{
		declare_transfer_member(entity, scope, kMoveAssignmentTransfer, false);
	}
}

// 12.8p8, p10, p19 and p21: one value-transfer member the standard rather than
// the program declares.  A constructor takes the object it builds and the one
// it is built from; an assignment operator takes the object it writes to and
// hands it back as an lvalue.  Both are inline, because a definition no
// declaration wrote belongs to every translation unit that needs one.
void SemaAnalyzer::declare_transfer_member(SemaEntity& entity, Scope& scope,
                                           unsigned char kind, bool deleted)
{
	const bool constructor = kind == kCopyConstructorTransfer ||
		kind == kMoveConstructorTransfer;
	const bool moving = kind == kMoveConstructorTransfer ||
		kind == kMoveAssignmentTransfer;
	std::vector<TypeId> parameters;
	parameters.push_back(types_.pointer_to(entity.type));
	// 12.8p8 and p19: the parameter is `const C&` for a copy and `C&&` for a
	// move.  The non-const `C&` form p8 allows is not written here: every
	// subobject this milestone copies is copied through a `const` reference.
	parameters.push_back(types_.reference_to(
		moving ? entity.type : types_.qualified(entity.type, kCvConst),
		moving));
	const TypeId result = constructor
		? types_.fundamental(FT_VOID)
		: types_.reference_to(entity.type, false);
	const std::string spelled = constructor
		? QualifiedName(types_.user_name(entity.type)).last()
		: std::string("operator=");
	SemaEntity& member = model_.create(
		SemaKind::Function, spelled,
		types_.function_of(result, parameters, false));
	name_in_region(member, scope, spelled);
	member.object_member = true;
	member.inline_function = true;
	member.defaulted = true;
	member.implicit_declaration = true;
	member.deleted = deleted;
	member.transfer = kind;
	member.tail = &member;
	entity.transfers[kind - 1] = &member;
	if (constructor)
	{
		member.special = kConstructorFunction;
		// 13.1: the constructors of a class are the declarations of one name,
		// so this one joins the chain 13.3.1.3 walks rather than replacing it.
		if (entity.constructor == nullptr)
		{
			entity.constructor = &member;
		}
		else
		{
			entity.constructor->tail->next = &member;
		}
		entity.constructor->tail = &member;
		model_.hold_overload(*entity.constructor, types_.signature(member.type),
		                     member);
		model_.declare_in(scope, member);
		return;
	}
	// 13.5.3p1: `operator=` is a name an ordinary lookup in the class reaches,
	// so the declaration is bound in this class's own region as well as chained.
	// A base's or an enclosing class's declaration of the name is another
	// class's member, which this one neither joins nor hides.
	SemaEntity* const head = own_assignments(scope);
	if (head == nullptr)
	{
		model_.bind(scope, "operator=", member);
	}
	else
	{
		head->tail->next = &member;
		head->tail = &member;
		model_.hold_overload(*head, types_.signature(member.type), member);
	}
	model_.declare_in(scope, member);
}

// 12.8p15 and p28: the special member one subobject of class type is carried by
// where the enclosing class's is `kind`.  A class with no move member is moved
// by its copy member instead, because 12.8p15 direct-initializes the subobject
// from an xvalue and 13.3 binds that to a `const` lvalue reference where no
// rvalue one is declared.
SemaEntity* SemaAnalyzer::selected_transfer(TypeId type, unsigned char kind)
{
	SemaEntity* const owner = model_.type_owner(member_copy_type(type));
	if (owner == nullptr)
	{
		return nullptr;
	}
	SemaEntity* const chosen = owner->transfers[kind - 1];
	if (chosen != nullptr)
	{
		return chosen;
	}
	if (kind == kMoveConstructorTransfer)
	{
		return owner->transfers[kCopyConstructorTransfer - 1];
	}
	if (kind == kMoveAssignmentTransfer)
	{
		return owner->transfers[kCopyAssignmentTransfer - 1];
	}
	return nullptr;
}

// 12.8p12, p13, p25 and p26: whether a value-transfer member the standard gives
// this class does nothing but copy the bytes of an object, and 12.8p11 and p23
// whether the standard can give it a definition at all.  Both are one walk of
// the subobjects: a subobject of class type is carried by the member its own
// class has, and a subobject of any other type is carried by its storage - so
// what the enclosing member is, is what its parts are.
void SemaAnalyzer::settle_transfers(SemaEntity& entity, Scope& scope)
{
	for (unsigned char kind = 1; kind <= kTransferKinds; ++kind)
	{
		SemaEntity* const member = entity.transfers[kind - 1];
		if (member == nullptr || !member->defaulted)
		{
			// 8.4.2p1: a definition the program wrote is what the program says
			// it is, and nothing about the class makes it trivial.
			continue;
		}
		// 12.8p12 and p25: a class with a virtual function has no trivial
		// transfer member, because the definition the standard gives it leaves
		// the vpointer of the object written into naming that object's own
		// class - which the bytes of the source do not hold wherever the two
		// are of different classes.  It is asked here rather than of each
		// subobject, because the vpointer is a fact of *this* class's storage.
		bool trivial = !entity.polymorphic;
		bool deleted = member->deleted;
		// 15.4p14: the exception-specification of a member the standard defines
		// is what the members its definition directly invokes allow, which for
		// 12.8p15's memberwise transfer is the one member each subobject of
		// class type is carried by.  A subobject of any other type is carried
		// by its storage and invokes nothing.
		bool nonthrowing = true;
		const bool assignment = kind == kCopyAssignmentTransfer ||
			kind == kMoveAssignmentTransfer;
		for (std::size_t index = 0; index < entity.bases.size(); ++index)
		{
			SemaEntity* const carried =
				selected_transfer(entity.bases[index].entity->type, kind);
			if (carried == nullptr || carried->deleted)
			{
				deleted = true;
			}
			else
			{
				trivial = trivial && carried->trivial;
				nonthrowing = nonthrowing && carried->nonthrowing;
				if (!accessible(*carried, scope))
				{
					deleted = true;
				}
			}
		}
		for (std::size_t index = 0; index < scope.declarations.size(); ++index)
		{
			SemaEntity& field = *scope.declarations[index];
			if (!declares_subobject(field, scope))
			{
				continue;
			}
			const TypeId bare = member_copy_type(field.type);
			// 8.3.4p1: an array of const elements is const in each of them, so
			// what says whether a member can be written is the cv-qualification
			// of the element rather than of the array around it.
			TypeId element = field.type;
			while (types_.kind(types_.strip_cv(element)) == TypeKind::Array)
			{
				element = types_.target(types_.strip_cv(element));
			}
			if (assignment && (types_.is_reference(field.type) ||
			                   (types_.cv(element) & kCvConst) != 0))
			{
				// 12.8p23: a member of const-qualified or reference type is one
				// an assignment has nothing to write, so the standard gives the
				// class's own assignment no definition.
				deleted = true;
				continue;
			}
			if (!assignment &&
			    types_.kind(field.type) == TypeKind::RValueReference)
			{
				// 12.8p11: a member of rvalue reference type is one a copy has
				// nothing to bind.
				deleted = true;
				continue;
			}
			if (!types_.is_class(bare))
			{
				continue;
			}
			SemaEntity* const carried = selected_transfer(field.type, kind);
			if (carried == nullptr || carried->deleted ||
			    !accessible(*carried, scope))
			{
				deleted = true;
				continue;
			}
			trivial = trivial && carried->trivial;
			nonthrowing = nonthrowing && carried->nonthrowing;
		}
		member->trivial = trivial && !deleted;
		member->deleted = deleted;
		if (one_storage(entity.type))
		{
			// 12.8p15 and p28: the definition the standard gives a union copies
			// the object representation and invokes the transfer member of no
			// subobject's class, so 15.4p14 has nothing for it to allow.  p11's
			// deletion and p12's triviality go on asking every variant member,
			// because those are questions about the members the class declares
			// and not about what its definition calls.
			nonthrowing = true;
		}
		if (!member->wrote_exception_specification)
		{
			// 15.4p14: an implicitly declared member, and one `= default`
			// declared it, throws what the members it invokes throw.  One the
			// program wrote an exception-specification on keeps what it wrote.
			member->nonthrowing = nonthrowing;
		}
	}
	// 12.8p11, p12 and 12.4p8: what an object of this class is carried by is
	// what its copy constructor is *and* what the end of its lifetime is, and
	// the layout wrote a first answer for the first of them before this class
	// had one.  All three are settled here so that the layout of a class
	// holding one of these, 5.2.2p4's argument, 6.6.3p2's returned object and
	// the lowering's copy of an object all read the one fact rather than each
	// asking the declarations again: whether the bytes are the copy, whether the
	// program has a copy of an object of this class at all, and whether
	// anything runs when one of them ends.
	//
	// The end of the lifetime is 12.4p8's question and not 12.4p5's, because
	// what says the bytes are not the whole of the object is that something
	// *runs* when one ends - and a destructor 12.4p5 calls non-trivial whose
	// body writes no statement runs nothing, which is the answer every other
	// end of a lifetime in this translation already reads.
	const SemaEntity* const copy = entity.transfers[kCopyConstructorTransfer - 1];
	const bool deleted_copy = copy == nullptr || copy->deleted;
	types_.settle_copy_facts(types_.strip_cv(entity.type),
	                         !deleted_copy && copy->trivial, deleted_copy,
	                         vacuous_destruction(entity.type),
	                         !entity.bases.empty());
}

// 8.4.2p2 and 12.8p12: a special member the program explicitly defaulted or
// deleted *outside* its class has the definition the standard describes as much
// as one defaulted where it was declared does, and the class was complete
// before that definition was read - so the answers that class carries are
// asked again here, over the class the definition arrived at.
//
// They are asked again rather than patched because they are answers about the
// class and not about the one member that changed: 12.1p5's triviality reads
// what every subobject's own member is, and 12.8p12's copy is what says whether
// 5.2.2p4's argument, 8.5's initialization of an object of the class and the
// layout of a class holding one are the bytes.  A class *already* settled over
// this one read the answer that just changed, and `resettle_completed_classes`
// is where those are asked again - here, where only this class's own answers
// are known to have moved, is where a class the program writes *after* the
// definition reads the right one.
void SemaAnalyzer::resettle_defaulted_member(SemaEntity& function)
{
	Scope* const region = function.region;
	if (checking_ > 0 || region == nullptr || region->owner == nullptr ||
	    (!function.defaulted && !function.deleted))
	{
		return;
	}
	settle_class_answers(*region->owner, *region);
	resettle_classes_ = true;
}

// 11.2p1: whether a member of another class may be named from inside `scope`,
// which is what 12.8p11 and p23 ask of the transfer member of every subobject.
bool SemaAnalyzer::accessible(const SemaEntity& member, Scope& scope)
{
	if (member.access == kPublicAccess)
	{
		return true;
	}
	for (Scope* at = &scope; at != nullptr; at = at->parent)
	{
		if (at == member.region)
		{
			return true;
		}
	}
	if (member.access != kProtectedAccess)
	{
		return false;
	}
	// 11.4p1: a protected member of a base class is one a class derived from it
	// may name, and 12.8p11's subobject is exactly the base subobject of the
	// class asking - so the derivation this class already holds is the answer.
	return derives_from(scope, *member.region);
}

// 12.1p5: whether the definition the standard would give a default constructor
// of this class is one it cannot write - a non-static data member of
// const-qualified or reference type that no brace-or-equal-initializer reaches
// is a subobject 8.5p6 leaves holding nothing and 8.5.3p1 leaves bound to
// nothing, and the constructor that would have to initialize it is deleted.
bool SemaAnalyzer::undefinable_default(Scope& scope)
{
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope) || member.default_initializer)
		{
			continue;
		}
		// 8.3.4p1: an array of const elements is const in each of them, so the
		// element type is what says whether a member can be left alone - and
		// its own cv-qualification is exactly what the question is about.
		TypeId type = member.type;
		while (types_.kind(types_.strip_cv(type)) == TypeKind::Array)
		{
			type = types_.target(types_.strip_cv(type));
		}
		if (types_.is_reference(type) ||
		    (!types_.is_class(types_.strip_cv(type)) &&
		     (types_.cv(type) & kCvConst) != 0))
		{
			return true;
		}
	}
	return false;
}

// 12.1p5: a class with no declared constructor has a default one, which the
// course ABI gives the object it initializes as its only parameter.  It is
// declared where the definition of the class ends, because that is where 9.2p2
// makes the class complete and where every member it would initialize is known.
void SemaAnalyzer::declare_constructor(SemaEntity& entity, Scope& scope)
{
	std::vector<TypeId> parameters;
	parameters.push_back(types_.pointer_to(entity.type));
	// 9p1: a class is named by its own name wherever it is declared, so the
	// constructor of `N::C` is `N::C::C` and the constructor of a class no
	// declaration named is named after the name the convention gave it.
	const std::string spelled = QualifiedName(types_.user_name(entity.type)).last();
	SemaEntity& constructor = model_.create(
		SemaKind::Function, spelled,
		types_.function_of(types_.fundamental(FT_VOID), parameters, false));
	name_in_region(constructor, scope, spelled);
	constructor.object_member = true;
	// 7.1.2p3 and 12.1p5: a constructor no declaration wrote is inline, so the
	// definition it is given belongs to every translation unit that needs one
	// rather than to the one that happened to write the class.
	constructor.inline_function = true;
	constructor.trivial = trivial_default_construction(scope);
	// 12.1p5: a member of const-qualified or reference type that nothing
	// initializes leaves the standard no definition to give this constructor.
	constructor.deleted = undefinable_default(scope);
	constructor.tail = &constructor;
	constructor.special = kConstructorFunction;
	constructor.defaulted = true;
	constructor.implicit_declaration = true;
	model_.declare_in(scope, constructor);
	// 12.9p3: an inheriting using-declaration may already have put constructors
	// on this chain, and none of them is the one this declares, so it joins
	// them rather than replacing them.
	if (entity.constructor == nullptr)
	{
		entity.constructor = &constructor;
	}
	else
	{
		entity.constructor->tail->next = &constructor;
	}
	entity.constructor->tail = &constructor;
	model_.hold_overload(*entity.constructor,
	                     types_.signature(constructor.type), constructor);
}

// 12.4p3: a class with no declared destructor has one, declared where the
// definition of the class ends and taking the object it destroys as the only
// parameter 9.3.1p3 gives a member function.
void SemaAnalyzer::declare_destructor(SemaEntity& entity, Scope& scope)
{
	std::vector<TypeId> parameters;
	// 12.4p12: a destructor may be invoked for an object of any cv-qualified
	// version of its class, which the object parameter is what says.
	parameters.push_back(
		types_.pointer_to(types_.qualified(entity.type, kCvConst | kCvVolatile)));
	const std::string spelled =
		"~" + QualifiedName(types_.user_name(entity.type)).last();
	SemaEntity& destructor = model_.create(
		SemaKind::Function, spelled,
		types_.function_of(types_.fundamental(FT_VOID), parameters, false));
	name_in_region(destructor, scope, spelled);
	destructor.object_member = true;
	destructor.inline_function = true;
	destructor.trivial = trivial_destruction(scope);
	destructor.tail = &destructor;
	destructor.special = kDestructorFunction;
	destructor.defaulted = true;
	destructor.implicit_declaration = true;
	model_.declare_in(scope, destructor);
	// 12.4p12 and 5.2.4: `x.~C()` names the destructor through the class, so
	// the one name a lookup can reach it by is bound where it is declared.
	model_.bind(scope, spelled, destructor);
	entity.destructor = &destructor;
}


// 11.2: whether a context in `from` may name `member`.  A member declared
// `public` is named from anywhere; any other is named only from inside the
// class that declared it, which 11.7 also gives to a class nested in it.
// 3.3.6: the innermost namespace a region is written in, which 7.3.1.2p3 makes
// a friend-declared function a member of however deeply the class that declared
// it is nested.
Scope& SemaAnalyzer::friend_namespace(Scope& scope)
{
	Scope* at = &scope;
	while (at->kind != ScopeKind::Namespace && at->parent != nullptr)
	{
		at = at->parent;
	}
	return *at;
}


Naming::Naming(SemaAnalyzer& owner, Scope* region)
	: owner(owner)
	, held(owner.naming_)
{
	if (region != nullptr)
	{
		owner.naming_ = region;
	}
}

Naming::~Naming()
{
	owner.naming_ = held;
}

// 11.2p4: the region a conversion is written in, held over the whole of an
// initialization rather than over the reading of the expression alone - because
// the conversion 8.5 applies is written where the initialization is and is
// asked for after the operand's own reading has given the region back.
Written::Written(SemaAnalyzer& owner, Scope* region)
	: owner(owner)
	, held(owner.reading_)
{
	if (region != nullptr)
	{
		owner.reading_ = region;
	}
}

Written::~Written()
{
	owner.reading_ = held;
}

// 11p6: a declaration written outside the class it names a member of checks
// every one of its names with the access that class gives, which is what lets
// the return type of `A::I A::f()` and the initializer of `A::I A::x` name a
// private nested type.  The class is the region the declarator-id's
// nested-name-specifier reached; a spelling that reaches none names no member,
// and the declaration that wrote it fails on its own where it is read.
Scope* SemaAnalyzer::naming_context(const std::string& written,
                                    const Context& ctx)
{
	const QualifiedName spelled(written);
	if (!spelled.qualified())
	{
		return nullptr;
	}
	try
	{
		Scope* const region = resolve_prefix(spelled, ctx);
		return region != nullptr && region->kind == ScopeKind::Class ? region
		                                                            : nullptr;
	}
	catch (const std::runtime_error&)
	{
		return nullptr;
	}
}


bool observable_expression(const DumpNode& node)
{
	switch (node.fact.kind)
	{
	case FactKind::Sizeof:
		return false;

	case FactKind::Literal:
	case FactKind::Id:
	case FactKind::Member:
	case FactKind::Unary:
	case FactKind::Binary:
	case FactKind::Conditional:
	case FactKind::Subscript:
	case FactKind::Cast:
	// 4.10p3 and 10p1: a base conversion is the address its operand already
	// produced, moved to where the class put the base - so what running it does
	// is what running the operand does and nothing more.
	case FactKind::BaseConversion:
		break;

	default:
		return true;
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (observable_expression(*node.children[index]))
		{
			return true;
		}
	}
	return false;
}

// 5.2.5p1: the object expression of a member access is evaluated whatever the
// member turns out to be.  Where the member is not part of the object the
// access denotes what the member alone does, and the object expression is left
// with nothing to do - which is only true where evaluating it does nothing.
// This milestone has no node that sequences a discarded operand before the
// member, so an object expression that does something is refused rather than
// dropped from the program the output describes.
void SemaAnalyzer::require_droppable(const DumpNode& object,
                                     const std::string& member)
{
	if (observable_expression(object))
	{
		throw std::runtime_error("the object expression of " + member +
		                         " does something, and " + member +
		                         " is not a member of the object it names");
	}
}

// 12.1p5: whether default-initializing an object of the class this region
// declares does nothing at all.  It does nothing when no member asks for
// anything: a member with a brace-or-equal-initializer asks for what 12.6.2p8
// makes it, and a member of class type asks for whatever its own constructor
// is.  Layout has already run, so the class's members are exactly the
// declarations of this region.
bool SemaAnalyzer::trivial_default_construction(Scope& scope)
{
	if (scope.owner != nullptr && scope.owner->polymorphic)
	{
		// 12.1p5: a class with a virtual function has no trivial default
		// constructor, because constructing an object of it writes the
		// vpointer whatever else it leaves alone.
		return false;
	}
	for (std::size_t index = 0;
	     scope.owner != nullptr && index < scope.owner->bases.size(); ++index)
	{
		// 12.1p5: each base class subobject is constructed too, so what its own
		// constructor does is part of what constructing this class does.
		const SemaEntity* const base =
			scope.owner->bases[index].entity->constructor;
		if (base != nullptr && !base->trivial)
		{
			return false;
		}
	}
	// 12.6.2p8 and 9.5p1: a variant member no mem-initializer designated is not
	// initialized at all, and the constructor the standard defines designates
	// none - so what default-initializing an object of a union comes to is what
	// 9.5p2's one brace-or-equal-initializer comes to, and nothing else.  This
	// is 12.4p8's reading of the same one storage at the other end of the
	// lifetime, which `vacuous_destruction` and `write_member_destructions`
	// already write, asked here so the question and the code that answers it
	// agree: a member of class type whose own constructor does something is no
	// object of the union's until a constructor says it stands in the storage.
	const bool variant = scope.owner != nullptr && one_storage(scope.owner->type);
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		if (member.default_initializer)
		{
			return false;
		}
		if (variant)
		{
			continue;
		}
		const SemaEntity* const constructor =
			class_constructors(types_.element_of(member.type));
		if (constructor != nullptr && !constructor->trivial)
		{
			return false;
		}
	}
	return true;
}

// 15.4p14 and 12.1p5: whether the default constructor of the class this region
// declares throws nothing.  What its definition directly invokes is the default
// constructor of the base class subobject and of each member of class type, so
// the answer is yes exactly where every one of those says so - the same walk
// 12.1p5's triviality is, asked of a different fact of the same members.
//
// A member with a brace-or-equal-initializer is initialized by 12.6.2p8 from
// whatever the program wrote there, which is an expression rather than a
// declaration this can read - so it allows every exception, as 15.4p14 says a
// member function it cannot see the specification of does.
bool SemaAnalyzer::default_construction_nonthrowing(Scope& scope)
{
	for (std::size_t index = 0;
	     scope.owner != nullptr && index < scope.owner->bases.size(); ++index)
	{
		const SemaEntity* const base =
			default_constructor(scope.owner->bases[index].entity->type);
		if (base != nullptr && !base->nonthrowing)
		{
			return false;
		}
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		if (member.default_initializer)
		{
			return false;
		}
		const SemaEntity* const constructor =
			default_constructor(types_.element_of(member.type));
		if (constructor != nullptr && !constructor->nonthrowing)
		{
			return false;
		}
	}
	return true;
}

// 15.4p14 and 12.4p3: whether the destructor of the class this region declares
// throws nothing.  What its definition directly invokes is the destructor of
// the base class subobject and of each member of class type, so the answer is
// yes exactly where every one of those says so - the same walk 12.4p5's
// triviality is, asked of a different fact of the same members.
bool SemaAnalyzer::destruction_nonthrowing(Scope& scope)
{
	if (scope.owner != nullptr && one_storage(scope.owner->type))
	{
		// 12.4p8: a union's destructor destroys none of its members, so there
		// is no destructor it directly invokes and nothing for 15.4p14 to
		// allow - the same reading `write_member_destructions` writes and
		// `vacuous_destruction` answers.
		return true;
	}
	for (std::size_t index = 0;
	     scope.owner != nullptr && index < scope.owner->bases.size(); ++index)
	{
		const SemaEntity* const base =
			scope.owner->bases[index].entity->destructor;
		if (base != nullptr && !base->nonthrowing)
		{
			return false;
		}
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		const SemaEntity* const destructor =
			class_destructor(types_.element_of(member.type));
		if (destructor != nullptr && !destructor->nonthrowing)
		{
			return false;
		}
	}
	return true;
}

// 12.4p3: whether destroying an object of the class this region declares does
// nothing at all, which it does when every member's own destruction does.
bool SemaAnalyzer::trivial_destruction(Scope& scope)
{
	if (scope.owner != nullptr && scope.owner->destructor != nullptr &&
	    scope.owner->destructor->virtual_function)
	{
		// 12.4p5: a virtual destructor is not trivial, because 5.3.5p3's
		// `delete` of a pointer to a base class has to reach it through the
		// object's own class - so the end of the lifetime is a call the
		// program can observe wherever the object stands.
		return false;
	}
	for (std::size_t index = 0;
	     scope.owner != nullptr && index < scope.owner->bases.size(); ++index)
	{
		// 12.4p8: each base class subobject is destroyed too.
		const SemaEntity* const base =
			scope.owner->bases[index].entity->destructor;
		if (base != nullptr && !base->trivial)
		{
			return false;
		}
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		const SemaEntity* const destructor =
			class_destructor(types_.element_of(member.type));
		if (destructor != nullptr && !destructor->trivial)
		{
			return false;
		}
	}
	return true;
}

// 12.4p3 and 12.4p8: whether any subobject of the class this region declares
// has a destructor the program itself declared.  The walk is the one 12.4p8's
// destruction makes - the base class subobject and the non-variant members,
// because a union's destructor destroys no member of its own - and each of
// them already carries its answer, so this is one step per declaration and
// never a walk of the tree below it.
bool SemaAnalyzer::subobject_declares_destruction(SemaEntity& entity,
                                                  Scope& scope)
{
	for (std::size_t index = 0; index < entity.bases.size(); ++index)
	{
		if (types_.has_declared_destruction(entity.bases[index].entity->type))
		{
			return true;
		}
	}
	if (one_storage(entity.type))
	{
		return false;
	}
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		const SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope) ||
		    types_.is_reference(member.type))
		{
			continue;
		}
		if (types_.has_declared_destruction(types_.element_of(member.type)))
		{
			return true;
		}
	}
	return false;
}

void SemaAnalyzer::inject_anonymous_members(SemaEntity* entity,
                                            const Context& ctx, const Span& span,
                                            bool is_static)
{
	// 9.5p1: a class with no name and no declarator declares its members in the
	// region it is written in rather than a region of its own.  The standard
	// writes the rule for a union; the anonymous struct the README puts in this
	// milestone's subset is the same rule over a class whose members are laid
	// out one after another instead of over one, which is what the class's own
	// layout already settled - so what the two share is that the members are
	// reached through an object no name reaches, and nothing here has to tell
	// them apart.
	if (entity == nullptr || entity->scope == nullptr || !entity->name.empty() ||
	    !types_.is_class(entity->type))
	{
		return;
	}
	// 9.5p3 asks a namespace-scope anonymous aggregate to be declared `static`,
	// and the object it declares is one no other unit has a name for either
	// way - so the object file gives it internal linkage whether or not the
	// specifier was written, rather than owing a name nothing can reach.  The
	// earlier milestones accept the form without it and their fixtures ask for
	// that, so the specifier changes nothing here.
	const bool at_namespace = ctx.scope->kind == ScopeKind::Namespace;
	(void)is_static;
	// 9.5p1 is written for the union, and the anonymous struct is the extension
	// the README puts in this milestone's subset - as a *member*, which is
	// where g++ and the references accept one and nowhere else.  Written
	// anywhere else it declares nothing, so nothing is what it is read as.
	if (types_.class_tag(entity->type) != ClassTag::Union &&
	    ctx.scope->kind != ScopeKind::Class)
	{
		throw std::runtime_error("an anonymous struct declares nothing outside "
		                         "a class");
	}
	// 9.5p1: the class declares an object of itself that has no name either, so
	// a member is still a member of an object, and the convention names that
	// object after the terminals the declaration was written from, as it names
	// the class.
	SemaEntity* storage = nullptr;
	if (semantics())
	{
		const std::string name =
			(types_.class_tag(entity->type) == ClassTag::Union
				 ? "__anonymous_union_storage__"
				 : "__anonymous_class_storage__") +
			decimal(span.begin) + "_" + decimal(span.end);
		storage = &model_.create(SemaKind::Variable, name, entity->type);
		model_.bind(*ctx.scope, name, *storage);
		model_.declare_in(*ctx.scope, *storage);
		storage->object_member = ctx.scope->kind == ScopeKind::Class;
		// 9.5p1: this is the object no name reaches, which is what tells it
		// from an object of an unnamed class a declarator did name.
		storage->anonymous_storage = true;
		// 3.1p2 and 9.5p3: at namespace scope the class declared an object, and
		// this unit is the one that defines it - with the internal linkage
		// `static` gave it, which is what keeps two units that each write one
		// from naming the same object.
		storage->object_definition = at_namespace;
		storage->internal_linkage = storage->internal_linkage || at_namespace;
		if (ctx.node != nullptr)
		{
			DumpNode& line = open_fact(*ctx.node, "variable " + name + " " +
			                           types_.description(entity->type),
			                           FactKind::Variable);
			line.fact.entity = storage;
			line.fact.type = entity->type;
			line.fact.object_definition = at_namespace;
			construct_object(*storage, line, nullptr, ctx);
		}
		// 9.2p1: a union written in a class declares an object that is a member
		// of it, which the enclosing class initializes and which no line of its
		// own describes, as no other data member has one.
	}
	Scope& members = *entity->scope;
	for (std::size_t index = 0; index < members.declarations.size(); ++index)
	{
		SemaEntity& member = *members.declarations[index];
		if (member.kind != SemaKind::Variable || member.shadowed != nullptr)
		{
			continue;
		}
		if (member.storage == nullptr)
		{
			member.storage = storage;
		}
		// 9.5p1 through a class written inside this one: the member already
		// reaches the object that class declared, and what this one adds is
		// where *that* object stands - which is the object this class declared.
		// So the chain is left alone and the object of the inner class is given
		// its own place in the outer one, which the walk that names the member
		// then follows from the outside in.
		model_.bind(*ctx.scope, member.name, member);
		model_.declare_in(*ctx.scope, member);
		if (!semantics())
		{
			write_line(*ctx.dump, "variable", member.name, member.type);
		}
	}
}

// 9.3.1p3: a non-static member function is called on an object of its class,
// which is a parameter of it no declarator writes, cv-qualified as the
// cv-qualifier-seq written after its parameter-clause says.  Holding it in the
// type is what lets everything above read a member function as the function it
// is: its declaration, its definition and a pointer to it all see the same
// parameters.
TypeId SemaAnalyzer::with_object_parameter(TypeId type,
                                           const AstNode& declarator,
                                           const Context& target, bool is_static,
                                           const std::string& name,
                                           bool qualified)
{
	if (!semantics() && checking_ == 0)
	{
		return type;
	}
	// 14.6p8: a reading of a template's own definition is asked what its
	// declarations say, and whether a member function is called on an object is
	// one of the things they say - 12.3.2p1's conversion function and 13.5p6's
	// operator are both refused without it.
	// 8.3.5p1: the ref-qualifier is already part of the function type the
	// declarator built, and 9.3.1p3's rebuild below is what carries it over.
	const RefQualifier ref = types_.function_ref_qualifier(type);
	// 8.3.5p5: the cv-qualifier-seq of a member function is written after its
	// parameter-clause, so it is a suffix of the declarator rather than one of
	// the qualifiers its specifiers wrote - and it qualifies the object rather
	// than the function, which is why it is read here and not there.
	const unsigned cv = declarator_function_cv(declarator);
	// 12.5p1: an allocation or deallocation function a class declares is a
	// static member of it even where `static` was not written.  The storage it
	// is asked for is what an object of the class would stand in, so there is no
	// object for 9.3.1p3's implicit parameter to name.
	// 9.4.1p2: the definition of a static member function written outside its
	// class shall not repeat `static`, so which kind of member a qualified
	// declarator declares is a fact of the declaration in the class it
	// redeclares rather than of its own specifiers - and a static member
	// function writes no ref-qualifier, so the declaration it is looked for
	// among is one that carries none.
	// 14.1p1 and 14.5.2p1: the region the declaration belongs to, which for a
	// member template is the class the head's own region was opened in - a
	// member template declares a member like any other, so 9.3.1p3 gives it the
	// object parameter every non-static one has.
	Scope& where = declaring_region(*target.scope);
	const bool object_member =
		where.kind == ScopeKind::Class && !is_static &&
		where.owner != nullptr && !allocation_function_name(name) &&
		!(qualified &&
		  declares_static_member(
			  where, name,
			  types_.ref_qualified_function(type, RefQualifier::None),
			  target.template_head));
	if (!object_member)
	{
		if (ref != RefQualifier::None)
		{
			// 8.3.5p6: a ref-qualifier shall be part of a function declarator
			// only where it declares a non-static member function, because
			// 13.3.1p4 gives it a meaning only where there is an implicit
			// object parameter for it to qualify.
			throw std::runtime_error(
				name + " is declared with a ref-qualifier where 8.3.5p6 allows "
				"one only on a non-static member function");
		}
		return type;
	}
	std::vector<TypeId> parameters;
	parameters.push_back(
		types_.pointer_to(types_.qualified(where.owner->type, cv)));
	const std::vector<TypeId>& written = types_.parameters(type);
	parameters.insert(parameters.end(), written.begin(), written.end());
	return types_.ref_qualified_function(
		types_.function_of(types_.target(type), parameters,
		                   types_.variadic(type)),
		ref);
}

// 15.4p14: whether the declarator wrote an exception-specification at all,
// whatever it says.  It is the other half of the question above: where a
// declaration wrote one, what it wrote is what the function says; where none
// did, 12.4p3 and 15.4p14 give a special member the one an implicit
// declaration would have had.
bool SemaAnalyzer::declarator_writes_exception_specification(
	const AstNode& declarator)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& part = *declarator.children[index];
		if (part.kind == AstKind::NestedDeclarator &&
		    declarator_writes_exception_specification(part))
		{
			return true;
		}
		if (part.kind != AstKind::FunctionQualifier)
		{
			continue;
		}
		if (part.text.compare(0, 5, "throw") == 0 ||
		    part.text.compare(0, 8, "noexcept") == 0)
		{
			return true;
		}
	}
	return false;
}

// 9.4p1: whether `where` declares `name` as a static member function whose
// declarator wrote `type`.  That declaration is the one a definition written
// outside the class redeclares, and 9.4.1p2 makes it the only place `static` is
// written - so without this question the definition declares a second,
// non-static function of the same name, which the call the program wrote does
// not name.  The chain the name heads is indexed by the parameter type list, so
// the question is a probe rather than a walk of the declarations already made.
bool SemaAnalyzer::declares_static_member(Scope& where, const std::string& name,
                                          TypeId type, Scope* head)
{
	SemaEntity* const declared = model_.find(where, name, LookupKind::Any);
	if (declared == nullptr || declared->kind != SemaKind::Function)
	{
		return false;
	}
	// 14.5.2 and 14.5.6.1p5: a member template's definition writes a head of its
	// own, so the declaration it redeclares carries a type over *those*
	// parameters and 13.1's index - which is keyed by the list as written -
	// cannot find it.  The question is the one `TemplateSignature` answers:
	// which declaration of this name has the same type once each head's
	// parameters stand for the places they were declared in.
	if (head != nullptr)
	{
		const SemaEntity* const templated =
			TemplateSignature(*this).equivalent(*declared, *head, type);
		return templated != nullptr && !templated->object_member;
	}
	// 13.1's index of a class's chain is keyed by the list the declarator wrote,
	// and a definition of a static member function writes the same list its
	// declaration did.
	const SemaEntity* const prior =
		model_.overload_of(*declared, member_signature(type, false));
	return prior != nullptr && !prior->object_member;
}

// 8.4.2p1 and 8.5p7: whether the program wrote a constructor of this class -
// one that is neither implicitly declared nor explicitly defaulted or deleted
// on its first declaration.  That is what 8.5p7 asks before it zero-initializes
// a value-initialized object, and it is one walk of the declarations of the
// name rather than a search.
bool SemaAnalyzer::user_provided_constructor(const SemaEntity& head)
{
	for (const SemaEntity* at = &head; at != nullptr; at = at->next)
	{
		if (!at->defaulted && !at->deleted)
		{
			return true;
		}
	}
	return false;
}

// 12.1p5 and 3.9p6: the constructors a class has, which is a question about a
// complete class - 14.7.1p1 asks a specialization that only a typedef-name has
// named so far for its definition here, because every place that builds an
// object of a class asks this one first.
SemaEntity* SemaAnalyzer::class_constructors(TypeId type)
{
	if (!types_.is_class(types_.strip_cv(type)))
	{
		return nullptr;
	}
	require_complete_type(type);
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	return owner == nullptr ? nullptr : owner->constructor;
}

// 8.5.1p1: whether an object of `type` is initialized from a braced-init-list
// by initializing its members with the clauses rather than by a constructor.
bool SemaAnalyzer::aggregate_type(TypeId type)
{
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	return owner != nullptr && owner->aggregate;
}

SemaEntity* SemaAnalyzer::class_destructor(TypeId type)
{
	if (!types_.is_class(types_.strip_cv(type)))
	{
		return nullptr;
	}
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	return owner == nullptr ? nullptr : owner->destructor;
}
