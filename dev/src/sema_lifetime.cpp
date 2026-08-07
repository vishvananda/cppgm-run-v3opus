#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"

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

// True for the nodes that hold the arguments an initializer wrote rather than
// one expression: the parenthesised forms an initializer, a call and a
// mem-initializer each spell, and the braced-init-list 8.5.4 writes.
bool is_initializer_list(AstKind kind)
{
	return kind == AstKind::ParenInitializer ||
		kind == AstKind::ParenArgumentList || kind == AstKind::ArgumentList ||
		kind == AstKind::BracedInitList;
}

// The argument list of a call or of a mem-initializer, in either of the two
// spellings PA10 writes one as.
const AstNode* call_arguments(const AstNode& node)
{
	const AstNode* list = child_of(node, AstKind::ArgumentList);
	return list != nullptr ? list : child_of(node, AstKind::ParenArgumentList);
}

}

// What one object of class type costs the program: the calls that begin and end
// its lifetime, and the region that owes them.
//
// `sema_class.cpp` says what a class *is* - what its objects hold, who may name
// each part, and which special member functions it has.  This says what running
// them comes to: 12.1p5 and 8.5 choose the constructor an initializer names,
// 12.6.2p10 puts the base and the members it initializes in the order 9.2p13
// laid them out, 12.4p8 destroys them in the reverse of it, 12.2p1 gives a
// prvalue of class type storage of its own, and 3.7.1/3.8p1 say which region
// ends the lifetime of an object a declaration named.
//
// The two halves are split here because the seam is real: everything above is
// a fact of the class, settled once where 9.2p2 completes it, and everything
// below is a fact of one object, written where the program names it.  A
// question about a subobject is still asked in the same words wherever the
// subobject stands - a base, a member, or an object of its own - so both halves
// read the same `SemaEntity` and neither re-reads syntax the other read.

// The object a constructor-action runs on, written as its address, which is the
// argument 9.3.1p3 made the constructor's first parameter.
void SemaAnalyzer::write_constructed_object(SemaEntity& variable,
                                            DumpNode& call, Placement where,
                                            Value& object, TypeId object_type)
{
	if (where == Placement::Base)
	{
		// 12.6.2p5: the base class subobject of the object this constructor is
		// running on, whose address is what 4.10p3's conversion of `this`
		// already is - so no address is taken around it.
		object = base_value(this_value(call), variable, false);
		return;
	}
	// 5.3.1p3 writes the address around the object, so the object's own line
	// stands under the one the address takes rather than in place of it.
	DumpNode& node = model_.open_node(call, std::string());
	DumpNode& inner = model_.open_node(node, std::string());
	if (where == Placement::Member)
	{
		// 12.6.2: the subobject is a member of the object the constructor being
		// written was called on, so it is named through `this`.
		object = member_value(variable, implied_object(variable, inner),
		                      variable.name, inner);
	}
	else
	{
		object.type = object.spelled = variable.type;
		object.category = ValueCategory::LValue;
		object.what = "id-expression";
		object.entity = &variable;
		object.payload = variable.name;
		object.node = &inner;
		respell(object);
	}
	// 12.6p1: what the constructor runs on is one object of its own class,
	// which for an array of class type is an element rather than the array the
	// line names.  The line keeps the array it was written from - that is where
	// the element is - and the object 9.3.1p3 gives the constructor is the
	// element's.
	object.type = object.spelled = object_type;
	address_of_object(object, node, false);
}

// 12.6p1 and 8.5p7: whether what is declared is an array of class type whose
// elements are created by constructing each of them, which is what a
// declaration that wrote no clause for any element asks for - no initializer at
// all, and the empty `()` or `{}` that value-initializes every element.  A
// clause of its own initializes the element it reached, which 8.5.1 writes
// where the clauses are read.
bool SemaAnalyzer::element_constructed(TypeId type, const AstNode* written)
{
	if (types_.kind(types_.strip_cv(type)) != TypeKind::Array ||
	    !types_.is_class(element_of(type)))
	{
		return false;
	}
	return written == nullptr ||
		(is_initializer_list(written->kind) && written->children.empty());
}

// 12.8p32: a copy 12.8p31 elides is still a copy the program wrote, so the
// constructor 13.3 would have chosen for it has to be one this region may name
// and one the standard has a definition for.  The elision says the call does
// not run, not that the program did not have to be allowed to write it.  The
// initializer is a prvalue, so what 13.3 chooses is the class's move
// constructor where it has one and its copy constructor otherwise - which is
// the one fact 12.8p15 already settled on the class rather than a resolution
// run again here.
void SemaAnalyzer::require_elided_transfer(TypeId type, const Context& ctx)
{
	SemaEntity* const chosen = selected_transfer(type, kMoveConstructorTransfer);
	if (chosen == nullptr || chosen->deleted)
	{
		throw std::runtime_error(
			"an object of " + types_.description(types_.strip_cv(type)) +
			" is initialized from a value of its own class, whose copy 12.8p32 "
			"asks for a constructor the program has none of");
	}
	if (ctx.scope != nullptr && !accessible(*chosen, *ctx.scope))
	{
		throw std::runtime_error(
			"an object of " + types_.description(types_.strip_cv(type)) +
			" is initialized from a value of its own class, whose copy 12.8p32 "
			"asks for a constructor the access its class gave does not reach");
	}
}

