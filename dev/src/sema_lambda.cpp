#include "sema_lambda.h"

#include <stdexcept>
#include <string>

#include "sema_analyzer.h"
#include "sema_declaration.h"
#include "sema_facts.h"
#include "sema_scope.h"
#include "token_model.h"

// 5.1.2's closure class, built as the class-specifier the standard describes.
// See `sema_lambda.h` for why the syntax is made rather than read.

namespace
{

// The nodes reused from the lambda-expression itself - its
// parameter-declaration-clause, its exception-specification's condition, its
// trailing-return-type and its compound-statement - are the parse's own, and
// the parse owns them without const: what the analysis holds is a reading of
// the tree and not the arena under it.  Standing one of them under a node the
// analysis built writes nothing into it.
AstNode* borrowed(const AstNode* written)
{
	return const_cast<AstNode*>(written);
}

const AstNode* child_kind(const AstNode& node, AstKind kind)
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

// 5.1.2p1: the capture-list a lambda-introducer wrote, as the terminals between
// the brackets.  A capture is an object of the closure class rather than a name
// its body reaches, so a lambda that wrote one is a shape whose members this
// milestone does not describe - and saying so is not the same as reading it as
// a captureless one, whose body would then name the enclosing function's own
// storage from a class that holds none of it.
bool captures_nothing(const AstNode& introducer)
{
	for (std::size_t index = 0; index < introducer.text.size(); ++index)
	{
		const char written = introducer.text[index];
		if (written != '[' && written != ']' && written != ' ' &&
		    written != '\t')
		{
			return false;
		}
	}
	return true;
}

// 5.1.2p3: the reading of one body a lambda-expression stands in, which is what
// tells one closure class from another.
//
// A function's body is read *twice over* here: 5.1.2p4's deduced return type is
// read where 8.3.5p2's trailing-return-type is - in the declarator's own
// regions, before the body - and the compound-statement is read again at
// 9.2p2's closing brace, in a block region under them.  A lambda-expression
// written in the returned expression is therefore reached by both readings, and
// keyed by the block it happens to stand in it would declare a class per
// reading: one class per lambda per level, doubled at every level below it.
//
// So the key is the outermost of the regions one function declarator opens -
// the prototype region, the function's own and every block inside them - which
// both readings stand in and which 14.7.1p1's next specialization opens a fresh
// one of.  A closure class between them is walked through, because it is a
// region this reading made rather than one the program wrote: the deduced
// return type is read *in* it and the body is read in a block under it, and
// they are one reading of the body around it.  A class the program declared, a
// namespace or a template head ends the walk - a member template specialized
// twice is two readings of one body and two regions, which is exactly what
// keying on the class around it would lose.
const Scope& reading_region(const Scope& in)
{
	const Scope* region = &in;
	for (const Scope* up = &in; up != nullptr; up = up->parent)
	{
		const bool inside_one_reading =
			up->kind == ScopeKind::Prototype ||
			up->kind == ScopeKind::Function || up->kind == ScopeKind::Block ||
			(up->kind == ScopeKind::Class && up->owner != nullptr &&
			 up->owner->closure_class);
		if (!inside_one_reading)
		{
			break;
		}
		region = up;
	}
	return *region;
}

// 6.6.3p1: a `return` with an expression, anywhere in the body the lambda
// wrote.  5.1.2p4 gives the call operator the type of that expression, and
// `void` where the body holds none - which is what makes the walk a question
// about the whole body rather than about its first statement.
const AstNode* returned_expression(const AstNode& body)
{
	for (std::size_t index = 0; index < body.children.size(); ++index)
	{
		const AstNode& statement = *body.children[index];
		if (statement.kind == AstKind::ReturnStatement)
		{
			if (!statement.children.empty())
			{
				return statement.children[0];
			}
			continue;
		}
		if (statement.kind == AstKind::LambdaExpression ||
		    statement.kind == AstKind::FunctionDefinition)
		{
			// 6.6.3p1: a `return` belongs to the innermost function around it,
			// and those two are the only nodes that write one.  A lambda
			// written in this body has a body - and a return type - of its
			// own, and so does a member of a class 9.3p1 let this body
			// declare: `[]{ struct S { char c() { return 'a'; } }; return 1; }`
			// returns `int` and the walk has to reach past the `'a'` standing
			// in front of it.
			continue;
		}
		const AstNode* const nested = returned_expression(statement);
		if (nested != nullptr)
		{
			return nested;
		}
	}
	return nullptr;
}

}  // namespace

LambdaReading::LambdaReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

AstNode& LambdaReading::make(AstKind kind)
{
	return *analyzer_.written_->make(kind);
}

