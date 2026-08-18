#include "sema_elision.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_access.h"
#include "sema_analyzer.h"

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

}  // namespace

Elision::Elision(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

// 12.8p32: a copy 12.8p31 elides is still a copy the program wrote, so the
// constructor 13.3 would have chosen for it has to be one this region may name
// and one the standard has a definition for.  The elision says the call does
// not run, not that the program did not have to be allowed to write it.  The
// initializer is a prvalue, so what 13.3 chooses is the class's move
// constructor where it has one and its copy constructor otherwise - which is
// the one fact 12.8p15 already settled on the class rather than a resolution
// run again here.
void Elision::require_transfer(TypeId type, const SemaContext& ctx)
{
	TypeTable& types = analyzer_.types_;
	SemaEntity* const chosen =
		analyzer_.selected_transfer(type, kMoveConstructorTransfer);
	if (chosen == nullptr || chosen->deleted)
	{
		throw std::runtime_error(
			"an object of " + types.description(types.strip_cv(type)) +
			" is initialized from a value of its own class, whose copy 12.8p32 "
			"asks for a constructor the program has none of");
	}
	if (ctx.scope != nullptr && !Access(analyzer_).accessible(*chosen, ctx.scope))
	{
		throw std::runtime_error(
			"an object of " + types.description(types.strip_cv(type)) +
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
// copy for the elision to remove.  What the elision removes is the call;
// 12.8p32 asked for access to the constructor above, and `select_overload` and
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
void Elision::release_created_object(DumpNode& node)
{
	DumpNode& at = created_object_node(node);
	AnalyzedValue created;
	created.node = &at;
	analyzer_.release_temporary(created);
	at.fact.destruction = nullptr;
	at.fact.object = nullptr;
}

// 5.2.9p4 and 12.2p1: the node the object a prvalue creates stands on.  A cast
// to a class type *is* the direct-initialization of the object under it, so
// the two name one object and the fact of it is written on the one below - and
// every reader that has to reach the object rather than the value walks there,
// because a question asked of the cast alone is answered about a node that
// holds no object at all.
DumpNode& Elision::created_object_node(DumpNode& node)
{
	TypeTable& types = analyzer_.types_;
	DumpNode* at = &node;
	while (at->fact.object == nullptr && at->fact.kind == FactKind::Cast &&
	       at->fact.category == ValueCategory::PRValue &&
	       !at->children.empty() &&
	       types.strip_cv(at->children[0]->fact.type) ==
	           types.strip_cv(at->fact.type))
	{
		at = at->children[0];
	}
	return *at;
}

bool Elision::into_destination(const SemaEntity& constructor,
                               std::vector<AnalyzedValue>& arguments,
                               TypeId object_type, DumpNode& line,
                                  DumpNode& action, bool into_temporary,
                                  bool written_call)
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
	TypeTable& types = analyzer_.types_;
	AnalyzedValue& source = arguments[0];
	if (source.node == nullptr || source.category != ValueCategory::PRValue ||
	    types.strip_cv(source.type) != types.strip_cv(object_type) ||
	    !creates_its_object(*source.node, types))
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
	release_created_object(*source.node);
	source.node->fact.written_call = written_call;
	line.children.pop_back();
	line.children.push_back(source.node);
	return true;
}

WrittenInitializer Elision::read_initializer(
	const AstNode* written, TypeId object_type, const SemaContext& ctx,
	bool value_init)
{
	// 8.5p15 and 8.5p16: which of the arguments the program wrote reach the
	// constructor, and whether 13.3.1.4 leaves out the ones declared `explicit`.
	TypeTable& types = analyzer_.types_;
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
		analyzer_.resolve(written->children[0]->text, ctx, LookupKind::Type);
	if (named == nullptr || !names_a_type(*named) ||
	    types.strip_cv(named->type) != types.strip_cv(object_type))
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

// 8.5p14 and 12.8p31: the whole of a copy-initialization written `= e`, where
// what `e` is worth may already be the object.
//
// The initializer is read first, into the destination's own line, because
// 12.8p31 lets a value of the object's own type be what initializes it with no
// constructor standing between them - so what the reading left is either the
// initialization itself or one operand 13.3 has still to resolve.  A prvalue
// that creates the object it is worth creates this one; 12.8p12's class, whose
// bytes stand for the object, needs no call to leave out and the two are one
// anyway.  A glvalue names an object that goes on existing, and a prvalue that
// only selects among objects - a conditional, a comma - created its elsewhere,
// so 8.5p14 leaves each of them the call of the transfer 13.3 chooses and the
// line is given back for that call to be written under.
bool Elision::of_written_prvalue(TypeId object_type, const AstNode& written,
                                 const SemaContext& ctx, DumpNode& line,
                                 bool member, bool written_call,
                                 AnalyzedValue& source)
{
	TypeTable& types = analyzer_.types_;
	source = analyzer_.expression(written, ctx, line);
	if (member || source.node == nullptr ||
	    source.category != ValueCategory::PRValue ||
	    types.strip_cv(source.type) != types.strip_cv(object_type) ||
	    (!creates_its_object(*source.node, types) &&
	     !types.bytes_stand_for_object(types.strip_cv(object_type))))
	{
		line.children.pop_back();
		return false;
	}
	if (creates_its_object(*source.node, types))
	{
		// 12.2p3: the object the prvalue creates is this one, so the
		// full-expression that was holding the end of its lifetime is not what
		// ends it - the destination's own end is.
		release_created_object(*source.node);
	}
	source.node->fact.written_call = written_call;
	require_transfer(object_type, ctx);
	return true;
}