// 8.5, 12.1 and 13.3.1.3: an object of class type is initialized by one of the
// constructors of its class, chosen from the arguments its initializer wrote.
// The action is one call like any other, written under the declaration of the
// object, and the definition of the constructor it names is asked for here.
void SemaAnalyzer::construct_object(SemaEntity& variable, DumpNode& line,
                                    const AstNode* written, const Context& ctx,
                                    Placement where, bool copied,
                                    const Value* given, bool value_init,
                                    const std::vector<SemaEntity*>* forwarded)
{
	const bool member = where != Placement::Named;
	// 12.6p1: an array of class type is initialized element by element, and
	// each element is one object of the element's class.  The one constructor
	// every element is given is chosen once, here, from the type of an element;
	// the action names the array, so how many objects it creates is what the
	// declared type says rather than a count written beside it.
	const TypeId object_type = element_of(variable.type);

	if (!types_.is_class(types_.strip_cv(object_type)))
	{
		// 8.5p6: default-initializing an object of any other type performs no
		// initialization, and there is nothing for the output to describe.
		return;
	}
	SemaEntity* const head = class_constructors(object_type);
	if (head == nullptr)
	{
		// 3.9p6 and 9.2p2: an object needs a complete class, and 12.1p5 gives
		// every complete one the output describes a constructor, so a class
		// with none here is one this translation unit never defined.
		throw std::runtime_error("an object of the incomplete class type " +
		                         types_.description(object_type) +
		                         " is declared");
	}
	// 8.5p15 and 8.5p16: which of the arguments the program wrote reach the
	// constructor, and whether 13.3.1.4 leaves out the ones declared `explicit`.
	const AstNode* list = nullptr;
	bool converting = false;
	// 12.8p31 and 5.2.3p1: whether the initializer is a prvalue written
	// `T(a, b)` or `T{...}` that this object is created by rather than copied
	// from.
	bool elided_prvalue = false;
	// 8.5p14 and 8.5p16: only `= { ... }` is copy-list-initialization.  `= e`
	// is copy-initialization, which 13.3.1.4 answers by leaving the `explicit`
	// constructors out of the candidates rather than by refusing one, and
	// `= T(...)` is the direct-initialization 12.8p31 elides into.
	const bool copy_list =
		copied && written != nullptr && written->kind == AstKind::BracedInitList;
	if (written != nullptr)
	{
		if (is_initializer_list(written->kind))
		{
			list = written;
			// 8.5p7: `()` and `{}` value-initialize the object rather than
			// naming an argument for a constructor.
			value_init = list->children.empty();
		}
		else
		{
			// 8.5p14: copy-initialization from one expression, which only a
			// converting constructor may answer.
			converting = true;
		}
	}
	if (converting && written->kind == AstKind::CallExpression &&
	    !written->children.empty() &&
	    written->children[0]->kind == AstKind::IdExpression)
	{
		// 12.8p31 and 5.2.3p1: a class object copy-initialized from a prvalue
		// of its own type is initialized by whatever makes that prvalue, so the
		// arguments of `T(...)` are the constructor's and no object of the type
		// stands between them.
		SemaEntity* const named =
			resolve(written->children[0]->text, ctx, LookupKind::Type);
		if (named != nullptr && names_a_type(*named) &&
		    types_.strip_cv(named->type) == types_.strip_cv(object_type))
		{
			list = call_arguments(*written);
			if (list != nullptr && list->children.size() == 1 &&
			    list->children[0]->kind == AstKind::BracedInitList)
			{
				// 5.2.3p3: the prvalue was written `T{...}`, whose one braced
				// list stands where the arguments of `T(...)` do.  What
				// initializes the object is that list, so 8.5.4 reads it and
				// 8.5.1 gives its clauses to the members of an aggregate.
				list = list->children[0];
			}
			// 5.2.3p2 leaves `T()` the value-initialization 8.5p7 writes where
			// it stands rather than an object something was built into, so it
			// is the one spelling of the prvalue this is not: `T(a, b)` names
			// arguments and `T{...}` names braces, and either of them is what
			// the object was created by.
			elided_prvalue = list != nullptr &&
				(list->kind == AstKind::BracedInitList || !list->children.empty());
			converting = false;
			// 5.2.3p2: `T()` value-initializes what it makes, and the grammar
			// writes no argument-list node for one that has no arguments.
			value_init = list == nullptr || list->children.empty();
		}
	}

	Value source;
	if (given != nullptr)
	{
		// 13.3.3.1.2p1: the one argument was analysed where the program wrote
		// it, so it is taken as it stands.  Its line is not held by this one
		// yet, so nothing is taken back out of what this line already holds.
		source = *given;
		converting = true;
	}
	else if (converting)
	{
		// 8.5p14: the initializer is read before anything is written for the
		// initialization, because 12.8p31 lets a value of the object's own type
		// be what initializes it, with no constructor standing between them.
		source = expression(*written, ctx, line);
		if (types_.strip_cv(source.type) == types_.strip_cv(object_type) &&
		    !member && source.category == ValueCategory::PRValue)
		{
			// 12.8p31: the initializer is a prvalue of the object's own class,
			// so the object it denotes and the one being initialized may be one
			// and nothing stands between them.  A glvalue names an object that
			// goes on existing, so 8.5p14 leaves the initialization the call of
			// the copy or move constructor 13.3 chooses for it.
			require_elided_transfer(object_type, ctx);
			return;
		}
		line.children.pop_back();
	}
	DumpNode& action = model_.open_node(line, std::string());
	action.fact.kind = FactKind::ConstructorAction;
	action.fact.type = variable.type;
	action.fact.elided_prvalue = elided_prvalue;
	action.fact.base_subobject = where == Placement::Base;
	action.fact.subobject_step = where != Placement::Named;
	DumpNode& call = model_.open_node(action, std::string());
	DumpNode& callee = model_.open_node(call, std::string());
	Value object;
	write_constructed_object(variable, call, where, object, object_type);
	std::vector<Value> arguments;
	if (forwarded != nullptr)
	{
		// 12.9p8: the arguments are this constructor's own parameters, each
		// named as the program naming it would be, in declaration order.
		for (std::size_t index = 0; index < forwarded->size(); ++index)
		{
			arguments.push_back(parameter_value(*(*forwarded)[index], call));
		}
	}
	else if (list != nullptr)
	{
		for (std::size_t index = 0; index < list->children.size(); ++index)
		{
			arguments.push_back(expression(*list->children[index], ctx, call));
		}
	}
	if (source.node != nullptr)
	{
		// The one argument of a copy-initialization was read before the action
		// was opened, so its line moves into the place the call gives it.
		call.children.push_back(source.node);
		arguments.push_back(source);
	}

	std::vector<SemaEntity*> candidates(1, head);
	SemaEntity& constructor = *select_overload(candidates, arguments,
	                                           head->name, &object, converting);
	require_access(constructor, ctx.scope);
	if (copy_list && constructor.explicit_function)
	{
		// 8.5.4p3: copy-list-initialization that chooses an `explicit`
		// constructor is ill formed, which is not the same as leaving one out
		// of the candidates: the choice is made and then refused.
		throw std::runtime_error("a copy-list-initialization of " +
		                         types_.description(variable.type) +
		                         " chooses a constructor declared explicit");
	}
	if (constructor.deleted && !(value_init && constructor.trivial))
	{
		// 8.4.3p2: a program that names a deleted function is ill formed.
		// 8.5p7 names none: a class with no user-provided constructor is
		// value-initialized by the zero of its storage, and where the default
		// constructor is trivial that zero is the whole initialization.
		throw std::runtime_error("a deleted constructor of " +
		                         types_.description(variable.type) +
		                         " is what initializes an object of it");
	}
	if (where != Placement::Base)
	{
		// 12.1 and the ABI: what this action creates is a complete object -
		// a named one, or the member subobject that is one of its own - so it
		// runs the complete-object entry of the constructor.  A base class
		// subobject is the one case that runs the base-object entry instead.
		constructor.complete_object_entry = true;
	}
	else
	{
		constructor.base_object_entry = true;
	}
	if (where != Placement::Named && !constructor.trivial)
	{
		// 15.2p2: this step builds a subobject an exception out of a later one
		// leaves standing, which is what odr-uses its destructor.
		record_unwind_subobject(variable.type);
	}
	const std::vector<TypeId>& parameters = types_.parameters(constructor.type);
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		if (index + 1 >= parameters.size())
		{
			// 5.2.2p7: the constructor was declared with an ellipsis and this
			// argument is one no parameter names, so it is passed as it stands.
			require_complete_value(arguments[index]);
			continue;
		}
		const Match match = match_argument(arguments[index],
		                                   parameters[index + 1]);
		apply_conversion(arguments[index], parameters[index + 1], match, ctx,
		                 Requested::Argument);
	}
	for (std::size_t index = arguments.size() + 1; index < parameters.size();
	     ++index)
	{
		// 8.3.6p1: the constructor is called as if the default-argument had
		// been written where the argument is missing.
		write_default_argument(constructor, index, call);
	}
	action.text = "constructor-action " + constructor.dump_name;
	action.fact.entity = &constructor;
	// 8.5p7: a non-union class with no user-provided constructor is
	// zero-initialized before its default constructor - which is the one this
	// initialization chose - runs on it.  The zero is what the object holds
	// wherever that constructor leaves a member alone, so it is written even
	// where the constructor itself does nothing at all.
	action.fact.zero_initialized = value_init && !user_provided_constructor(*head);
	call.text = spell("call-expression", ValueCategory::PRValue,
	                  types_.target(constructor.type), std::string());
	set_fact(call, FactKind::Call, types_.target(constructor.type),
	         ValueCategory::PRValue);
	callee.text = "callee " + constructor.dump_name + " " +
		types_.description(constructor.type);
	set_fact(callee, FactKind::Callee, constructor.type, ValueCategory::LValue);
	callee.fact.entity = &constructor;
	demand_constructor_definition(constructor);
}