// 5.1.2p2 and p3: the expression is a prvalue of the closure type, which 12.2p1
// makes an object the function it was written in gives storage to.
//
// p14 says what initializes that object: one initialization per capture, from
// the entity each capture names.  A lambda that captured nothing therefore
// writes *no* initialization at all - not the trivial default construction an
// empty class would get, because p19 leaves the closure type with no default
// constructor to call.  The object stands in its storage and the expression is
// worth it, which is what leaves the temporary here with no constructor-action
// under it where every other prvalue of class type has one.
AnalyzedValue LambdaReading::expression(const AstNode& node,
                                        const SemaContext& ctx,
                                        DumpNode& parent)
{
	SemaEntity& closure = closure_class(node, ctx);
	DumpNode& line = analyzer_.model_.open_node(parent, std::string());
	// 12.2p1: one lambda-expression standing in one region is one object, for
	// the same reason it is one class.  13.3 reads an argument before the
	// initialization that takes it does, and every other prvalue is worth an
	// object made where the node that produced it is first asked for one - so
	// making one per reading here would give the function storage for an object
	// no later reading names, and 12.2p3 an end of a lifetime to write for it.
	const Scope& region = reading_region(*ctx.scope);
	SemaEntity* held = analyzer_.model_.closure_object_of(region, node.begin);
	if (held == nullptr)
	{
		held = &analyzer_.model_.create(SemaKind::Variable, std::string(),
		                                closure.type);
		analyzer_.model_.hold_closure_object(region, node.begin, *held);
	}
	SemaEntity& object = *held;
	object.object_member = false;
	analyzer_.set_fact(line, FactKind::TemporaryObject, closure.type,
	                   ValueCategory::PRValue);
	line.fact.entity = &object;
	line.fact.spelling = "tmpobj";
	// 12.2p3: the closure object is a temporary, so the full-expression it was
	// created in is what ends its lifetime - which for a closure holding no
	// captures is 12.4p8's vacuous destruction and no call.
	analyzer_.register_temporary(line, ctx.scope);
	line.text = analyzer_.spell("temporary-object", ValueCategory::PRValue,
	                            closure.type, std::string());
	AnalyzedValue value;
	value.type = closure.type;
	value.spelled = closure.type;
	value.category = ValueCategory::PRValue;
	value.what = "temporary-object";
	value.entity = &object;
	value.node = &line;
	return value;
}

// 5.1.2p3: the closure type of one lambda-expression, declared in the smallest
// scope around it.
//
// One lambda-expression makes one class however many times the reading of the
// body it stands in is made: 14.7.1p1 reads a template's body once per
// specialization, and each of those readings is a region of its own, so the
// class is held under the region it was declared in and the position the
// expression was written at.  Two readings of one body in one region - a
// declarator read where 8.5.1p11 probes what a clause initializes, and again
// where it initializes it - therefore name one class, and the member function
// whose body the class reading reads at its closing brace is read once.
SemaEntity& LambdaReading::closure_class(const AstNode& node,
                                         const SemaContext& ctx)
{
	const Scope& region = reading_region(*ctx.scope);
	SemaEntity* const held = analyzer_.model_.closure_of(region, node.begin);
	if (held != nullptr)
	{
		return *held;
	}
	const AstNode* const introducer =
		child_kind(node, AstKind::LambdaIntroducer);
	if (introducer != nullptr && !captures_nothing(*introducer))
	{
		throw std::runtime_error("a lambda-expression writes the capture " +
		                         introducer->text +
		                         ", whose members this milestone's closure "
		                         "class does not hold");
	}
	// 5.1.2p3 leaves the class unnamed, which is what the class reading's own
	// convention for a class no declarator names is written for: a class in a
	// function is numbered among the ones the unit defines there, and one at
	// namespace scope is named after the terminals it was written from.
	AstNode& specifier = make(AstKind::ClassSpecifier);
	specifier.begin = node.begin;
	specifier.end = node.end;
	specifier.completed = node.end;
	specifier.add(&call_operator(node));
	SemaSpan span;
	span.begin = node.begin;
	span.end = node.end;
	SemaEntity& closure =
		analyzer_.class_declaration(specifier, ctx, span, true, std::string());
	closure.closure_class = true;
	analyzer_.model_.hold_closure(region, node.begin, closure);
	return closure;
}

