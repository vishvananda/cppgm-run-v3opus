#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_access.h"
#include "sema_constexpr.h"
#include "sema_operator.h"
#include "sema_string_init.h"

namespace
{

// 8.5.3p5: how many conversions may stand between the line a reference
// initializer left and the prvalue whose object 12.2p5 extends.  A binding
// writes a fixed number of them - a base class subobject of the temporary, a
// member of it, the cast a conversion made - so the walk down to the object is
// bounded rather than being a search of the initializer.
const unsigned kBoundTemporaryDepth = 8;

// 13.3.1.4p1: holds the class an object is being direct-initialized of while
// 13.3 measures its candidates, and puts back what stood before however the
// resolution leaves - a refusal is one of the probes above catches and goes on
// asking questions past.
struct DirectInitialization
{
	DirectInitialization(TypeId& held, TypeId type)
		: held_(held)
		, outer_(held)
	{
		held_ = type;
	}

	~DirectInitialization() { held_ = outer_; }

private:
	TypeId& held_;
	const TypeId outer_;
};

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
	if (where == Placement::Delegate)
	{
		// 12.6.2p6: the object this constructor is already running on, whole.
		// It is no subobject of anything, so `this` is the address the target
		// constructor is passed exactly as it stands - neither an address taken
		// around a name nor a conversion to a base.
		object = this_value(call);
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
	    !types_.is_class(types_.element_of(type)))
	{
		return false;
	}
	return written == nullptr ||
		(is_initializer_list(written->kind) && written->children.empty());
}

bool creates_its_object(const DumpNode& node, TypeTable& types)
{
	switch (node.fact.kind)
	{
	case FactKind::TemporaryObject:
		// 12.2p1: the temporary is the object the program wrote, and what
		// creates it is the constructor standing under it.
		return node.fact.entity != nullptr && !node.children.empty() &&
			node.children[0]->fact.kind == FactKind::ConstructorAction;

	case FactKind::Call:
		// 6.6.3p2: the returned object is created where the call names storage
		// for it, so a call of a function returning a class creates one.
		return node.fact.category == ValueCategory::PRValue;

	case FactKind::Cast:
		// 5.2.9p4: a cast to a class type *is* the direct-initialization of a
		// temporary of it, so the object the cast is worth is the one standing
		// under it and the cast creates it wherever that one is created.
		// 3.10p9 leaves the cv-qualification of a class prvalue on the node the
		// cast wrote and off the object under it, and one object is what they
		// both name - which is the same reading the lowering's own cast makes.
		// Which node stands under the cast is no part of the question: a call
		// of a function returning this class hands back an object it creates
		// exactly as a `temporary-object` written here does, and 5.2.9p4's cast
		// over either of them is one more spelling of the same object.
		return node.fact.category == ValueCategory::PRValue &&
			!node.children.empty() &&
			types.strip_cv(node.children[0]->fact.type) ==
				types.strip_cv(node.fact.type) &&
			creates_its_object(*node.children[0], types);

	default:
		break;
	}
	return false;
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
	if (ctx.scope != nullptr && !Access(*this).accessible(*chosen, ctx.scope))
	{
		throw std::runtime_error(
			"an object of " + types_.description(types_.strip_cv(type)) +
			" is initialized from a value of its own class, whose copy 12.8p32 "
			"asks for a constructor the access its class gave does not reach");
	}
}

// 12.8p31: a temporary of the object's own class that has not been bound to a
// reference, and that would be copied or moved into this object, is created in
// this object's storage instead - so the transfer 13.3 chose does not run and
// the line the initialization holds is the one that creates the temporary.
//
// The question is asked once, after 13.3 has chosen, because which constructor
// the initializer reaches is what says there is a copy here at all: an argument
// that reaches the parameter through a conversion function of its own class is
// a prvalue of this class only after that conversion has been applied, and a
// class that declares a constructor taking the argument's own type reaches no
// copy for the elision to remove.  What the elision removes is the call; 12.8p32
// already asked for access to the constructor above, and `select_overload` and
// `require_access` are what asked.
// 12.8p31 and 12.2p3: the object an elided prvalue creates *is* the object
// being initialized, so the full-expression that was holding the end of its
// lifetime holds it no longer - the one end it has is the destination's own,
// written where the destination's declaration or its boundary says.  The
// object stands under whatever 5.2.9p4's cast wrote over the prvalue, which is
// where `creates_its_object` reads it from, so this walks to the same node
// rather than to the one the elision happened to be handed: a cast left in the
// frame is an end written on the storage the destination stands in, which
// destroys the object the initialization has just built.  15.2p2 reads the
// same fact, so the node keeps neither it nor the object.
void SemaAnalyzer::elide_created_object(DumpNode& node)
{
	DumpNode& at = created_object_node(node);
	Value created;
	created.node = &at;
	release_temporary(created);
	at.fact.destruction = nullptr;
	at.fact.object = nullptr;
}

// 5.2.9p4 and 12.2p1: the node the object a prvalue creates stands on.  A cast
// to a class type *is* the direct-initialization of the object under it, so
// the two name one object and the fact of it is written on the one below - and
// every reader that has to reach the object rather than the value walks there,
// because a question asked of the cast alone is answered about a node that
// holds no object at all.
DumpNode& SemaAnalyzer::created_object_node(DumpNode& node)
{
	DumpNode* at = &node;
	while (at->fact.object == nullptr && at->fact.kind == FactKind::Cast &&
	       at->fact.category == ValueCategory::PRValue && !at->children.empty() &&
	       types_.strip_cv(at->children[0]->fact.type) ==
	           types_.strip_cv(at->fact.type))
	{
		at = at->children[0];
	}
	return *at;
}

bool SemaAnalyzer::elide_transfer(const SemaEntity& constructor,
                                  std::vector<Value>& arguments,
                                  TypeId object_type, DumpNode& line,
                                  DumpNode& action, bool into_temporary)
{
	if (arguments.size() != 1 ||
	    (constructor.transfer != kCopyConstructorTransfer &&
	     constructor.transfer != kMoveConstructorTransfer))
	{
		return false;
	}
	if (into_temporary)
	{
		// 12.8p31: the destination is itself a temporary this expression made
		// for a prvalue, so what the elision would do is take the source's
		// object out of the frame that holds it and put it in storage the
		// enclosing initialization has not settled - the destination's own
		// place is still being decided, and 8.5.3p5 has already bound the
		// source to the reference this constructor's parameter is.  What the
		// elision reaches is a destination that stands of its own: a
		// declaration's storage, 5.3.4p12's allocated storage, a subobject of
		// one of those.  So the source is materialized where the argument
		// stands and the transfer 13.3 chose is a call like any other.
		return false;
	}
	Value& source = arguments[0];
	if (source.node == nullptr || source.category != ValueCategory::PRValue ||
	    types_.strip_cv(source.type) != types_.strip_cv(object_type) ||
	    !creates_its_object(*source.node, types_))
	{
		return false;
	}
	if (line.children.empty() || line.children.back() != &action)
	{
		return false;
	}
	// 12.2p3: the object this line creates is the one being initialized, so
	// its lifetime is the one the declaration holds and no full-expression ends
	// it - and 15.2p2 reads the same one end, so the node keeps neither.
	elide_created_object(*source.node);
	line.children.pop_back();
	line.children.push_back(source.node);
	return true;
}

WrittenInitializer SemaAnalyzer::read_initializer(
	const AstNode* written, TypeId object_type, const Context& ctx,
	bool value_init)
{
	// 8.5p15 and 8.5p16: which of the arguments the program wrote reach the
	// constructor, and whether 13.3.1.4 leaves out the ones declared `explicit`.
	WrittenInitializer form;
	form.value_init = value_init;
	if (written == nullptr)
	{
		return form;
	}
	if (is_initializer_list(written->kind))
	{
		form.list = written;
		// 8.5p7: `()` and `{}` value-initialize the object rather than naming
		// an argument for a constructor.
		form.value_init = written->children.empty();
		return form;
	}
	// 8.5p14: copy-initialization from one expression, which only a converting
	// constructor may answer.
	form.converting = true;
	if (written->kind != AstKind::CallExpression || written->children.empty() ||
	    written->children[0]->kind != AstKind::IdExpression)
	{
		return form;
	}
	// 12.8p31 and 5.2.3p1: a class object copy-initialized from a prvalue of
	// its own type is initialized by whatever makes that prvalue, so the
	// arguments of `T(...)` are the constructor's and no object of the type
	// stands between them.
	SemaEntity* const named =
		resolve(written->children[0]->text, ctx, LookupKind::Type);
	if (named == nullptr || !names_a_type(*named) ||
	    types_.strip_cv(named->type) != types_.strip_cv(object_type))
	{
		return form;
	}
	form.list = call_arguments(*written);
	if (form.list != nullptr && form.list->braced)
	{
		// 5.2.3p3: the prvalue was written `T{...}`, whose one braced list
		// stands where the arguments of `T(...)` do.  What initializes the
		// object is that list, so 8.5.4 reads it and 8.5.1 gives its clauses to
		// the members of an aggregate.  `T({...})` writes the same one node
		// under the same list and means the other thing - one argument, which
		// 13.3.3.1.5 gives to a constructor - so the braces the parse saw are
		// what says which was written.
		form.list = form.list->children[0];
	}
	// 5.2.3p2 leaves `T()` the value-initialization 8.5p7 writes where it
	// stands rather than an object something was built into, so it is the one
	// spelling of the prvalue this is not: `T(a, b)` names arguments and
	// `T{...}` names braces, and either of them is what the object was created
	// by.
	form.elided_prvalue =
		form.list != nullptr && (form.list->kind == AstKind::BracedInitList ||
		                         !form.list->children.empty());
	form.converting = false;
	// 5.2.3p2: `T()` value-initializes what it makes, and the grammar writes no
	// argument-list node for one that has no arguments.
	form.value_init = form.list == nullptr || form.list->children.empty();
	return form;
}