// 12.8p31 and 5.2.3p3: an initializer written `T{...}` for an object that is
// itself of `T` creates that very object - the prvalue and what it initializes
// are one, so nothing stands between the braces and the object.  What is left
// initializing it is the braced-init-list, which is the same list a declarator
// could have carried: 8.5.1 gives its clauses to the members of an aggregate
// and 13.3.1.7 gives them to a constructor of any other class.  Told from
// `T(...)` by the one braced clause standing where the arguments do, which is
// exactly what 5.2.3p3 wrote.
const AstNode* SemaAnalyzer::braced_prvalue_of(const AstNode& written,
                                               TypeId type, const Context& ctx)
{
	if (written.kind != AstKind::CallExpression || written.children.empty() ||
	    written.children[0]->kind != AstKind::IdExpression ||
	    !types_.is_class(types_.strip_cv(type)))
	{
		return nullptr;
	}
	const AstNode* const list = call_arguments(written);
	if (list == nullptr || list->children.size() != 1 ||
	    list->children[0]->kind != AstKind::BracedInitList)
	{
		return nullptr;
	}
	SemaEntity* const named =
		resolve(written.children[0]->text, ctx, LookupKind::Type);
	if (named == nullptr || !names_a_type(*named) ||
	    types_.strip_cv(named->type) != types_.strip_cv(type))
	{
		return nullptr;
	}
	return list->children[0];
}

// 8.5.1p2 and 13.3.1.7: the constructor an object of an aggregate class is
// built by where it is an object of its own - an element of an array of the
// class, a temporary, an argument - rather than a subobject the enclosing
// list's clauses reach into.  The class declared no constructor that could take
// the clauses, so the one it is given takes its non-static data members: each
// by value, in the order 9.2p13 laid them out, under the name its declaration
// wrote.  The class owns it and it is declared once, so n elements of an array
// are n calls of one function rather than n copies of the same stores.
SemaEntity* SemaAnalyzer::member_constructor(TypeId type)
{
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(type));
	if (owner == nullptr || owner->scope == nullptr || !owner->aggregate)
	{
		return nullptr;
	}
	if (owner->member_constructor != nullptr)
	{
		return owner->member_constructor;
	}
	Scope& scope = *owner->scope;
	std::vector<TypeId> types;
	types.push_back(types_.pointer_to(owner->type));
	std::vector<Parameter> named;
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		const TypeId bare = types_.strip_cv(member.type);
		if (member.bit_field || types_.is_reference(member.type) ||
		    types_.kind(bare) == TypeKind::Array || types_.is_class(bare))
		{
			// 9.6p1, 8.3.2p4 and 8.3.5p5: none of these is a member a by-value
			// parameter carries what the member holds - a bit-field and a
			// reference are no object of their own, and an array parameter is
			// the pointer 8.3.5p5 adjusts it to.  A class member would be one
			// object built to be copied into another, which is a second object
			// this milestone does not write, so the class is given no such
			// constructor and 8.5.1 initializes its subobjects where they are.
			return nullptr;
		}
		Parameter one;
		// 8.3.5p5: the parameter is what a declaration of it with the member's
		// own type would be, so an array member contributes a pointer and the
		// member's own cv-qualification is dropped.
		one.type = types_.adjust_parameter(member.type);
		one.name = member.name;
		named.push_back(one);
		types.push_back(one.type);
	}
	const std::string spelled =
		QualifiedName(types_.user_name(owner->type)).last();
	SemaEntity& constructor = model_.create(
		SemaKind::Function, spelled,
		types_.function_of(types_.fundamental(FT_VOID), types, false));
	name_in_region(constructor, scope, spelled);
	constructor.object_member = true;
	// 7.1.2p3: the definition is one the standard rather than the program
	// writes, so it belongs to every unit that needs one.
	constructor.inline_function = true;
	constructor.tail = &constructor;
	constructor.special = kConstructorFunction;
	constructor.defaulted = true;
	constructor.member_entry = true;
	constructor.region = &scope;
	constructor_parameters_[constructor.id] = named;
	owner->member_constructor = &constructor;
	return &constructor;
}

// 8.5.1p11: whether the clause initializes the whole subaggregate rather than
// the first of its members.  8.5.1p2 copy-initializes the member from the
// clause, so a value of the member's own class type - or of a class derived
// from it, which 12.8p31 and 4.10p3 reach the same object through - is what
// initializes it on its own; anything else is a clause of the enclosing list
// that 8.5.1p11's elided braces left standing for the subaggregate's first
// member.  The clause is read into a line nothing keeps, because which subobject
// it belongs under is exactly what this answers.
bool SemaAnalyzer::clause_initializes_class(TypeId type, const AstNode& clause,
                                            const Context& ctx)
{
	if (clause.kind == AstKind::BracedInitList)
	{
		return true;
	}
	DumpNode scratch;
	Value value;
	try
	{
		value = expression(clause, ctx, scratch);
	}
	catch (const std::runtime_error&)
	{
		// A clause that is no expression of its own here is one the members
		// below read; whatever is wrong with it is reported where it is read.
		return false;
	}
	const TypeId from = types_.strip_cv(value.type);
	return from == types_.strip_cv(type) || derived_from(from, type) != nullptr;
}

// 8.5.1p2: the object an aggregate class's own constructor builds, whose
// arguments are what the clauses initialize its members with.  Which clause
// reaches which member is 8.5.1's walk of the list rather than 13.3's matching,
// and there is one candidate, so the call is written here rather than chosen.
// 8.5.1p7 gives a member no clause reached the value-initialization every other
// subobject of an aggregate gets.
void SemaAnalyzer::construct_from_members(SemaEntity& constructor,
                                          const AstNode& list,
                                          const Context& ctx, DumpNode& parent)
{
	Scope& scope = *constructor.region;
	const std::vector<TypeId>& parameters = types_.parameters(constructor.type);
	DumpNode& action = model_.open_node(
		parent, "constructor-action " + constructor.dump_name);
	action.fact.kind = FactKind::ConstructorAction;
	action.fact.type = types_.target(parameters[0]);
	action.fact.entity = &constructor;
	DumpNode& call = model_.open_node(
		action,
		spell("call-expression", ValueCategory::PRValue,
		      types_.target(constructor.type), std::string()));
	set_fact(call, FactKind::Call, types_.target(constructor.type),
	         ValueCategory::PRValue);
	DumpNode& callee = model_.open_node(
		call, "callee " + constructor.dump_name + " " +
		types_.description(constructor.type));
	set_fact(callee, FactKind::Callee, constructor.type, ValueCategory::LValue);
	callee.fact.entity = &constructor;
	// The object the call runs on is named by the place asking for it - the
	// element of the array, the storage a temporary took - so the call holds
	// the place that argument stands in and nothing else.
	model_.open_node(call, std::string());
	Clauses clauses(list);
	std::size_t at = 1;
	for (std::size_t index = 0; index < scope.declarations.size(); ++index)
	{
		SemaEntity& member = *scope.declarations[index];
		if (!declares_subobject(member, scope))
		{
			continue;
		}
		if (clauses.spent())
		{
			// 8.5.1p7: no clause reached the member, so what the constructor is
			// passed for it is the value-initialization of its own type.
			DumpNode& zero = model_.open_node(
				call, spell("literal", ValueCategory::PRValue, parameters[at],
				            "0"));
			set_fact(zero, FactKind::Literal, parameters[at],
			         ValueCategory::PRValue);
			zero.fact.constant = true;
			++at;
			continue;
		}
		initialize(clauses.next(), parameters[at], ctx, call, true,
		           Requested::Argument);
		++clauses.at;
		++at;
	}
	if (!clauses.spent())
	{
		// 8.5.1p6: a clause that reached no member initializes nothing.
		throw std::runtime_error("an initializer list has more clauses than the "
		                         "aggregate has subobjects");
	}
	// 12.1 and the ABI: what the action creates is a complete object, so it
	// runs the complete-object entry of the constructor.
	constructor.complete_object_entry = true;
	demand_constructor_definition(constructor);
}