// 5.1.2p5: the public inline function call operator of the closure class,
// whose parameter-declaration-clause and exception-specification are the
// lambda-declarator's own and which is `const` unless the declarator wrote
// `mutable`.
AstNode& LambdaReading::call_operator(const AstNode& node)
{
	const AstNode* const written = child_kind(node, AstKind::LambdaDeclarator);
	const AstNode* const body = child_kind(node, AstKind::CompoundStatement);
	if (body == nullptr)
	{
		throw std::runtime_error("a lambda-expression writes no body");
	}
	AstNode& definition = make(AstKind::FunctionDefinition);
	definition.begin = node.begin;
	definition.end = node.end;
	AstNode& declarator = make(AstKind::Declarator);
	AstNode& id = make(AstKind::Identifier);
	id.text = "operator()";
	declarator.add(&id);
	const AstNode* const parameters =
		written == nullptr ? nullptr
		                   : child_kind(*written, AstKind::ParameterClause);
	declarator.add(parameters != nullptr ? borrowed(parameters)
	                                     : &make(AstKind::ParameterClause));
	if (written == nullptr ||
	    child_kind(*written, AstKind::LambdaSpecifier) == nullptr)
	{
		// 5.1.2p5: `mutable` is what leaves the operator without the `const`
		// every other closure's carries.
		AstNode& qualifier = make(AstKind::CvQualifier);
		qualifier.token = KW_CONST;
		qualifier.text = "const";
		declarator.add(&qualifier);
	}
	const AstNode* const specification =
		written == nullptr
			? nullptr
			: child_kind(*written, AstKind::NoexceptSpecification);
	if (specification != nullptr)
	{
		// 15.4p1: the declarator's own exception-specification, which the
		// lambda-declarator spells as a part of its own and a function
		// declarator spells as one of its suffixes.
		AstNode& qualifier = make(AstKind::FunctionQualifier);
		qualifier.text = "noexcept";
		if (!specification->children.empty())
		{
			qualifier.add(borrowed(specification->children[0]));
		}
		declarator.add(&qualifier);
	}
	write_return_type(definition, declarator, *body, written);
	definition.add(&declarator);
	definition.add(borrowed(body));
	return definition;
}

// 5.1.2p4: what the call operator returns - the trailing-return-type where the
// declarator wrote one, `void` where the body returns no expression, and the
// type of the expression it does return otherwise.
void LambdaReading::write_return_type(AstNode& definition, AstNode& declarator,
                                      const AstNode& body,
                                      const AstNode* written)
{
	const AstNode* const trailing =
		written == nullptr ? nullptr
		                   : child_kind(*written, AstKind::TrailingReturnType);
	const AstNode* const returned =
		trailing != nullptr ? nullptr : returned_expression(body);
	AstNode& specifiers = make(AstKind::DeclSpecifierSeq);
	AstNode& specifier = make(AstKind::DeclSpecifier);
	if (trailing == nullptr && returned == nullptr)
	{
		specifier.token = KW_VOID;
		specifier.text = "void";
		specifiers.add(&specifier);
		definition.add(&specifiers);
		return;
	}
	// 7.1.6.4p1 with 8.3.5p2: what the declarator writes after its
	// declarator-id is the type, and `auto` is the placeholder standing where
	// the decl-specifier-seq would say one.
	specifier.token = KW_AUTO;
	specifier.text = "auto";
	specifiers.add(&specifier);
	definition.add(&specifiers);
	if (trailing != nullptr)
	{
		declarator.add(borrowed(trailing));
		return;
	}
	AstNode& deduced = make(AstKind::TrailingReturnType);
	AstNode& type = make(AstKind::TypeId);
	AstNode& sequence = make(AstKind::TypeSpecifierSeq);
	AstNode& asked = make(AstKind::DeducedReturnType);
	asked.add(borrowed(returned));
	sequence.add(&asked);
	type.add(&sequence);
	deduced.add(&type);
	declarator.add(&deduced);
}

// 5.1.2p4 at the reading that asks it: the type of the returned expression
// after 4.1's lvalue-to-rvalue, 4.2's array-to-pointer and 4.3's
// function-to-pointer conversion - which is the type the value has and not the
// one 7.1.6.2p4 would give the same words, an lvalue there naming a reference.
//
// 5p8 leaves this reading unevaluated: it says what the operator returns, and
// the body that returns it is read once, below, where the program's own
// storage and lifetimes are written.
TypeId LambdaReading::deduced_return_type(const AstNode& node,
                                          const SemaContext& ctx)
{
	const ReadingDepth measuring(analyzer_.unevaluated_);
	DumpNode scratch;
	const AnalyzedValue value =
		analyzer_.probe_expression(*node.children[0], ctx, scratch);
	analyzer_.require_complete_value(value);
	return analyzer_.decayed(value);
}