// 8.5, 12.1 and 13.3.1.3: an object of class type is initialized by one of the
// constructors of its class, chosen from the arguments its initializer wrote.
// The action is one call like any other, written under the declaration of the
// object, and the definition of the constructor it names is asked for here.
//
// `written` is the initializer the program wrote, or null for an object with
// none; `member` says the object is a non-static data member of the one the
// constructor being written is initializing, so that the action names it
// through `this` rather than by a name of its own.
// `copied` says the initializer was written with `=`, which 8.5.4p3 makes an
// initialization no `explicit` constructor may answer.
// `given` is an initializer already analysed where it was written, which
// 13.3.3.1.2's conversion has and no source form does: the value is taken as it
// stands and its line moves into the place the call gives it.
// `value_init` says the initializer was an empty list the grammar wrote no node
// for, which is what 5.2.3p2's `T()` is.
// `forwarded`, where given, are the parameters whose values 12.9p8 passes to
// the constructor of the base subobject an inheriting constructor initializes,
// in place of an initializer the program wrote.
// `direct` says the place that asked for the initialization wrote it as a
// direct-initialization, which 13.3.1.4 leaves the class's `explicit`
// constructors in: 5.2.9p4's cast is one and 13.3.3.1.2's conversion is not,
// and the two reach here the same way - with the one operand already read.
// `into_temporary` says the object being initialized is one the analysis made
// to hold a prvalue rather than one that stands of its own, which is what
// 12.8p31's elision asks about the destination.
// `boundary_object` says which kind of temporary that is: 5.2.2p4's parameter
// and 6.6.3p2's returned object are made by a boundary to carry a value across
// it, and the rest are objects an expression the program wrote asked for.
// `chosen`, where given, is left holding the constructor 13.3 picked, which
// 12.6.2p6's chain of delegations is walked over.
void SemaAnalyzer::construct_object(SemaEntity& variable, DumpNode& line,
                                    const AstNode* written, const Context& ctx,
                                    Placement where, bool copied,
                                    const Value* given, bool value_init,
                                    const std::vector<SemaEntity*>* forwarded,
                                    bool direct, bool into_temporary,
                                    bool boundary_object, SemaEntity** chosen)
{
	const bool member = where == Placement::Member || where == Placement::Base;
	// 12.6p1: an array of class type is initialized element by element, and
	// each element is one object of the element's class.  The one constructor
	// every element is given is chosen once, here, from the type of an element;
	// the action names the array, so how many objects it creates is what the
	// declared type says rather than a count written beside it.
	const TypeId object_type = types_.element_of(variable.type);

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
	// 8.5p14 and 8.5p16: only `= { ... }` is copy-list-initialization.  `= e`
	// is copy-initialization, which 13.3.1.4 answers by leaving the `explicit`
	// constructors out of the candidates rather than by refusing one, and
	// `= T(...)` is the direct-initialization 12.8p31 elides into.
	const bool copy_list =
		copied && written != nullptr && written->kind == AstKind::BracedInitList;
	const WrittenInitializer form =
		read_initializer(written, object_type, ctx, value_init);
	const AstNode* const list = form.list;
	const bool elided_prvalue = form.elided_prvalue;
	bool converting = form.converting;
	value_init = form.value_init;

	if (elided_prvalue)
	{
		// 12.8p31 and 12.8p32: the initializer is a prvalue of the object's own
		// class written where the object stands, so the object it creates and
		// this one are one and no copy runs - and p32 still asks for the
		// constructor that copy would have called, because the elision says the
		// call does not run and not that the program did not have to be allowed
		// to write it.
		require_elided_transfer(object_type, ctx);
	}
	Value source;
	if (given != nullptr)
	{
		// 13.3.3.1.2p1: the one argument was analysed where the program wrote
		// it, so it is taken as it stands.  Its line is not held by this one
		// yet, so nothing is taken back out of what this line already holds.
		source = *given;
		// 13.3.1.3 and 13.3.1.4: the operand is one, and which of the class's
		// constructors are candidates for it is what the place that asked
		// wrote - 5.2.9p4's cast is a direct-initialization and leaves the
		// `explicit` ones in, and 13.3.3.1.2's conversion is not and does not.
		converting = !direct;
	}
	else if (converting)
	{
		// 8.5p14: the initializer is read before anything is written for the
		// initialization, because 12.8p31 lets a value of the object's own type
		// be what initializes it, with no constructor standing between them.
		source = expression(*written, ctx, line);
		if (types_.strip_cv(source.type) == types_.strip_cv(object_type) &&
		    !member && source.category == ValueCategory::PRValue &&
		    source.node != nullptr &&
		    (creates_its_object(*source.node, types_) ||
		     types_.bytes_stand_for_object(types_.strip_cv(object_type))))
		{
			// 12.8p31: the initializer is a prvalue of the object's own class
			// that creates the object it is worth, so the object it creates and
			// the one being initialized may be one and nothing stands between
			// them.  A glvalue names an object that goes on existing, so 8.5p14
			// leaves it the call of the copy or move constructor 13.3 chooses.
			//
			// A prvalue that only selects among objects - a conditional, a comma
			// - creates nothing: the object it is worth was created where its
			// operand stands, and 12.8p15's copy of it into this one is a call
			// the program wrote and can watch run.  12.8p12's is not: where the
			// class carries an object by its bytes there is no call to leave
			// out, and the two objects are one.
			if (creates_its_object(*source.node, types_))
			{
				// 12.2p3: the object the prvalue creates is this one, so the
				// full-expression that was holding the end of its lifetime is
				// not what ends it - the destination's own end is.
				elide_created_object(*source.node);
			}
			require_elided_transfer(object_type, ctx);
			return;
		}
		line.children.pop_back();
	}
	DumpNode& action = model_.open_node(line, std::string());
	action.fact.kind = FactKind::ConstructorAction;
	action.fact.type = variable.type;
	action.fact.elided_prvalue = elided_prvalue;
	action.fact.boundary_object = boundary_object;
	action.fact.base_subobject = where == Placement::Base;
	action.fact.subobject_step = member;
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
		// 13.3.3.1p4: this is 13.3.1.7's second phase where the initializer is
		// a braced-init-list, and where that list holds exactly one element
		// that is itself a list, a parameter of the class being initialized
		// reaches it through no user-defined conversion.
		const bool sole_list = list->kind == AstKind::BracedInitList &&
			list->children.size() == 1 &&
			list->children[0]->kind == AstKind::BracedInitList;
		// 14.5.3p4: an argument written `pattern...` is one argument per
		// element of the run its packs are bound to.
		Clauses written(list, *this, ctx);
		for (; !written.spent(); ++written.at)
		{
			Value one = argument_expression(written.next(), written.in(ctx),
			                                call);
			if (sole_list && one.braced != nullptr)
			{
				one.listed_class = object_type;
			}
			arguments.push_back(one);
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
	// 13.3.1.4p1: this is a direct-initialization of an object of the class
	// written with one argument, so the temporary bound to the first parameter
	// of a constructor of that class is initialized in the context of this
	// initialization - which is what lets 12.3.2p2's `explicit` conversion
	// functions of the argument's own class reach it.  The class is what the
	// question is asked about, so it is what is carried; a copy-initialization
	// and a call with any other number of arguments carry nothing.
	// The context stands over the conversions below as well as over the choice,
	// because 13.3 measures each argument's sequence once to choose and once to
	// apply, and the two have to be the one answer.
	const DirectInitialization direct_context(
		direct_initialized_,
		!converting && arguments.size() == 1 ? types_.strip_cv(object_type)
		                                     : kNoType);
	SemaEntity& constructor = *select_overload(candidates, arguments,
	                                           head->name, &object, converting);
	Access(*this).require_access(constructor, ctx.scope);
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
	note_construction_entry(constructor, where == Placement::Base);
	if (member && !constructor.trivial)
	{
		// 15.2p2: this step builds a subobject an exception out of a later one
		// leaves standing, which is what odr-uses its destructor.  12.6.2p6's
		// delegation builds no subobject: it builds the whole object, and after
		// it there is no later step for an exception to leave it standing at.
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
	if (!member && where != Placement::Base &&
	    elide_transfer(constructor, arguments, object_type, line, action,
	                   into_temporary))
	{
		// 12.8p31: the constructor 13.3 chose carries an object into this one,
		// and what it was given is a prvalue that creates its own object - so
		// the two objects are one and the copy between them is not written.
		return;
	}
	for (std::size_t index = arguments.size() + 1; index < parameters.size();
	     ++index)
	{
		// 8.3.6p1: the constructor is called as if the default-argument had
		// been written where the argument is missing.
		write_default_argument(constructor, index, call);
	}
	write_constructor_action(action, call, callee, constructor, *head,
	                         value_init);
	if (chosen != nullptr)
	{
		*chosen = &constructor;
	}
}

// The three lines an initialization leaves once 13.3 has chosen: the action
// that says an object's lifetime begins here, the call that runs the
// constructor and the callee that names it.  Every reader of the initialization
// - the lowering, 15.2p2's handler, 12.8p31's elision - reads those three, so
// they are written in one place however the initialization reached its choice.
void SemaAnalyzer::write_constructor_action(DumpNode& action, DumpNode& call,
                                            DumpNode& callee,
                                            SemaEntity& constructor,
                                            const SemaEntity& head,
                                            bool value_init)
{
	action.text = "constructor-action " + constructor.dump_name;
	action.fact.entity = &constructor;
	// 8.5p7: a non-union class with no user-provided constructor is
	// zero-initialized before its default constructor - which is the one this
	// initialization chose - runs on it.  The zero is what the object holds
	// wherever that constructor leaves a member alone, so it is written even
	// where the constructor itself does nothing at all.
	action.fact.zero_initialized = value_init && !user_provided_constructor(head);
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
	if (constructor.primary != nullptr &&
	    constructor.primary->template_parameters != nullptr)
	{
		// 14.5.2p1 with 14.7.1p1: what 13.3.1.3 chose may be a specialization of
		// a *constructor template*, which is a declaration the deduction made and
		// no definition - so building the object is what asks the template for
		// one, exactly as naming any other specialization does.  A constructor is
		// reached by no name, so this is where that ask stands.
		instantiate(constructor);
	}
	// 14.7.1p1: building an object is a use of the constructor 13.3.1.3 chose,
	// and a specialization's own is a body the instantiation put aside.
	require_definition(constructor);
	note_instantiated_transfer(constructor);
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
                                                  bool value_init, bool owned,
                                                  bool direct, bool copy_list,
                                                  bool boundary)
{
	const TypeId object_type = types_.strip_cv(type);
	// 10.4p2: a temporary is an object, which a class with a pure final
	// overrider has none of - so 5.2.3's explicit type conversion and every
	// other place that builds one is refused here rather than at each of them.
	// 3.9p5 and 5.2.3p2: the object this creates is one of `type`, so the type
	// has to be complete here - which for a class template specialization is
	// 14.7.1p1's implicit instantiation and not merely the name being written.
	require_complete_type(object_type);
	require_creatable_object(object_type, "a temporary");
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
	const bool from_clauses =
		given == nullptr && written != nullptr &&
		written->kind == AstKind::BracedInitList &&
		!written->children.empty() && aggregate_type(object_type);
	SemaEntity* const from_members =
		from_clauses ? member_constructor(object_type) : nullptr;
	if (from_members != nullptr)
	{
		construct_from_members(*from_members, *written, ctx, line);
	}
	else if (from_clauses)
	{
		// 8.5.1p2: the class is an aggregate, so the clauses initialize its
		// members and no constructor it declared is what takes them - but the
		// object is one of its own here, and 8.3.5p5 leaves the class with no
		// by-value parameter list that carries an array member.  This is the
		// one shape of 8.5.4 the milestone still leaves, and saying so is not
		// the same as saying no constructor accepted the arguments.
		throw std::runtime_error("a braced-init-list builds an object of the "
		                         "aggregate " +
		                         types_.description(object_type) +
		                         ", whose members no by-value parameter list "
		                         "describes");
	}
	else
	{
		// 12.8p31: what this builds is an object the analysis made to hold a
		// prvalue, which is no destination the elision reaches - the place that
		// asked for the prvalue is what settles where it stands.
		construct_object(object, line, written, ctx, Placement::Named, copy_list,
		                 given, value_init, nullptr, direct, true, boundary);
	}
	if (owned)
	{
		// 12.2p3: the temporary is destroyed at the end of the full-expression
		// it was created in, so the open full-expression is what holds it.
		// 13.3.3.1.5p5's temporary - the one a braced-init-list was written
		// into - demands no definition of the destructor of its class, so what
		// its end comes to is 12.4p8's reading of that destructor's body.
		register_temporary(line, ctx.scope, false,
		                   written == nullptr ||
		                       written->kind != AstKind::BracedInitList);
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
//
// 6.6.3p2's returned object and 5.16p3's result object are temporaries the
// program wrote no `T(...)` for, and each takes its name the same way: what
// asked for the object is what its storage is named after, wherever the object
// came from.
void SemaAnalyzer::name_argument_temporary(const Value& value,
                                           const char* prefix,
                                           const Context& ctx, bool owned)
{
	if (value.node == nullptr)
	{
		return;
	}
	const bool prvalue_object =
		(value.node->fact.kind == FactKind::Call ||
		 value.node->fact.kind == FactKind::Conditional) &&
		value.category == ValueCategory::PRValue &&
		types_.is_class(types_.strip_cv(value.type));
	if (value.node->fact.kind == FactKind::TemporaryObject || prvalue_object)
	{
		value.node->fact.spelling = prefix;
	}
	if (!owned)
	{
		release_temporary(value);
		return;
	}
	// 8.5.3p5 and 12.2p3: the reference binds a temporary, so the object that
	// temporary is has a lifetime, and the full-expression this argument stands
	// in is what ends it.
	register_temporary(*value.node, ctx.scope);
}

// 12.2p3: what the end of the full-expression a temporary was created in comes
// to.  The object is one no declaration named, so what the destruction names is
// the node that produced it - the same object every other reader of that
// prvalue reaches, and the one piece of storage the lowering gave it.
void SemaAnalyzer::temporary_destruction(SemaEntity& object, DumpNode& parent,
                                         bool full_expression)
{
	SemaEntity* const destructor = class_destructor(types_.element_of(object.type));
	if (destructor == nullptr || !declared_destruction(object.type))
	{
		return;
	}
	note_destruction_entry(*destructor, false);
	DumpNode& action = model_.open_node(
		parent, "destructor-action " + destructor->dump_name);
	action.fact.kind = FactKind::DestructorAction;
	action.fact.full_expression_end = full_expression;
	action.fact.entity = destructor;
	action.fact.type = object.type;
	DumpNode& named = model_.open_node(
		action, spell("temporary-object", ValueCategory::PRValue, object.type,
		              std::string()));
	set_fact(named, FactKind::TemporaryObject, object.type,
	         ValueCategory::PRValue);
	named.fact.entity = &object;
	named.fact.object = &object;
}

// 12.2p1: the object a prvalue of class type standing in storage of its own is.
// A `temporary-object` is one the analysis already declared; a call and a
// conditional hand back a prvalue no object was declared for, and the first
// place that needs an object rather than a value is where one is made.
SemaEntity* SemaAnalyzer::prvalue_object(DumpNode& node)
{
	if (node.fact.object != nullptr)
	{
		return node.fact.object;
	}
	if (node.fact.kind == FactKind::TemporaryObject)
	{
		node.fact.object = node.fact.entity;
		return node.fact.object;
	}
	const bool own_storage =
		(node.fact.kind == FactKind::Call ||
		 node.fact.kind == FactKind::Conditional) &&
		node.fact.category == ValueCategory::PRValue &&
		types_.is_class(types_.strip_cv(node.fact.type));
	if (!own_storage)
	{
		return nullptr;
	}
	SemaEntity& object = model_.create(SemaKind::Variable, std::string(),
	                                   types_.strip_cv(node.fact.type));
	object.object_member = false;
	node.fact.object = &object;
	return &object;
}

SemaEntity* SemaAnalyzer::register_temporary(DumpNode& node, const Scope* from,
                                             bool extended, bool demanded)
{
	const bool known = node.fact.object != nullptr;
	SemaEntity* const object = prvalue_object(node);
	if (object == nullptr || (known && !extended))
	{
		// The prvalue was already given an object, so the lifetime it has is
		// already held by whichever region was asked for it first.
		return object;
	}
	if (known)
	{
		// 12.2p5: the full-expression that created this temporary was holding
		// the end of its lifetime, and the reference it has just been bound to
		// holds it now - so it moves out of the one and into the other rather
		// than being ended twice.
		Value written;
		written.node = &node;
		release_temporary(written);
		if (!lifetimes_.empty())
		{
			lifetimes_.back().push_back(object);
			++live_destructions_;
		}
		return object;
	}
	if (demanded ? !declared_destruction(object->type)
	             : vacuous_destruction(object->type))
	{
		// 12.4p3: the program declared no destructor anywhere below this
		// object's class, so its end names nothing at all - no region has to
		// hold it to write one, and nothing says it began.  13.3.3.1.5p5's
		// temporary is the one that asks 12.4p8 instead: it demands no
		// definition of its own, so what its end comes to is what running the
		// destructor comes to and not whether one was declared.
		node.fact.object = nullptr;
		return nullptr;
	}
	// 15.2p2: an exception thrown while this object stands ends its lifetime
	// wherever it goes on to, so the call is written on the place the lifetime
	// began as well as on the place it ends.
	node.fact.destruction = class_destructor(types_.element_of(object->type));
	if (node.fact.destruction != nullptr)
	{
		// 12.4p6 and 3.2p4: that call is an odr-use of the destructor whether
		// or not any statement writes a second one - an argument object the
		// callee ends, and one 12.8p31 elides into a destination, are both ends
		// 15.2p2 alone reaches - so this is where the unit says which entry the
		// object file owes and asks for the definition the standard gives.
		note_destruction_entry(*node.fact.destruction, false);
	}
	// 12.4p11 and 12.2p3: the lifetime ends in a call of the destructor of the
	// object's class, wherever the region that holds it writes that call.  A
	// place that reaches this with no region of its own to ask from is one the
	// declaration the prvalue came from already asked for.
	if (from != nullptr)
	{
		Access(*this).require_destruction_access(*object, from);
	}
	if (extended)
	{
		// 12.2p5: the reference the temporary was bound to is what its lifetime
		// now follows, so the block that declared the reference ends it.
		lifetimes_.back().push_back(object);
		++live_destructions_;
		return object;
	}
	if (temporaries_.empty())
	{
		// 12.2p3's end of the lifetime is the end of the full-expression, and
		// this prvalue stands where this milestone marks none.
		throw std::runtime_error(
			"a temporary of the class type " + types_.description(object->type) +
			" is created, whose destructor 12.2p3 runs at a point this "
			"milestone does not mark");
	}
	temporaries_.back().push_back(object);
	return object;
}

void SemaAnalyzer::release_temporary(const Value& value)
{
	if (temporaries_.empty())
	{
		return;
	}
	release_temporary(value, temporaries_.back());
}

void SemaAnalyzer::release_temporary(const Value& value,
                                     std::vector<SemaEntity*>& frame)
{
	// 5.2.9p4: the object the prvalue is worth stands under whatever cast was
	// written over it, so what is taken out of the frame is that object and
	// not the node the reader happened to be holding.
	const SemaEntity* const object =
		value.node == nullptr ? nullptr
		                      : created_object_node(*value.node).fact.object;
	if (object == nullptr)
	{
		return;
	}
	for (std::size_t index = frame.size(); index-- > 0;)
	{
		if (frame[index] == object)
		{
			frame.erase(frame.begin() + static_cast<std::ptrdiff_t>(index));
			return;
		}
	}
}

void SemaAnalyzer::register_discarded_object(const Value& value, DumpNode& line,
                                             const Context& ctx)
{
	DumpNode* written = value.node;
	if (written == nullptr && !line.children.empty())
	{
		written = line.children[0];
	}
	if (written == nullptr)
	{
		return;
	}
	if (written->fact.kind == FactKind::Cast &&
	    types_.is_void(types_.strip_cv(written->fact.type)) &&
	    !written->children.empty())
	{
		written = written->children[0];
	}
	if (written->fact.kind == FactKind::TemporaryObject)
	{
		// 8.5.3p5: the storage the object stands in is named after what asked
		// for it, and what asked for this one is a statement throwing its value
		// away - the same name a call handing one back is given where nothing
		// else asked.
		written->fact.spelling = "discard";
	}
	register_temporary(*written, ctx.scope);
}

void SemaAnalyzer::open_full_expression()
{
	temporaries_.push_back(std::vector<SemaEntity*>());
}

std::vector<SemaEntity*> SemaAnalyzer::take_full_expression()
{
	std::vector<SemaEntity*> frame;
	frame.swap(temporaries_.back());
	temporaries_.pop_back();
	return frame;
}

// 12.2p3: the read is a question and not one of the program's initializations,
// and the node it is written into is dropped as soon as it is answered - so
// every temporary it created is dropped with it.  A frame of its own is what
// says so: an object left standing in the enclosing full-expression is an end
// of a lifetime written on storage nothing was ever asked to name, which the
// lowering has no object to end.  It is also what lets the question be asked
// where no full-expression is open at all.
SemaAnalyzer::Value SemaAnalyzer::probe_expression(const AstNode& node,
                                                   const Context& ctx,
                                                   DumpNode& scratch)
{
	open_full_expression();
	try
	{
		const Value value = expression(node, ctx, scratch);
		take_full_expression();
		return value;
	}
	catch (...)
	{
		take_full_expression();
		throw;
	}
}

// 12.2p3: the temporaries created during a full-expression are destroyed at its
// end, in the reverse of the order they were created in.
void SemaAnalyzer::end_temporaries(const std::vector<SemaEntity*>& frame,
                                   DumpNode& line)
{
	for (std::size_t index = frame.size(); index-- > 0;)
	{
		temporary_destruction(*frame[index], line, true);
	}
}

void SemaAnalyzer::close_full_expression(DumpNode& line)
{
	const std::vector<SemaEntity*> frame = take_full_expression();
	end_temporaries(frame, line);
}

// 5.14p1 and 13.5p1: an operand this expression turned out to evaluate however
// the answer came - an overloaded operator's, which is an argument of a call -
// so what it created is the enclosing full-expression's to end after all.
void SemaAnalyzer::keep_temporaries(const std::vector<SemaEntity*>& frame)
{
	if (temporaries_.empty() || frame.empty())
	{
		return;
	}
	temporaries_.back().insert(temporaries_.back().end(), frame.begin(),
	                           frame.end());
}

// 12.2p3 and 5.16p1: what an arm created, ended under a node of the arm's own,
// which is what lets the lowering write those ends at the end of the block that
// arm is and on no other path out of the conditional.
void SemaAnalyzer::end_arm_temporaries(const std::vector<SemaEntity*>& frame,
                                       DumpNode& line, FactKind arm,
                                       const char* text)
{
	bool ends_in_something = false;
	for (std::size_t index = 0; index < frame.size(); ++index)
	{
		ends_in_something = ends_in_something ||
			(class_destructor(types_.element_of(frame[index]->type)) != nullptr &&
			 declared_destruction(frame[index]->type));
	}
	if (!ends_in_something)
	{
		// 12.4p3: nothing the arm created comes to anything at its end, so the
		// arm needs no place to write one.
		return;
	}
	end_temporaries(frame, open_fact(line, text, arm));
}

// 8.5.3p5: the temporary a reference initializer bound.  A binding to a base
// class subobject of the temporary, and one written through a conversion the
// initialization made, each stand over the prvalue that made the object - so
// what the reference extends is found under them rather than at the line the
// initializer left.
DumpNode* SemaAnalyzer::bound_temporary(DumpNode& node)
{
	DumpNode* at = &node;
	for (unsigned steps = 0; steps < kBoundTemporaryDepth; ++steps)
	{
		if (at->fact.kind == FactKind::TemporaryObject)
		{
			return at;
		}
		if ((at->fact.kind == FactKind::Call ||
		     at->fact.kind == FactKind::Conditional) &&
		    at->fact.category == ValueCategory::PRValue &&
		    types_.is_class(types_.strip_cv(at->fact.type)))
		{
			return at;
		}
		if ((at->fact.kind == FactKind::BaseConversion ||
		     at->fact.kind == FactKind::Cast ||
		     at->fact.kind == FactKind::Member) &&
		    !at->children.empty())
		{
			at = at->children[0];
			continue;
		}
		return nullptr;
	}
	return nullptr;
}

// 12.2p5: a reference bound to a temporary keeps that temporary alive for its
// own lifetime, so the block that declared the reference is what ends the
// temporary rather than the full-expression that created it.
void SemaAnalyzer::extend_bound_temporary(TypeId declared, const Context& ctx,
                                          DumpNode& line)
{
	if (!types_.is_reference(declared) || line.children.empty() ||
	    lifetimes_.empty() ||
	    !types_.is_class(types_.strip_cv(types_.target(declared))))
	{
		// 3.7.1: a reference no block declared has no block for 12.2p5 to end
		// the temporary with, so what it binds is left where it was created.
		return;
	}
	DumpNode* const written = bound_temporary(*line.children.back());
	if (written == nullptr)
	{
		return;
	}
	register_temporary(*written, ctx.scope, true);
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
	SemaEntity* const destructor = class_destructor(types_.element_of(entity.type));
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
	if (where != Placement::Parameter && !ends_in_call(entity))
	{
		// 12.4p3: nothing runs, so nothing is written - except at 5.2.2p4's
		// boundary, where the destruction is what the function owes for an
		// object it was handed rather than one it made, and 12.4p5's triviality
		// is what says whether the class has one to owe.
		return;
	}
	if (where == Placement::Named && entity.name.empty())
	{
		// 12.2p5: the object is a temporary a reference extended into this
		// block, which no declaration named - so what the destruction names is
		// the object itself and not an id-expression there is none of.  What
		// ends it is the block and not the full-expression that created it,
		// which is what 12.2p5 moved.
		temporary_destruction(entity, parent, false);
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
		(where == Placement::Member || where == Placement::Base) &&
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

// 12.6.2p2 and 12.6.2p6: whether a mem-initializer-id spells `named` - the base
// class of the constructor's own class for p2, the constructor's own class for
// p6.  A class has a name of its own and every alias of it names it too, so the
// question is asked of what the name denotes rather than of the characters it
// was written with, and both paragraphs ask it the same way.
bool SemaAnalyzer::names_the_class(const std::string& written,
                                   const SemaEntity& named, const Context& ctx)
{
	try
	{
		SemaEntity* const found = resolve(written, ctx, LookupKind::Type);
		return found != nullptr && names_a_type(*found) &&
			types_.strip_cv(found->type) == types_.strip_cv(named.type);
	}
	catch (const std::runtime_error&)
	{
		// A name that reaches no region names no class either, and the
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
// 12.6.2p10: the members are initialized in declaration order and the
// mem-initializers may be written in any, so which one names each member is
// asked once per member rather than by a scan of the list per member.  The
// index is built once per constructor definition, keyed by the unqualified
// name each mem-initializer-id wrote.
void SemaAnalyzer::read_mem_initializers(
	const Pending& pending, const Context& inner,
	std::unordered_map<std::string, MemInitializer>& named)
{
	PackReading packs(*this);
	// 12.6.2p2: which class each id names a base of, which is what tells a
	// mem-initializer of a base from one of a member of the same name.
	const SemaEntity* const owner =
		pending.members != nullptr ? pending.members->owner : nullptr;
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
		if (child_of(one, AstKind::ParameterPack) == nullptr)
		{
			hold_mem_initializer(pending, base_key(id->text, inner, owner),
			                     wrote, named);
			continue;
		}
		// 14.5.3p4: the mem-initializer is a pattern read once per element of
		// the run its packs are bound to, each reading in a region of its own -
		// so what the ctor-initializer holds is one entry per element, keyed by
		// the base class that element's reading of the id names.
		const PackReading::Run run = packs.run_of_node(one, inner);
		if (!run.found)
		{
			// 14.5.3p5: the pattern of a pack expansion shall name at least one
			// parameter pack.
			throw std::runtime_error("a mem-initializer of " +
			                         types_.description(pending.function->type) +
			                         " is expanded and names no parameter pack");
		}
		for (std::size_t element = 0; run.settled && element < run.length;
		     ++element)
		{
			Context where = inner;
			where.scope = &packs.element_region(run, element, inner);
			wrote.region = where.scope;
			hold_mem_initializer(pending, base_key(id->text, where, owner),
			                     wrote, named);
		}
	}
}

// 12.6.2p2: the name the index of a ctor-initializer holds one entry under.
//
// A mem-initializer-id that names a direct base is held under the whole name
// that class has, because 10p1 lets a class derive from two whose names end in
// the same component - `n1::b` and `n2::b` are two base subobjects and one key
// would make them one entry the second is refused at.  Every other id is held
// under the last component of what was written, which is 12.6.2p2's member; an
// entry read for one element of an expansion writes the pattern's own spelling,
// and every element writes the same one, so the class the element's own region
// makes of it is what tells those apart as well.
std::string SemaAnalyzer::base_key(const std::string& written,
                                   const Context& where,
                                   const SemaEntity* owner)
{
	try
	{
		SemaEntity* const found = resolve(written, where, LookupKind::Type);
		if (found != nullptr && names_a_type(*found) && owner != nullptr &&
		    names_direct_base(*owner, found->type))
		{
			return types_.user_name(found->type);
		}
	}
	catch (const std::runtime_error&)
	{
		// A name that reaches no region names no class either, and the
		// mem-initializer it was written in is refused where it is read.
	}
	return QualifiedName(written).last();
}

// 10p1: whether `type` is one of the classes `owner` derives from directly,
// which is what says a mem-initializer-id naming it is 12.6.2p2's base rather
// than a type of the same name standing somewhere the lookup reached.
bool SemaAnalyzer::names_direct_base(const SemaEntity& owner, TypeId type)
{
	const TypeId named = types_.strip_cv(type);
	for (std::size_t index = 0; index < owner.bases.size(); ++index)
	{
		if (types_.strip_cv(owner.bases[index].entity->type) == named)
		{
			return true;
		}
	}
	return false;
}

void SemaAnalyzer::hold_mem_initializer(
	const Pending& pending, const std::string& key,
	const MemInitializer& wrote,
	std::unordered_map<std::string, MemInitializer>& named)
{
	if (named.insert(std::make_pair(key, wrote)).second)
	{
		return;
	}
	// 12.6.2p6: a ctor-initializer that writes more than one mem-initializer
	// for one member is ill formed, which is not the same as the second one
	// being the one that has no effect.
	throw std::runtime_error("a constructor of " +
	                         types_.description(pending.function->type) +
	                         " initializes " + wrote.spelled + " twice");
}

// 12.6.2p6: a mem-initializer-id that names the constructor's own class is not
// 12.6.2p2's base or member at all - it delegates the whole initialization to
// another constructor of the same class.  The class's own name is what such a
// mem-initializer nearly always writes, so it is asked for in one probe of the
// index the list was already read into; a name that reaches the class some
// other way - a typedef-name, an injected-class-name of a class this one is
// nested in - costs one lookup, and only where p6's own rule already says the
// list is the one entry a delegating ctor-initializer may hold and that entry
// names neither a member nor the base under its own name.
const AstNode* SemaAnalyzer::delegating_initializer(
	const Pending& pending,
	std::unordered_map<std::string, MemInitializer>& named,
	const Context& inner)
{
	SemaEntity* const owner =
		pending.members != nullptr ? pending.members->owner : nullptr;
	if (owner == nullptr || named.empty())
	{
		return nullptr;
	}
	const std::string own = QualifiedName(types_.user_name(owner->type)).last();
	std::unordered_map<std::string, MemInitializer>::iterator wrote =
		named.find(own);
	if (wrote != named.end() &&
	    QualifiedName(wrote->second.spelled).qualified() &&
	    !names_the_class(wrote->second.spelled, *owner, inner))
	{
		// 12.6.2p2: the index the list was read into is keyed on the last
		// component of each mem-initializer-id, and a nested-name-specifier
		// reaches a class of its own - `struct S : N::S` writes `N::S(...)` for
		// its base and the component is the same `S` this class is called.  So
		// a spelling that wrote one is asked what it denotes before it is read
		// as 12.6.2p6's delegation; an unqualified one is not, because 12.6.2p2
		// looks it up in the scope of the constructor's class first and this
		// class's own injected-class-name is what stands there.
		wrote = named.end();
	}
	if (wrote == named.end())
	{
		if (named.size() != 1)
		{
			return nullptr;
		}
		std::unordered_map<std::string, MemInitializer>::iterator only =
			named.begin();
		const std::unordered_map<std::string, Binding>::const_iterator found =
			pending.members->names.find(only->first);
		if (found != pending.members->names.end() &&
		    found->second.ordinary != nullptr &&
		    declares_subobject(*found->second.ordinary, *pending.members))
		{
			// 12.6.2p2: the name is a non-static data member of the class, so
			// the mem-initializer names that member rather than the class.  A
			// typedef-name the class declares is not one of those, and naming
			// the class through it is exactly what this reading is for.
			return nullptr;
		}
		for (std::size_t index = 0; index < owner->bases.size(); ++index)
		{
			// 12.6.2p2: the one mem-initializer names a base of the class, so
			// it initializes that subobject rather than delegating.
			if (QualifiedName(
				    types_.user_name(owner->bases[index].entity->type)).last() ==
			    only->first)
			{
				return nullptr;
			}
		}
		if (!names_the_class(only->second.spelled, *owner, inner))
		{
			return nullptr;
		}
		wrote = only;
	}
	if (named.size() != 1)
	{
		// 12.6.2p6: a delegating mem-initializer shall be the only
		// mem-initializer of its ctor-initializer, because the constructor it
		// names initializes every base and every member itself.
		throw std::runtime_error("a delegating constructor of " +
		                         types_.description(pending.function->type) +
		                         " writes a second mem-initializer");
	}
	wrote->second.used = true;
	return wrote->second.written;
}

// 12.6.2p6: what a delegating constructor does before its body - one call of
// the constructor 13.3 chose out of the class's own set, on the object this one
// was itself called on.  4.10p3 converts nothing: `this` already points at the
// complete object, and the target runs its complete-object entry on it.
void SemaAnalyzer::write_delegating_initialization(const Pending& pending,
                                                   DumpNode& line,
                                                   const AstNode* written,
                                                   const Context& inner)
{
	SemaEntity& owner = *pending.members->owner;
	SemaEntity* target = nullptr;
	// 1.9p10 and 12.6.2: the mem-initializer's expression-list is a
	// full-expression, so a temporary written in it is destroyed once the call
	// it was written for has returned.
	open_full_expression();
	construct_object(owner, line, written, inner, Placement::Delegate, false,
	                 nullptr, false, nullptr, false, false, false, &target);
	close_full_expression(line);
	if (target == nullptr)
	{
		return;
	}
	if (target == pending.function)
	{
		// 12.6.2p6: the one cycle a single definition can close on its own,
		// caught where it is written rather than left to the walk below.
		throw std::runtime_error("a constructor of " +
		                         types_.description(owner.type) +
		                         " delegates to itself");
	}
	pending.function->delegates_to = target;
	delegations_.push_back(pending.function);
}

// 12.6.2p6: no constructor shall delegate to itself, however many delegations
// stand between.  Each constructor has at most one delegation, so the graph is
// a forest of chains and the whole unit is one walk of the constructors that
// delegate: a chain is followed until it reaches one already walked or one that
// delegates to nothing, and a constructor met twice on the chain being walked
// is the cycle.  Every constructor is coloured once, so n delegations cost n
// steps however the chains run through each other.
void SemaAnalyzer::check_delegation_cycles()
{
	const unsigned char kOnChain = 1;
	const unsigned char kWalked = 2;
	std::unordered_map<std::uint32_t, unsigned char> colour;
	std::vector<SemaEntity*> chain;
	for (std::size_t index = 0; index < delegations_.size(); ++index)
	{
		SemaEntity* at = delegations_[index];
		chain.clear();
		while (at != nullptr)
		{
			unsigned char& mark = colour[at->id];
			if (mark == kWalked)
			{
				break;
			}
			if (mark == kOnChain)
			{
				throw std::runtime_error("a constructor of " +
				                         types_.description(at->type) +
				                         " delegates to itself");
			}
			mark = kOnChain;
			chain.push_back(at);
			at = at->delegates_to;
		}
		for (std::size_t at_chain = 0; at_chain < chain.size(); ++at_chain)
		{
			colour[chain[at_chain]->id] = kWalked;
		}
	}
}

// 12.6.2p10: the base class subobjects are initialized first and in the order
// the base-specifier-list declared them, whatever place their mem-initializers
// were written in and whether or not one was written at all.
void SemaAnalyzer::write_base_initialization(
	const Pending& pending, DumpNode& line,
	std::unordered_map<std::string, MemInitializer>& named,
	const Context& inner)
{
	SemaEntity* const owner =
		pending.members != nullptr ? pending.members->owner : nullptr;
	for (std::size_t index = 0;
	     owner != nullptr && index < owner->bases.size(); ++index)
	{
		write_one_base_initialization(pending, line, named, inner,
		                              *owner->bases[index].entity);
	}
}

// 12.6.2p2: what one of those subobjects is initialized by - the
// mem-initializer whose id names its class, or nothing the program wrote.
// 12.9p8's inheriting constructor writes no ctor-initializer at all and
// initializes the base it was declared from from its own parameters instead.
void SemaAnalyzer::write_one_base_initialization(
	const Pending& pending, DumpNode& line,
	std::unordered_map<std::string, MemInitializer>& named,
	const Context& inner, SemaEntity& base)
{
	if (pending.function->inherited != nullptr &&
	    pending.function->inherited->region == base.scope)
	{
		// 12.9p8: an inheriting constructor initializes the base subobject by
		// calling the constructor it was declared from, with its own parameters
		// as the arguments.  The parameters are the declarations the definition
		// just made, in the order the declaration wrote them.
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
		construct_object(base, line, nullptr, inner, Placement::Base, false,
		                 nullptr, false, &forwarded);
		return;
	}
	const AstNode* written = nullptr;
	// 12.6.2p2: an id that named this base was held under the whole name the
	// class has, which is what tells one base from another whose name ends in
	// the same component.
	std::unordered_map<std::string, MemInitializer>::iterator wrote =
		named.find(types_.user_name(base.type));
	if (wrote != named.end() && wrote->second.used)
	{
		wrote = named.end();
	}
	if (wrote == named.end())
	{
		// 12.6.2p2: the mem-initializer-id may be any name for the base class,
		// which a typedef-name is one of.  Its own name is asked about first,
		// so only a ctor-initializer that spelled the base some other way costs
		// one lookup per mem-initializer it wrote.
		for (wrote = named.begin(); wrote != named.end(); ++wrote)
		{
			if (!wrote->second.used &&
			    names_the_class(wrote->second.spelled, base, inner))
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
	if (written != nullptr || !trivially_constructed(base.type))
	{
		// 14.5.3p4: an entry read for one element of an expansion is read in
		// the region that element binds its packs in, which is what makes the
		// arguments of `base<I>(tag<I>(), 0)...` this element's own.
		Context where = inner;
		if (wrote != named.end() && wrote->second.region != nullptr)
		{
			where.scope = wrote->second.region;
		}
		// 1.9p10 and 12.6.2: a mem-initializer's expression-list is a
		// full-expression, so a temporary written in it is destroyed once the
		// subobject it built has been.
		open_full_expression();
		construct_object(base, line, written, where, Placement::Base);
		close_full_expression(line);
		return;
	}
	const SemaEntity* const built = default_constructor(base.type);
	if (built == nullptr || !built->constexpr_function)
	{
		// 7.1.5p4: a base class subobject shall be initialized too, and where
		// nothing here has anything to construct that is because the base's own
		// default constructor does nothing - which 12.1p5 makes a constexpr
		// constructor only where it leaves no subobject holding no value.
		ConstexprRequirement(*this).require_initialized(*pending.function,
		                                               base.name);
	}
}

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
		write_member_parameters(pending, line, inner);
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
	std::unordered_map<std::string, MemInitializer> named;
	read_mem_initializers(pending, inner, named);
	const AstNode* const delegated =
		delegating_initializer(pending, named, inner);
	if (delegated != nullptr)
	{
		// 12.6.2p6: the constructor this one names initializes every base and
		// every member, so this definition writes that one call and no step of
		// its own.
		write_delegating_initialization(pending, line, delegated, inner);
		return;
	}
	// 9.5p1 and 12.6.2p8: every member of a union stands in the one storage, so
	// a constructor of one initializes at most one of them - the variant member
	// a mem-initializer designated, else the one 9.5p2 let write a
	// brace-or-equal-initializer, and no member at all where the
	// ctor-initializer designated another.  Which of the two it is, is one probe
	// of the class's region per mem-initializer the list held.
	const bool is_union =
		members.owner != nullptr && one_storage(members.owner->type);
	bool designated_variant = false;
	for (std::unordered_map<std::string, MemInitializer>::const_iterator at =
	         named.begin();
	     is_union && !designated_variant && at != named.end(); ++at)
	{
		const std::unordered_map<std::string, Binding>::const_iterator found =
			members.names.find(at->first);
		designated_variant = found != members.names.end() &&
			found->second.ordinary != nullptr &&
			declares_subobject(*found->second.ordinary, members);
	}
	write_base_initialization(pending, line, named, inner);
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
		if (written == nullptr && member.default_initializer &&
		    !designated_variant)
		{
			// 12.6.2p8 and 9.2p2: a brace-or-equal-initializer is read in the
			// class it was written in, which is a complete-class context.  In a
			// union it is read only where no *other* variant member was
			// designated: what the ctor-initializer named is what the one
			// storage holds, and the initializer written on the member the
			// constructor did not name says nothing about it.
			const std::unordered_map<std::uint32_t, HeldInitializer>::const_iterator
				found = member_initializers_.find(member.id);
			if (found != member_initializers_.end())
			{
				written = found->second.written;
				where.scope = found->second.scope;
				where.dump = where.scope->dump;
			}
		}
		if (is_union && written == nullptr)
		{
			// 12.6.2p8: a variant member no mem-initializer designated and no
			// brace-or-equal-initializer reaches is not initialized at all -
			// not default-initialized, which is what an ordinary member of the
			// same type would be.  9.5p1's one storage is what makes the
			// difference: the constructor says which member stands in it, and
			// one that says nothing leaves it holding no member of any of them.
			continue;
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
				const SemaEntity* const built =
					default_constructor(types_.element_of(type));
				if (built == nullptr || !built->constexpr_function)
				{
					// 7.1.5p4: every constructor involved in initializing a
					// non-static data member shall be a constexpr constructor,
					// and 12.1p5 leaves the one the standard defines for a
					// class holding an uninitialized scalar outside that.
					ConstexprRequirement(*this).require_initialized(
						*pending.function, member.name);
				}
				continue;
			}
			// The action names the member through `this`, so it needs no line
			// of its own to say which subobject is being initialized.
			open_full_expression();
			construct_object(member, line, written, where, Placement::Member);
			close_full_expression(line);
			continue;
		}
		if (written == nullptr)
		{
			// 8.5p6 and 12.6.2p8: a member of any other type that no
			// initializer reaches is default-initialized, which does nothing.
			// 7.1.5p4 is the one reader that cares that it does nothing: a
			// constexpr constructor leaving a member holding no value is the
			// program's error rather than a step with no action to write.
			ConstexprRequirement(*this).require_initialized(*pending.function,
			                                               member.name);
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
			// the one expression written in the parentheses - which 14.5.3p4
			// makes a question about the run a `pattern...` entry stands for
			// rather than about how many entries the program wrote.
			Clauses passed(written, *this, where);
			if (passed.spent())
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
			if (passed.list.size() != 1)
			{
				throw std::runtime_error("a mem-initializer of " + member.name +
				                         " passes more than one argument to a "
				                         "member of non-class type");
			}
			open_full_expression();
			initialize(passed.next(), type, passed.in(where), node);
			close_full_expression(line);
			continue;
		}
		open_full_expression();
		if (written->kind == AstKind::BracedInitList)
		{
			// 8.5.1p2: the member is an aggregate or an array and the clauses
			// initialize its subobjects where they stand, which is the same
			// reading a declaration of an object of it gets.  Nothing here is
			// an object of its own for 13.3.3.1.5 to build: 9.2p13 already
			// laid this one out inside the object being constructed.
			list_initialize(*written, type, where, node);
		}
		else if (!StringInitialization(*this).as_object(types_.strip_cv(type),
		                                               *written, where, node))
		{
			// 8.5.2p1: a member that is an array of character type takes the
			// code units of a string literal written for it, which is no
			// conversion of the literal to the array's type and so is asked
			// before the initialization every other expression gets - the same
			// order a declaration of such an array is read in, because 12.6.2p8
			// makes the brace-or-equal-initializer that initialization.
			initialize(*written, type, where, node);
		}
		close_full_expression(line);
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
	if (members.owner != nullptr && one_storage(members.owner->type))
	{
		// 12.8p15 and p28: the member the standard defines for a union copies
		// the object representation of it and nothing else.  9.5p1's one
		// storage is why there is no other form: at most one of the members
		// declared holds an object, the transfer cannot ask which, and a walk
		// of the declarations would carry every one of them through the one
		// storage in turn - reading bytes no lifetime wrote and writing over
		// the bytes the step before it carried.
		write_storage_transfer(*parameter, line, 0,
		                       types_.object_size(members.owner->type), kNoType);
		return;
	}
	const bool assigning = kind == kCopyAssignmentTransfer ||
		kind == kMoveAssignmentTransfer;
	const std::vector<BaseClass> no_bases;
	const std::vector<BaseClass>& bases =
		members.owner != nullptr ? members.owner->bases : no_bases;
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
	// 10.3p1 and 12.8p12: a class that added the vpointer has it standing in
	// front of its members, and the vpointer of the object being written into
	// names that object's own class rather than the source's - so there is no
	// leading run to carry and each member is carried on its own.
	const bool added_vptr =
		members.owner != nullptr && members.owner->introduces_vptr;
	if (bases.empty() && !added_vptr)
	{
		// 12.8p15: the members carried as storage are carried in one piece, and
		// the piece is only the object's own storage where nothing stands
		// before them in it.  9p6's member that holds nothing holds none of
		// that storage, so it ends the run rather than joining it: what a
		// transfer of it comes to is its own question, asked below.
		unsigned long long span = 0;
		for (; first < fields.size(); ++first)
		{
			const SemaEntity& field = *fields[first];
			if (field.bit_field ||
			    types_.is_empty_class(member_copy_type(field.type)) ||
			    !carried_as_storage(field.type, kind))
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
	for (std::size_t index = 0; index < bases.size(); ++index)
	{
		SemaEntity& base = *bases[index].entity;
		if (carries_nothing(base.type, kind))
		{
			continue;
		}
		Value source = base_value(parameter_value(*parameter, line), base,
		                          false);
		line.children.pop_back();
		transfer_source(source, kind);
		if (assigning)
		{
			write_transfer_assignment(base, source, line, inner, Placement::Base);
		}
		else
		{
			construct_object(base, line, nullptr, inner, Placement::Base, true,
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
		    carried_as_storage(field.type, kind))
		{
			// 12.8p15: an array member is carried element by element, and where
			// an element is carried by its bytes the whole array is those bytes
			// - which is the one form the transfer has for it, because the
			// member the element's class holds takes an element and no call of
			// it takes the array, and an element of any other type is 8.5's
			// initialization of storage no name reaches.  A run the leading
			// prefix already covered never reaches here.
			write_storage_transfer(*parameter, line, field.offset,
			                       types_.object_size(field.type), kNoType);
			continue;
		}
		DumpNode& access = model_.open_node(line, std::string());
		Value source = member_value(field, parameter_value(*parameter, access),
		                            field.name, access);
		line.children.pop_back();
		transfer_source(source, kind);
		// 12.8p15 and p28: an array member whose elements are carried by a call
		// is carried one element at a time, and the element of the source is
		// the one at the same index.  What the step reads is that element, so
		// the source is an element here exactly as the destination already is -
		// the line keeps the array it was written from, because that is where
		// the element stands, and the walk around the step says which.
		const bool elements =
			types_.kind(types_.strip_cv(field.type)) == TypeKind::Array;
		if (elements)
		{
			source.type = source.spelled = types_.qualified(
				types_.element_of(source.type), types_.object_cv(source.type));
		}
		if (assigning)
		{
			if (elements)
			{
				DumpNode& walk = open_fact(line, "array-transfer",
				                           FactKind::ArrayTransfer);
				walk.fact.type = field.type;
				write_transfer_assignment(field, source, walk, inner,
				                          Placement::Member, true);
				continue;
			}
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
// nothing but copy those bytes, and where 12.4p8 leaves nothing to run at the
// end of an object of it - because a second object made out of those bytes is
// a second end to run, which is what says the bytes are not the whole of it.
bool SemaAnalyzer::carried_as_storage(TypeId type, unsigned char kind)
{
	if (types_.is_reference(type))
	{
		// 8.3.2p1 and 8.5.3p2: a reference is not an object, so a subobject of
		// reference type has no bytes of its own for a copy to carry.  What
		// 12.8p15 gives it is the initialization of a reference, which binds
		// the one this transfer builds to the object the source's is bound to -
		// the read of the source the ABI's reference projection is, and the
		// write of the binding the destination holds.
		return false;
	}
	const TypeId bare = member_copy_type(type);
	if (!types_.is_class(bare))
	{
		return true;
	}
	if (!vacuous_destruction(bare))
	{
		return false;
	}
	const SemaEntity* const carried = selected_transfer(bare, kind);
	return carried != nullptr && carried->trivial && !carried->deleted;
}

// 9p6 and 12.8p15: whether carrying a subobject of this type comes to nothing
// at all - the class holds nothing, so there are no bytes, the member its own
// class has for this transfer does nothing, so there is no call either, and
// 12.4p8 leaves nothing at the end of the object it builds.
bool SemaAnalyzer::carries_nothing(TypeId type, unsigned char kind)
{
	const TypeId bare = member_copy_type(type);
	if (!types_.is_class(bare) || !types_.is_empty_class(bare) ||
	    !vacuous_destruction(bare))
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
                                             Placement where, bool elements)
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
	if (elements)
	{
		// 12.8p28: what this step assigns is one element of the array member,
		// which is the object the assignment operator of the element's own
		// class is called on.  The line goes on naming the array, so the walk
		// around the step is what says which element it stands at.
		target.type = target.spelled = types_.qualified(
			types_.element_of(target.type), types_.object_cv(target.type));
	}
	step.children.push_back(source.node);
	std::vector<Value> operands;
	operands.push_back(target);
	operands.push_back(source);
	Value chosen;
	if (types_.is_class(member_copy_type(target.type)))
	{
		if (!OperatorCall(*this).expression(OP_ASS, inner, step, operands,
		                                    OperatorCall::member_only(OP_ASS),
		                                    chosen))
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
                                           DumpNode& line, const Context& inner)
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
		if (types_.is_class(types_.strip_cv(member.type)))
		{
			// 12.8p31 and 5.2.2p4: the parameter is an object of its own that
			// this call owns and that nothing after this step reads, so what
			// carries it into the member is a read of it as an xvalue - which
			// is what makes 13.3 choose the member's own move constructor
			// where its class has one and its copy constructor where it has
			// not.  The step is the `constructor-action` any other member of
			// class type carries, so the object it names is the subobject and
			// not a second one.
			DumpNode held;
			Value source = parameter_value(*parameters[at], held);
			source.category = ValueCategory::XValue;
			construct_object(member, line, nullptr, inner, Placement::Member,
			                 false, &source);
			++at;
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
// **non-variant** members run, in the reverse of the order the members were
// constructed in.  9.5p1's one storage is what that word is there for: a
// constructor of a union says which of its members stands in that storage and
// the destructor cannot ask, so the members a union declares are not objects
// whose lifetimes this end ends - walking them would call a destructor on
// storage no lifetime began in, which is what 12.6.2p8's reading of the same
// storage already refuses to write the construction of.
void SemaAnalyzer::write_member_destructions(Scope& members, DumpNode& line)
{
	if (members.owner != nullptr && one_storage(members.owner->type))
	{
		return;
	}
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
		Access(*this).require_destruction_access(member, &members);
		destructor_action(member, line, Placement::Member);
	}
	for (std::size_t index = members.owner != nullptr
	                             ? members.owner->bases.size() : 0;
	     index > 0; --index)
	{
		// 12.4p8: the base class subobjects are destroyed after every member,
		// in the reverse of the order 12.6.2p10 constructed them in.
		SemaEntity& base = *members.owner->bases[index - 1].entity;
		Access(*this).require_destruction_access(base, &members);
		destructor_action(base, line, Placement::Base);
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
	Access(*this).require_destruction_access(entity, target.scope);
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
			DeclaredLifetime held;
			held.entity = &entity;
			held.line = &line;
			declared_lifetimes_.push_back(held);
			return;
		}
		static_lifetimes_.push_back(&entity);
		return;
	}
	// 3.6.3p1 and 6.7p4: a block-scope object declared `static` is destroyed
	// when the program ends and not when its block does, and the point the
	// runtime is handed that destruction at is the one initialization the
	// object gets - which is inside the block and reached at most once.  So the
	// action stands under the declaration, exactly as an object of a thread
	// does, rather than at the end of the block that names it.
	if (entity.local_static)
	{
		DeclaredLifetime held;
		held.entity = &entity;
		held.line = &line;
		declared_lifetimes_.push_back(held);
		return;
	}
	// 3.7.3p1: what is left is an object of the block, whatever its type.
	if (!lifetimes_.empty())
	{
		lifetimes_.back().push_back(&entity);
		if (ends_in_call(entity))
		{
			++live_destructions_;
			// 15.2p2: the declaration begins a lifetime an exception out of
			// anything after it has to end, so the line that begins it names
			// the destructor as well as the line that ends it.
			line.fact.destruction = class_destructor(types_.element_of(entity.type));
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
	end_parameter_lifetimes(line);
}

// 5.2.2p4 and 3.8p1: a parameter of class type is an object of the function
// whose storage the *caller* laid out and whose initialization the caller ran,
// so what is left to the function is its end - which is where control leaves
// the function and after every object a block of it declared has ended.
void SemaAnalyzer::open_parameter_lifetimes(DumpNode& line)
{
	for (std::size_t index = 0; index < line.children.size(); ++index)
	{
		DumpNode& child = *line.children[index];
		if (child.fact.kind != FactKind::Parameter)
		{
			break;
		}
		// 5.2.2p4: only the parameter the boundary left standing in the
		// caller's storage is the function's to end - the one the boundary
		// carried as bytes is a copy of the caller's object that the caller
		// laid out and the caller is done with.
		if (!types_.passes_indirectly(child.fact.type))
		{
			continue;
		}
		// 12.4p5: whether the class has an end to run at all.  It is asked of
		// the class rather than of 12.4p8's reading of a body, because what the
		// boundary owes is the destructor the class declares and not whatever
		// that destructor's definition turns out to write.  It is 12.4p5's
		// triviality and not 12.4p3's declaration: the reference writes this
		// end for a declared-but-trivial destructor only where some other use
		// of the unit already demanded its definition, which is a reading of
		// the whole unit and not of the boundary.
		SemaEntity* const destructor =
			class_destructor(types_.strip_cv(child.fact.type));
		if (child.fact.entity == nullptr || destructor == nullptr ||
		    destructor->trivial)
		{
			continue;
		}
		// 15.2p2: the object stands from the moment the body begins, so an
		// exception out of anything the body does has to end it - which is the
		// same end, written on the place the lifetime began as well as on the
		// places it is reached.
		child.fact.destruction = destructor;
		note_destruction_entry(*destructor, false);
		parameter_objects_.push_back(child.fact.entity);
	}
}

void SemaAnalyzer::end_parameter_lifetimes(DumpNode& line)
{
	for (std::size_t at = parameter_objects_.size(); at-- > 0;)
	{
		destructor_action(*parameter_objects_[at], line, Placement::Parameter);
	}
}

// 12.1p5: whether default-initializing an object of `type` does nothing at all,
// which is the one question that says a subobject needs no action written for
// it.  12.6.2p8 leaves such a member with no initialization to describe.
bool SemaAnalyzer::trivially_constructed(TypeId type)
{
	for (const SemaEntity* at = class_constructors(types_.element_of(type));
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
//
// Which of the two questions an end asks is which kind of object is ending.
// An object a declaration named is one the program can watch leave its block,
// and 12.4p8 says what running its destructor comes to; an object the
// *translation* made - 12.2p1's temporary, and the one 12.2p5 moved out of its
// full-expression into this block - is reached through an address alone, and
// the call is the one mark its end has, so it is written wherever the program
// declared a destructor below its class at all.  An unnamed object is the one
// the translation made, which is what `destructor_action` already reads it as.
bool SemaAnalyzer::ends_in_call(const SemaEntity& entity)
{
	return entity.name.empty() ? declared_destruction(entity.type)
	                           : !vacuous_destruction(entity.type);
}

// 12.4p3: the same question asked of a type rather than of an object, held per
// class where the class completes - so a chain of subobjects n deep costs one
// read and never a walk.
bool SemaAnalyzer::declared_destruction(TypeId type)
{
	return types_.has_declared_destruction(types_.element_of(type));
}

// 12.4p8: whether the definition `node` gives a function writes any statement
// at all.  The body is the last child a definition has, and this is the one
// reading of it, so the definition read in place and the same node found in the
// syntax ahead of the read cannot answer differently.
bool SemaAnalyzer::writes_no_statement(const AstNode& node)
{
	if (node.kind == AstKind::SpecialMemberDeclaration)
	{
		// 8.4.2p2: the definition is `= default`, so 12.4p4 and 12.6.2 write
		// what it comes to and the program wrote no statement of its own.
		// `= delete` is no definition at all and nothing runs it.
		const AstNode* const explicitly = child_of(node, AstKind::Initializer);
		return explicitly != nullptr && !explicitly->children.empty() &&
			explicitly->children[0]->text == "default";
	}
	const AstNode* const body =
		node.children.empty() ? nullptr : node.children.back();
	if (body == nullptr || body->kind != AstKind::CompoundStatement ||
	    !body->children.empty())
	{
		return false;
	}
	// 12.6.2p8: a mem-initializer initializes a subobject however empty the
	// compound-statement written after it is, so a definition that writes one
	// comes to something even where it writes no statement at all.  A
	// destructor has none to write, which leaves this the same question for
	// both members that ask it.
	for (std::size_t index = 0; index + 1 < node.children.size(); ++index)
	{
		if (node.children[index]->kind == AstKind::CtorInitializer)
		{
			return false;
		}
	}
	return true;
}

// 3.4.1p8 and 9.3p2: the definitions of members written outside their class,
// taken from the unit's syntax before any of it is read.
//
// A member defined outside its class stands wherever the program put it, which
// is to say possibly after a body that already has to know what defining it
// settled: 12.4p8's question of whether destroying an object comes to anything
// is the one this milestone asks, and every function body at namespace scope is
// read where it is written.  The parse of the whole unit is complete before the
// first declaration of it is read, so the answer is taken from the syntax once
// rather than from wherever in the read the definition happened to fall.  The
// walk leaves every function body alone, because 9.3p2 lets no such definition
// stand in one.
void SemaAnalyzer::collect_unit_definitions(const AstNode& node)
{
	if (node.kind == AstKind::CompoundStatement)
	{
		return;
	}
	if (node.kind == AstKind::SpecialMemberDefinition ||
	    node.kind == AstKind::SpecialMemberDeclaration)
	{
		// 8.4.2p2: a declaration written outside the class carries `= default`
		// or `= delete`, either of which settles what running the member comes
		// to as surely as a body does - so it is collected with the bodies.
		const QualifiedName spelled(node.text);
		if (spelled.qualified())
		{
			unit_definitions_[spelled.last()].push_back(&node);
		}
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		collect_unit_definitions(*node.children[index]);
	}
}

// 12.4p8 and 12.1p5: whether the definition this unit gives `member` writes a
// statement, asked where that definition may not have been read yet.
//
// A class's destructor has one unqualified name and its constructors share
// one, so the definitions to consider are the ones the walk collected under it
// - one, in any program that defines it once - and 3.4.1p8's prefix, resolved
// against the class itself, says which of them defines this one rather than
// another class's of the same name.  A prefix that reaches the class only
// through a name some other region binds is found by none of this, and the
// action is written as the call it is.
void SemaAnalyzer::note_definition_body(SemaEntity& member,
                                        const SemaEntity& owner)
{
	if (member.defined || owner.scope == nullptr)
	{
		return;
	}
	const std::unordered_map<std::string,
	                         std::vector<const AstNode*> >::const_iterator
		found = unit_definitions_.find(member.name);
	if (found == unit_definitions_.end())
	{
		return;
	}
	Context ctx;
	ctx.scope = owner.scope;
	ctx.dump = owner.scope->dump;
	for (std::size_t index = 0; index < found->second.size(); ++index)
	{
		const AstNode& node = *found->second[index];
		Scope* region = nullptr;
		try
		{
			region = resolve_prefix(QualifiedName(node.text), ctx);
		}
		catch (const std::runtime_error&)
		{
			// A prefix that names no region here defines no member of this
			// class, and what the program did write is refused where it is
			// read.
			continue;
		}
		if (region != owner.scope)
		{
			continue;
		}
		member.empty_body = writes_no_statement(node);
		return;
	}
}

// 12.4p8 and 3.8p1: whether destroying an object of this type comes to nothing.
//
// 12.4p5 makes a destructor trivial where the program wrote none and no
// subobject needs one, and running that one is nothing.  A destructor the
// program *did* write whose body has no statement is the same nothing wherever
// its class holds no subobject whose own destruction does something - the
// clause that says the destructor exists is not the clause that says running it
// does anything.  So this is the one question the end of a lifetime asks, and
// every action - a local going out of scope, a temporary, a subobject of a
// destructor the standard defines - asks it here.
//
// The walk is the subobject tree, and its answer is held per type: a class n
// members deep is walked once however many objects of it a function declares.
bool SemaAnalyzer::vacuous_destruction(TypeId type)
{
	const TypeId bare = types_.element_of(type);
	const std::unordered_map<TypeId, unsigned char>::const_iterator held =
		vacuous_.find(bare);
	if (held != vacuous_.end())
	{
		return held->second != 0;
	}
	SemaEntity* const destructor = class_destructor(bare);
	SemaEntity* const owner = model_.type_owner(bare);
	if (destructor != nullptr && owner != nullptr)
	{
		// 12.4p8: what running the destructor comes to is what its definition
		// writes, and a definition written outside the class stands wherever
		// the program put it - which may be after the body asking this.
		note_definition_body(*destructor, *owner);
	}
	bool nothing = destructor == nullptr || destructor->trivial;
	// 12.4p11 and 10.3p10: a *virtual* destructor is one the class's table
	// names, and running it writes the vpointer of the class it belongs to
	// whatever else its body comes to - so running one is never nothing,
	// however little the program wrote in it and however vacuous every
	// subobject of the class is.  A polymorphic class whose destructor 12.4p5
	// leaves trivial has no such body, and destroying an object of it is the
	// nothing 12.4p5 already says it is.
	const bool dispatches = destructor != nullptr &&
		destructor->virtual_function;
	if (dispatches)
	{
		nothing = false;
	}
	if (!nothing && !dispatches && !destructor->deleted &&
	    (destructor->empty_body || destructor->defaulted))
	{
		// 12.4p8: what a destructor comes to is its body and then the
		// destruction of each subobject, so one whose body writes nothing - and
		// one 12.4p4 gave no body at all - comes to what its subobjects come
		// to.  12.4p5 only calls that trivial where the program wrote no
		// destructor anywhere below, and what running it does is the same
		// either way.
		nothing = owner != nullptr && owner->scope != nullptr;
		for (std::size_t index = 0;
		     nothing && owner != nullptr && index < owner->bases.size(); ++index)
		{
			// 12.4p8: each base class subobject is destroyed too.
			nothing = vacuous_destruction(owner->bases[index].entity->type);
		}
		// 12.4p8: the members whose destructors run are the non-variant ones, so
		// a union's end of a lifetime is its body and nothing else - the same
		// 9.5p1 reading `write_member_destructions` writes, asked here so the
		// question and the code it decides give one answer.
		const bool variant = owner != nullptr && one_storage(owner->type);
		for (std::size_t index = 0;
		     nothing && !variant && owner != nullptr &&
		     owner->scope != nullptr &&
		     index < owner->scope->declarations.size(); ++index)
		{
			const SemaEntity& member = *owner->scope->declarations[index];
			nothing = !declares_subobject(member, *owner->scope) ||
				types_.is_reference(member.type) ||
				vacuous_destruction(member.type);
		}
	}
	vacuous_[bare] = nothing ? 1u : 0u;
	return nothing;
}

bool SemaAnalyzer::lifetimes_pending() const
{
	// The count is carried rather than recomputed: a jump asks this question
	// once per jump, and walking every open block for it would cost the depth
	// of the blocks around each one.
	return live_destructions_ != 0;
}