// 8.5.1p2 and 8.5.1p7: one subobject of an aggregate whose type is a class the
// clauses do not reach into - a class with a constructor of its own, or one a
// clause of its own class type initializes.  The subobject is an object like
// any other: 8.5 chooses what initializes it, and what the node carries is the
// `constructor-action` a declaration of the same object would carry.  It is
// named by the path the aggregate initialization already walks rather than by a
// declaration, so the object this creates is one no name reaches.
void SemaAnalyzer::construct_subobject(TypeId type, const AstNode* written,
                                       const Context& ctx, DumpNode& node,
                                       bool value_init,
                                       unsigned long long elements)
{
	const std::size_t before = node.children.size();
	SemaEntity& object = model_.create(SemaKind::Variable, std::string(), type);
	object.object_member = false;
	construct_object(object, node, written, ctx, Placement::Named, true, nullptr,
	                 value_init);
	if (elements > 1 && node.children.size() > before &&
	    node.children.back()->fact.kind == FactKind::ConstructorAction)
	{
		// 8.5.1p7: the one action is this many elements of the array, which is
		// the only thing that differs between them.
		node.children.back()->fact.elements = elements;
	}
}

// 12.1p5, 12.9p6 and 3.2p3: a constructor the standard gives a class rather
// than the program has the definition the standard describes, and one use of it
// is what asks this unit to write one.  A constructor the program declared and
// did not define is one this unit has no body for, so a use of it is a call of
// a definition elsewhere.
void SemaAnalyzer::demand_constructor_definition(SemaEntity& constructor)
{
	if (constructor.defined || constructor.deleted || !constructor.defaulted)
	{
		return;
	}
	constructor.defined = true;
	const std::vector<TypeId>& parameters = types_.parameters(constructor.type);
	Pending pending;
	pending.function = &constructor;
	pending.self = &model_.create(SemaKind::Parameter, "this", parameters[0]);
	pending.members = constructor.region;
	if (constructor.inherited != nullptr || constructor.member_entry ||
	    constructor.transfer != kNotTransfer)
	{
		// 12.9p8, 8.5.1p2 and 12.8p15: the definition writes parameters of its
		// own and names them in what it initializes - the base subobject for
		// one, each member for the other, the object being copied for the
		// third - so unlike 12.1p5's it has a region of its own for them to be
		// declared in.
		DumpScope& dump = model_.open_dump(*constructor.region->dump,
		                                   "scope function " + constructor.name);
		pending.scope = &model_.open(ScopeKind::Function, *constructor.region,
		                             &constructor, &dump);
		const std::unordered_map<std::uint32_t,
		                         std::vector<Parameter> >::const_iterator named =
			constructor_parameters_.find(constructor.id);
		if (named != constructor_parameters_.end())
		{
			pending.parameters = named->second;
		}
		else
		{
			// A base whose constructor this unit only saw declared through
			// another inheriting one still says what its parameters are; a name
			// is what it may not have said, and the output gives an unnamed
			// parameter one of its own.  A value-transfer member the standard
			// declared has one parameter and no declaration to have named it,
			// and its definition reads that object throughout, so it is named
			// here after what it is.
			for (std::size_t index = 1; index < parameters.size(); ++index)
			{
				Parameter written;
				written.type = parameters[index];
				if (constructor.transfer != kNotTransfer)
				{
					written.name = "other";
				}
				pending.parameters.push_back(written);
			}
		}
	}
	pending_.push_back(pending);
}

// 12.8p15 and p28: the definition a use of a value-transfer member the standard
// rather than the program gives a class asks this unit for.  A constructor
// reaches this through the initialization that chose it; an assignment operator
// through the name a call of it wrote, which is where every other function's
// definition is asked for too.
void SemaAnalyzer::demand_transfer_definition(SemaEntity& function)
{
	if (function.transfer == kNotTransfer ||
	    function.special == kConstructorFunction)
	{
		// A constructor is asked for where the object it builds is, because
		// only there is it known whether the complete-object or the base-object
		// entry of the ABI is the one being run.
		return;
	}
	demand_constructor_definition(function);
}

// 12.2p1: a prvalue of class type denotes an object, and no declaration named
// it, so the function it was written in has to give it storage.  The object is
// declared here, the constructor 8.5/13.3.1.3 chooses runs on it exactly as it
// would on one a declaration named, and what the expression is worth from then
// on is that object.
//
// 12.2p3's destruction at the end of the full-expression is not written here:
// the lowering marks no full-expression boundary yet, so a temporary of a class
// whose destructor does something is refused rather than left alive past the
// point 12.2p3 ends it.
SemaAnalyzer::Value SemaAnalyzer::materialize_temporary(TypeId type,
                                                        const AstNode* written,
                                                        const Context& ctx,
                                                        DumpNode& parent,
                                                        const char* prefix,
                                                        bool value_init)
{
	DumpNode& line = model_.open_node(parent, std::string());
	return build_temporary(type, line, written, nullptr, ctx, prefix,
	                       value_init);
}

SemaAnalyzer::Value SemaAnalyzer::build_temporary(TypeId type, DumpNode& line,
                                                  const AstNode* written,
                                                  const Value* given,
                                                  const Context& ctx,
                                                  const char* prefix,
                                                  bool value_init)
{
	const TypeId object_type = types_.strip_cv(type);
	SemaEntity& object = model_.create(SemaKind::Variable, std::string(),
	                                   object_type);
	object.object_member = false;
	set_fact(line, FactKind::TemporaryObject, object_type,
	         ValueCategory::PRValue);
	line.fact.entity = &object;
	line.fact.spelling = prefix;
	// 8.5.1p2 and 13.3.1.7: a prvalue of an aggregate class written with braces
	// is an object of its own rather than a subobject an enclosing list reaches
	// into, so the clauses initialize its members through the constructor 8.5.1
	// gives the class from them - the same one an element of an array of the
	// class is built by, declared once and held on the class.
	SemaEntity* const from_members =
		given == nullptr && written != nullptr &&
		written->kind == AstKind::BracedInitList &&
		!written->children.empty() && aggregate_type(object_type)
			? member_constructor(object_type)
			: nullptr;
	if (from_members != nullptr)
	{
		construct_from_members(*from_members, *written, ctx, line);
	}
	else
	{
		construct_object(object, line, written, ctx, Placement::Named, false,
		                 given, value_init);
	}
	const SemaEntity* const destructor = class_destructor(object_type);
	if (destructor != nullptr && !destructor->trivial)
	{
		// 12.2p3 ends the temporary's lifetime at the end of the
		// full-expression, which is a place this milestone does not mark.
		throw std::runtime_error(
			"a temporary of the class type " + types_.description(object_type) +
			" is created, whose destructor 12.2p3 runs at a point this "
			"milestone does not mark");
	}
	line.text = spell("temporary-object", ValueCategory::PRValue, object_type,
	                  std::string());
	Value value;
	value.type = object_type;
	value.spelled = object_type;
	value.category = ValueCategory::PRValue;
	value.what = "temporary-object";
	value.entity = &object;
	value.node = &line;
	return value;
}

// 8.5.3p5 and 13.3.3.1.2: the storage a temporary takes is named after the
// argument that asked for it.  A temporary something already read as the object
// it is - a base subobject of it, a member of it - keeps the name it was given
// where it was written, because the argument is no longer what made it.
void SemaAnalyzer::name_argument_temporary(const Value& value,
                                           const char* prefix)
{
	if (value.node != nullptr &&
	    value.node->fact.kind == FactKind::TemporaryObject)
	{
		value.node->fact.spelling = prefix;
	}
}

// 9.3.2p1: the type `this` has in the body of a member function, which is a
// pointer to the class qualified by the function's own cv-qualifier-seq.  For
// every member function but one that is the object parameter 9.3.1p3 gives the
// function's type.  12.4p1 gives a destructor no cv-qualifier-seq at all, and
// the const volatile 12.4p12 puts on its object parameter says which objects it
// may be called for rather than what its body may do to the one it is
// destroying, so a destructor's `this` drops it.
TypeId SemaAnalyzer::this_type(const SemaEntity& function)
{
	const TypeId object = types_.parameters(function.type)[0];
	if (function.special != kDestructorFunction)
	{
		return object;
	}
	return types_.pointer_to(types_.strip_cv(types_.target(object)));
}

// 12.4p3 and 3.8p1: the end of the lifetime of an object of class type is one
// call of the destructor of its class on it.  A destructor that does nothing is
// no action at all, so nothing is written for one.
void SemaAnalyzer::destructor_action(SemaEntity& entity, DumpNode& parent,
                                     Placement where)
{
	SemaEntity* const destructor = class_destructor(element_of(entity.type));
	if (destructor == nullptr)
	{
		return;
	}
	if (destructor->deleted)
	{
		// 8.4.3p2 and 12.4p11: a program that names a deleted function is ill
		// formed, and the end of an object's lifetime is what names its
		// destructor - so declaring the object is what is refused, rather than
		// its lifetime ending in a call of a definition nothing writes.
		throw std::runtime_error("an object of " +
		                         types_.description(entity.type) +
		                         " is declared, and the destructor its lifetime "
		                         "ends with is deleted");
	}
	if (destructor->trivial)
	{
		return;
	}
	note_destruction_entry(*destructor, where == Placement::Base);
	DumpNode& action = model_.open_node(
		parent, "destructor-action " + destructor->dump_name);
	action.fact.kind = FactKind::DestructorAction;
	action.fact.entity = destructor;
	action.fact.type = entity.type;
	action.fact.base_subobject = where == Placement::Base;
	// 12.4p8: an array of class type is as many objects as it has elements and
	// the destructor runs on each of them.  The action names the array, which
	// says how many they are; a member subobject is destroyed in the reverse of
	// the order 12.6.2p10 created it in, and the elements of one an enclosing
	// block declared are left in the order the references end them in.
	action.fact.reverse_elements =
		where != Placement::Named &&
		types_.kind(types_.strip_cv(entity.type)) == TypeKind::Array;
	if (where == Placement::Base)
	{
		// 12.4p8: the base class subobject of the object being destroyed, which
		// 4.10p3's conversion of `this` names.
		base_value(this_value(action), entity, false);
	}
	else if (where == Placement::Member)
	{
		DumpNode& node = model_.open_node(action, std::string());
		member_value(entity, implied_object(entity, node), entity.name, node);
	}
	else
	{
		DumpNode& node = model_.open_node(action, std::string());
		Value object;
		object.type = object.spelled = entity.type;
		object.category = ValueCategory::LValue;
		object.what = "id-expression";
		object.entity = &entity;
		object.payload = entity.name;
		object.node = &node;
		respell(object);
	}
}

// 12.6.2p2: whether a mem-initializer-id spells the base class of the
// constructor's own class.  The base has a name of its own and every alias of
// it names it too, so the question is asked of what the name denotes rather
// than of the characters it was written with.
bool SemaAnalyzer::names_the_base(const std::string& written,
                                  const SemaEntity& base, const Context& ctx)
{
	try
	{
		SemaEntity* const found = resolve(written, ctx, LookupKind::Type);
		return found != nullptr && names_a_type(*found) &&
			types_.strip_cv(found->type) == types_.strip_cv(base.type);
	}
	catch (const std::runtime_error&)
	{
		// A name that reaches no region names no base either, and the
		// mem-initializer it was written in is refused where it is read.
		return false;
	}
}

// 12.6.2: what a constructor initializes before its body runs.  Every non-static
// data member of the class is initialized, in the declaration order 12.6.2p10
// gives them whatever order the mem-initializers were written in: by the
// mem-initializer that names it, else by the brace-or-equal-initializer its own
// declaration wrote (12.6.2p8), else by default-initialization, which for
// anything but a class type leaves it holding no value the program may read.
void SemaAnalyzer::write_member_initializations(const Pending& pending,
                                                DumpNode& line,
                                                const Context& inner)
{
	Scope& members = *pending.members;
	if (pending.function->member_entry)
	{
		// 8.5.1p2: the constructor an aggregate was given initializes each of
		// its members with the parameter of the same name, in the one order
		// 12.6.2p10 and 9.2p13 share.
		write_member_parameters(pending, line);
		return;
	}
	if (pending.function->transfer != kNotTransfer && pending.function->defaulted)
	{
		// 12.8p15: a copy or move constructor the standard defines initializes
		// each subobject from the corresponding subobject of its parameter,
		// rather than from a mem-initializer the program wrote.
		write_transfer_steps(pending, line, inner);
		return;
	}
	// 12.6.2p10: the members are initialized in declaration order and the
	// mem-initializers may be written in any, so which one names each member is
	// asked once per member rather than by a scan of the list per member.
	std::unordered_map<std::string, MemInitializer> named;
	for (std::size_t at = 0;
	     pending.initializers != nullptr &&
	     at < pending.initializers->children.size(); ++at)
	{
		const AstNode& one = *pending.initializers->children[at];
		const AstNode* const id = child_of(one, AstKind::MemInitializerId);
		if (id == nullptr)
		{
			continue;
		}
		// 12.6.2p2: the mem-initializer's arguments are read in the
		// constructor's own region, where its parameters stand.
		MemInitializer wrote;
		wrote.written = one.children.size() > 1 ? one.children[1] : nullptr;
		wrote.spelled = id->text;
		if (!named.insert(std::make_pair(QualifiedName(id->text).last(), wrote))
		         .second)
		{
			// 12.6.2p6: a ctor-initializer that writes more than one
			// mem-initializer for one member is ill formed, which is not the
			// same as the second one being the one that has no effect.
			throw std::runtime_error("a constructor of " +
			                         types_.description(pending.function->type) +
			                         " initializes " + id->text + " twice");
		}
	}
	// 12.6.2p10: the base class subobject is initialized first, whatever place
	// its mem-initializer was written in and whether or not one was.
	SemaEntity* const base =
		members.owner != nullptr ? members.owner->base : nullptr;
	if (base != nullptr && pending.function->inherited != nullptr)
	{
		// 12.9p8: an inheriting constructor initializes the base subobject by
		// calling the constructor it was declared from, with its own parameters
		// as the arguments, and writes no ctor-initializer of its own.  The
		// parameters are the declarations the definition just made, in the
		// order the declaration wrote them.
		std::vector<SemaEntity*> forwarded;
		for (std::size_t index = 0;
		     index < pending.scope->declarations.size(); ++index)
		{
			SemaEntity& parameter = *pending.scope->declarations[index];
			if (parameter.kind == SemaKind::Parameter)
			{
				forwarded.push_back(&parameter);
			}
		}
		construct_object(*base, line, nullptr, inner, Placement::Base, false,
		                 nullptr, false, &forwarded);
	}
	else if (base != nullptr)
	{
		const AstNode* written = nullptr;
		const std::string spelled =
			QualifiedName(types_.user_name(base->type)).last();
		std::unordered_map<std::string, MemInitializer>::iterator wrote =
			named.find(spelled);
		if (wrote == named.end())
		{
			// 12.6.2p2: the mem-initializer-id may be any name for the base
			// class, which a typedef-name is one of.  Its own name is asked
			// about first, so only a ctor-initializer that spelled the base some
			// other way costs one lookup per mem-initializer it wrote.
			for (wrote = named.begin(); wrote != named.end(); ++wrote)
			{
				if (names_the_base(wrote->second.spelled, *base, inner))
				{
					break;
				}
			}
		}
		if (wrote != named.end())
		{
			written = wrote->second.written;
			wrote->second.used = true;
		}
		if (written != nullptr || !trivially_constructed(base->type))
		{
			construct_object(*base, line, written, inner, Placement::Base);
		}
	}
	for (std::size_t index = 0; index < members.declarations.size(); ++index)
	{
		SemaEntity& member = *members.declarations[index];
		if (!declares_subobject(member, members))
		{
			continue;
		}
		const AstNode* written = nullptr;
		Context where = inner;
		const std::unordered_map<std::string, MemInitializer>::iterator
			wrote = named.find(member.name);
		if (wrote != named.end())
		{
			written = wrote->second.written;
			wrote->second.used = true;
		}
		if (written == nullptr && member.default_initializer)
		{
			// 12.6.2p8 and 9.2p2: a brace-or-equal-initializer is read in the
			// class it was written in, which is a complete-class context.
			const std::unordered_map<std::uint32_t, Default>::const_iterator
				found = member_initializers_.find(member.id);
			if (found != member_initializers_.end())
			{
				written = found->second.written;
				where.scope = found->second.scope;
				where.dump = where.scope->dump;
			}
		}
		where.node = nullptr;
		const TypeId type = member.type;
		const bool braced =
			written != nullptr && written->kind == AstKind::BracedInitList;
		if ((types_.is_class(types_.strip_cv(type)) &&
		     !(braced && aggregate_type(type))) ||
		    element_constructed(type, written))
		{
			// 12.6.2p8 and 12.1p5: a member no initializer reaches is
			// default-initialized, and where that does nothing at all there is
			// no action to write and no subobject to name.  An array member is
			// 12.6p1's elements, each constructed by the one constructor the
			// action names.
			if (written == nullptr && trivially_constructed(type))
			{
				continue;
			}
			// The action names the member through `this`, so it needs no line
			// of its own to say which subobject is being initialized.
			construct_object(member, line, written, where, Placement::Member);
			continue;
		}
		if (written == nullptr)
		{
			// 8.5p6 and 12.6.2p8: a member of any other type that no
			// initializer reaches is default-initialized, which does nothing.
			continue;
		}
		DumpNode& node = open_fact(line, "member-initialization " + member.name +
		                           " " + types_.description(type),
		                           FactKind::MemberInitialization);
		node.fact.entity = &member;
		node.fact.type = type;
		node.fact.spelled = type;
		// 9.2p13: where the member is, is where its class put it, so the tree
		// names the object it is part of and the member it is, and nothing
		// below has to read a member access to learn either.
		implied_object(member, node);
		if (written->kind != AstKind::BracedInitList &&
		    is_initializer_list(written->kind))
		{
			// 8.5p16: direct-initialization of a member of non-class type takes
			// the one expression written in the parentheses.
			if (written->children.empty())
			{
				// 8.5p10: `m()` value-initializes the member, which for these
				// types is the zero of it.  It is the zero of an object rather
				// than a literal the program wrote, which is what says a
				// pointer takes 4.10p1's null pointer value and not the `0` a
				// null pointer constant is spelled with.
				DumpNode& zero = model_.open_node(
					node, spell("literal", ValueCategory::PRValue, type, "0"));
				set_fact(zero, FactKind::Literal, type, ValueCategory::PRValue);
				zero.fact.constant = true;
				zero.fact.zero_initialized = true;
				continue;
			}
			if (written->children.size() != 1)
			{
				throw std::runtime_error("a mem-initializer of " + member.name +
				                         " passes more than one argument to a "
				                         "member of non-class type");
			}
			initialize(*written->children[0], type, where, node);
			continue;
		}
		initialize(*written, type, where, node);
	}
	// 12.6.2p2: a mem-initializer-id shall name a non-static data member of the
	// constructor's class or one of its bases, so one that named neither is
	// refused rather than left as arguments nothing evaluates.  Every member
	// was reached above, so what is left unused is what named nothing.
	for (std::unordered_map<std::string, MemInitializer>::const_iterator at =
	         named.begin(); at != named.end(); ++at)
	{
		if (!at->second.used)
		{
			throw std::runtime_error("a constructor of " +
			                         types_.description(pending.function->type) +
			                         " has a mem-initializer for " +
			                         at->second.spelled +
			                         ", which names neither a base class nor a "
			                         "non-static data member of the class");
		}
	}
}

// 12.8p15 and p28: what a value-transfer member the standard rather than the
// program defines does - the base class subobject and then each non-static data
// member, in the one order 9.2p13 and 12.6.2p10 share, carried from the
// corresponding subobject of the object the parameter names.  A subobject of
// class type is carried by the member its own class has; any other subobject is
// carried by its storage, which for a copy or a move is 8.5's initialization of
// it and for an assignment is 5.17's.
//
// The one shape that is not one subobject at a time is the leading run of
// members whose bytes a copy carries exactly.  For a class with no base
// subobject that run begins where the object does, so the whole of it is one
// `copyobj` of the span it covers and only the members after it are named.
void SemaAnalyzer::write_transfer_steps(const Pending& pending, DumpNode& line,
                                        const Context& inner)
{
	SemaEntity& function = *pending.function;
	Scope& members = *pending.members;
	SemaEntity* parameter = nullptr;
	for (std::size_t index = 0;
	     pending.scope != nullptr && index < pending.scope->declarations.size();
	     ++index)
	{
		if (pending.scope->declarations[index]->kind == SemaKind::Parameter)
		{
			parameter = pending.scope->declarations[index];
			break;
		}
	}
	if (parameter == nullptr)
	{
		throw std::runtime_error("a value-transfer special member has no "
		                         "parameter to carry its object from");
	}
	const unsigned char kind = function.transfer;
	const bool assigning = kind == kCopyAssignmentTransfer ||
		kind == kMoveAssignmentTransfer;
	SemaEntity* const base =
		members.owner != nullptr ? members.owner->base : nullptr;
	std::vector<SemaEntity*> fields;
	for (std::size_t index = 0; index < members.declarations.size(); ++index)
	{
		SemaEntity& field = *members.declarations[index];
		if (declares_subobject(field, members))
		{
			fields.push_back(&field);
		}
	}
	std::size_t first = 0;
	if (base == nullptr)
	{
		// 12.8p15: the members carried as storage are carried in one piece, and
		// the piece is only the object's own storage where nothing stands
		// before them in it.
		unsigned long long span = 0;
		for (; first < fields.size(); ++first)
		{
			const SemaEntity& field = *fields[first];
			if (field.bit_field || !carried_as_storage(field.type, kind))
			{
				break;
			}
			const unsigned long long end =
				field.offset + types_.object_size(field.type);
			span = end > span ? end : span;
		}
		if (span != 0)
		{
			write_storage_transfer(*parameter, line, 0, span, kNoType);
		}
	}
	if (base != nullptr && !carries_nothing(base->type, kind))
	{
		Value source = base_value(parameter_value(*parameter, line), *base,
		                          false);
		line.children.pop_back();
		transfer_source(source, kind);
		if (assigning)
		{
			write_transfer_assignment(*base, source, line, inner, Placement::Base);
		}
		else
		{
			construct_object(*base, line, nullptr, inner, Placement::Base, true,
			                 &source);
		}
	}
	for (std::size_t index = first; index < fields.size(); ++index)
	{
		SemaEntity& field = *fields[index];
		if (field.bit_field)
		{
			// 9.6p2: the field is a run of bits in a storage unit it shares
			// with the fields beside it, and what carries all of them is one
			// read and one write of that unit.  The unit is named by the byte
			// it begins at, so a second field of the same unit is already done.
			if (index > 0 && fields[index - 1]->bit_field &&
			    fields[index - 1]->offset == field.offset)
			{
				continue;
			}
			write_storage_transfer(*parameter, line, field.offset,
			                       types_.object_size(field.type), field.type);
			continue;
		}
		if (carries_nothing(field.type, kind))
		{
			// 9p6 and 12.8p15: the subobject holds nothing and the member its
			// class has for this transfer does nothing, so there is neither a
			// call to write nor storage to carry.
			continue;
		}
		if (types_.kind(types_.strip_cv(field.type)) == TypeKind::Array &&
		    types_.is_class(member_copy_type(field.type)) &&
		    carried_as_storage(field.type, kind))
		{
			// 12.8p15: an array member is carried element by element, and where
			// the element's class carries one of them by its bytes the whole
			// array is those bytes - which is the one form the transfer has for
			// it, because the member the element's class holds takes an element
			// and no call of it takes the array.  A run the leading prefix
			// already covered never reaches here.
			write_storage_transfer(*parameter, line, field.offset,
			                       types_.object_size(field.type), kNoType);
			continue;
		}
		DumpNode& access = model_.open_node(line, std::string());
		Value source = member_value(field, parameter_value(*parameter, access),
		                            field.name, access);
		line.children.pop_back();
		transfer_source(source, kind);
		if (assigning)
		{
			write_transfer_assignment(field, source, line, inner,
			                          Placement::Member);
			continue;
		}
		if (types_.is_class(member_copy_type(field.type)))
		{
			construct_object(field, line, nullptr, inner, Placement::Member,
			                 true, &source);
			continue;
		}
		// 12.8p15: a member of any other type is initialized with the value the
		// corresponding member of the source holds, which is the same node an
		// ordinary mem-initializer for it would carry.
		DumpNode& node = open_fact(line, "member-initialization " + field.name +
		                           " " + types_.description(field.type),
		                           FactKind::MemberInitialization);
		node.fact.entity = &field;
		node.fact.type = field.type;
		node.fact.spelled = field.type;
		implied_object(field, node);
		node.children.push_back(source.node);
	}
}

// 12.8p15 and p28: whether a subobject of this type is carried by a copy of its
// bytes rather than by a call.  Anything that is not of class type is; a class
// is where the member its own class has for this transfer is one that does
// nothing but copy those bytes.
bool SemaAnalyzer::carried_as_storage(TypeId type, unsigned char kind)
{
	const TypeId bare = member_copy_type(type);
	if (!types_.is_class(bare))
	{
		return true;
	}
	const SemaEntity* const carried = selected_transfer(bare, kind);
	return carried != nullptr && carried->trivial && !carried->deleted;
}

// 9p6 and 12.8p15: whether carrying a subobject of this type comes to nothing
// at all - the class holds nothing, so there are no bytes, and the member its
// own class has for this transfer does nothing, so there is no call either.
bool SemaAnalyzer::carries_nothing(TypeId type, unsigned char kind)
{
	const TypeId bare = member_copy_type(type);
	if (!types_.is_class(bare) || !types_.is_empty_class(bare))
	{
		return false;
	}
	const SemaEntity* const carried = selected_transfer(bare, kind);
	return carried != nullptr && carried->trivial;
}

// 12.8p15: the subobject of the object a move reads from is an xvalue, which is
// what makes 13.3 choose the move member of the subobject's own class where it
// has one.  A copy leaves it the lvalue naming the parameter made it.
void SemaAnalyzer::transfer_source(Value& source, unsigned char kind)
{
	if (kind != kMoveConstructorTransfer && kind != kMoveAssignmentTransfer)
	{
		return;
	}
	source.category = ValueCategory::XValue;
	if (source.node != nullptr)
	{
		source.node->fact.category = ValueCategory::XValue;
	}
}

// One `StorageTransfer` step: the bytes at `offset` of the object being written
// into take the bytes at `offset` of the object read from.
void SemaAnalyzer::write_storage_transfer(SemaEntity& parameter, DumpNode& line,
                                          unsigned long long offset,
                                          unsigned long long span, TypeId scalar)
{
	DumpNode& node = open_fact(line, "storage-transfer", FactKind::StorageTransfer);
	node.fact.type = scalar;
	node.fact.value = offset;
	node.fact.elements = span;
	Value into = this_value(node);
	DumpNode& held = model_.wrap_node(*into.node, std::string());
	set_fact(held, FactKind::Unary, types_.target(into.type),
	         ValueCategory::LValue);
	held.fact.op = OP_STAR;
	parameter_value(parameter, node);
}

// 12.8p28: one subobject of an assignment the standard defines, which is 5.17's
// assignment of the corresponding subobject of the source to it.  A subobject of
// class type reaches the assignment operator its own class has, which is the
// same call 13.3.1.2 makes for an assignment the program wrote.
void SemaAnalyzer::write_transfer_assignment(SemaEntity& subobject,
                                             const Value& source,
                                             DumpNode& line,
                                             const Context& inner,
                                             Placement where)
{
	DumpNode& statement =
		open_fact(line, "expression-statement", FactKind::ExpressionStatement);
	DumpNode& step = model_.open_node(statement, std::string());
	Value target;
	if (where == Placement::Base)
	{
		Value self = this_value(step);
		DumpNode& held = model_.wrap_node(*self.node, std::string());
		set_fact(held, FactKind::Unary, types_.target(self.type),
		         ValueCategory::LValue);
		held.fact.op = OP_STAR;
		self.type = self.spelled = types_.target(self.type);
		self.category = ValueCategory::LValue;
		self.node = &held;
		target = base_value(self, subobject, false);
	}
	else
	{
		DumpNode& access = model_.open_node(step, std::string());
		target = member_value(subobject, implied_object(subobject, access),
		                      subobject.name, access);
	}
	step.children.push_back(source.node);
	std::vector<Value> operands;
	operands.push_back(target);
	operands.push_back(source);
	Value chosen;
	if (types_.is_class(member_copy_type(target.type)))
	{
		if (!operator_expression(OP_ASS, inner, step, operands, true, chosen))
		{
			throw std::runtime_error(
				"a subobject of the class type " +
				types_.description(member_copy_type(target.type)) +
				" has no assignment operator 12.8p28 can call on it");
		}
		return;
	}
	// 5.17p1: the value the source subobject holds is written into the one this
	// object holds, and the result is that object.
	step.text = spell("assignment-expression", ValueCategory::LValue,
	                  target.type, std::string());
	set_fact(step, FactKind::Assignment, target.type, ValueCategory::LValue);
	step.fact.op = OP_ASS;
}

// 8.5.1p2: what the constructor an aggregate class was given does - each of its
// non-static data members initialized with the parameter of the same name.  The
// parameters were declared in that same order, so the two walks step together
// and neither looks the other up.
void SemaAnalyzer::write_member_parameters(const Pending& pending,
                                           DumpNode& line)
{
	Scope& members = *pending.members;
	std::vector<SemaEntity*> parameters;
	for (std::size_t index = 0; index < pending.scope->declarations.size();
	     ++index)
	{
		SemaEntity& declared = *pending.scope->declarations[index];
		if (declared.kind == SemaKind::Parameter)
		{
			parameters.push_back(&declared);
		}
	}
	std::size_t at = 0;
	for (std::size_t index = 0; index < members.declarations.size(); ++index)
	{
		SemaEntity& member = *members.declarations[index];
		if (!declares_subobject(member, members) || at >= parameters.size())
		{
			continue;
		}
		DumpNode& node = open_fact(line, "member-initialization " + member.name +
		                           " " + types_.description(member.type),
		                           FactKind::MemberInitialization);
		node.fact.entity = &member;
		node.fact.type = member.type;
		node.fact.spelled = member.type;
		implied_object(member, node);
		parameter_value(*parameters[at], node);
		++at;
	}
}

// 12.4p8: after a destructor's body has run, the destructors of the class's
// members run, in the reverse of the order the members were constructed in.
void SemaAnalyzer::write_member_destructions(Scope& members, DumpNode& line)
{
	for (std::size_t index = members.declarations.size(); index-- > 0;)
	{
		SemaEntity& member = *members.declarations[index];
		if (!declares_subobject(member, members))
		{
			continue;
		}
		// 12.4p5 and 12.4p11: the destructor of the class is what names the
		// destructor of each of its members, so that is where the access 11 gave
		// the member's own is asked for.
		require_destruction_access(member, &members);
		destructor_action(member, line, Placement::Member);
	}
	if (members.owner != nullptr && members.owner->base != nullptr)
	{
		// 12.4p8: the base class subobject is destroyed after every member, in
		// the reverse of the order 12.6.2p10 constructed them in.
		require_destruction_access(*members.owner->base, &members);
		destructor_action(*members.owner->base, line, Placement::Base);
	}
}

// 3.7.1: which region ends the lifetime of the object a definition declares,
// which is what its storage duration says.  An object that is not local has
// static storage duration however it was declared - at namespace scope, or as
// the static data member 9.4.2p2 defines outside its class - and 3.6.3p1 ends
// it when the program does; a local one ends with the block that declared it.
void SemaAnalyzer::record_lifetime(SemaEntity& entity, const Context& target,
                                   DumpNode& line)
{
	// 12.4p11: whichever region ends it, the lifetime ends in a call of the
	// destructor of the object's class, and the region that declares the object
	// is where that call is named.
	require_destruction_access(entity, target.scope);
	if (target.scope->kind == ScopeKind::Namespace ||
	    target.scope->kind == ScopeKind::Class)
	{
		// 3.7.2p2 and 3.6.3p1: there is one object per thread, and its lifetime
		// ends when its own thread does - which is neither 3.6.3p1's shutdown
		// nor any point this program writes.  So the end of it is an action of
		// the declaration rather than of the program, handed to the runtime
		// where the object is initialized; the action is written under the
		// declaration once the initialization it follows has been.
		if (entity.thread_storage)
		{
			ThreadLifetime held;
			held.entity = &entity;
			held.line = &line;
			thread_lifetimes_.push_back(held);
			return;
		}
		static_lifetimes_.push_back(&entity);
		return;
	}
	// 3.7.1p3: a block-scope object declared `static` or `thread_local` has
	// reached the refusal the declaration itself writes, so what is left here
	// is an object of the block, whatever its type.
	if (!lifetimes_.empty())
	{
		lifetimes_.back().push_back(&entity);
		if (ends_in_call(entity))
		{
			++live_destructions_;
		}
	}
}

void SemaAnalyzer::open_lifetimes()
{
	lifetimes_.push_back(std::vector<SemaEntity*>());
}

void SemaAnalyzer::close_lifetimes(DumpNode& line)
{
	// 3.8p1 and 6.7p2: the objects a block declared are destroyed where control
	// leaves it, in reverse order of construction.
	std::vector<SemaEntity*>& frame = lifetimes_.back();
	for (std::size_t index = frame.size(); index-- > 0;)
	{
		if (ends_in_call(*frame[index]))
		{
			--live_destructions_;
		}
		destructor_action(*frame[index], line, Placement::Named);
	}
	lifetimes_.pop_back();
}

void SemaAnalyzer::leave_lifetimes(std::size_t depth, DumpNode& line)
{
	// 6.6p2 and 3.8p1: the blocks a jump leaves are the ones opened since the
	// statement it jumps out of, and the objects of each are destroyed in the
	// reverse of the order they were constructed in, innermost block first.
	for (std::size_t at = lifetimes_.size(); at-- > depth;)
	{
		const std::vector<SemaEntity*>& frame = lifetimes_[at];
		for (std::size_t index = frame.size(); index-- > 0;)
		{
			destructor_action(*frame[index], line, Placement::Named);
		}
	}
}

void SemaAnalyzer::unwind_lifetimes(DumpNode& line)
{
	leave_lifetimes(0, line);
}

// 12.1p5: whether default-initializing an object of `type` does nothing at all,
// which is the one question that says a subobject needs no action written for
// it.  12.6.2p8 leaves such a member with no initialization to describe.
bool SemaAnalyzer::trivially_constructed(TypeId type)
{
	for (const SemaEntity* at = class_constructors(element_of(type));
	     at != nullptr; at = at->next)
	{
		if (types_.parameters(at->type).size() == 1)
		{
			return at->trivial;
		}
	}
	return false;
}

// 12.4p3: whether the end of this object's lifetime is a call rather than
// nothing at all, which is the one question every count of live objects asks.
bool SemaAnalyzer::ends_in_call(const SemaEntity& entity)
{
	const SemaEntity* const destructor =
		class_destructor(element_of(entity.type));
	return destructor != nullptr && !destructor->trivial;
}

bool SemaAnalyzer::lifetimes_pending() const
{
	// The count is carried rather than recomputed: a jump asks this question
	// once per jump, and walking every open block for it would cost the depth
	// of the blocks around each one.
	return live_destructions_ != 0;
}
