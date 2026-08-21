#include "sema_constexpr.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_scope.h"
#include "token_model.h"

// 15.4's exception-specification, and 5.3.7's operator over it.
//
// The two halves are one file because they are one fact read twice.  15.4p1's
// specification is a fact of a *declaration* - `SemaEntity::nonthrowing`, which
// a declarator writes here and 15.4p14's implicit walks in `sema_class.cpp`
// write for the members the standard defines.  What 15.4p1 leaves to be worked
// out is the condition, which is a constant expression and folded as one; 9.2p2
// makes an exception-specification a context in which a class is complete, so a
// condition a member wrote is folded where the class-specifier closes and not
// where the declarator stands.
//
// The operator asks one question about an expression the program does not
// evaluate: could evaluating it throw.  15.4 has already answered that of every
// function, so what is left here is which functions the operand would call, and
// that is a question only the expression layer can answer: 13.3 chooses
// between overloads,
// 8.3.6p1 fills a place the call stopped short of, 8.5 copies a by-value
// argument, and each of those is a call 5.3.7p3 counts.
//
// So the reading is not a walk of the syntax.  The operand is read exactly once
// through the same layer an evaluated expression goes through, into a scratch
// node nothing keeps - 5.3.7p1 makes it an unevaluated operand, so 12.2p3's
// temporaries it creates are dropped with that node and no lifetime of the
// program begins - and 5.3.7p3 is then asked of the resolved tree that reading
// leaves.

namespace
{

// 15.4p1: the exception-specification the declarator wrote, read once for the
// two answers every reader of one needs.  True is the forms that say the
// function throws nothing with nothing to evaluate - `throw` with an empty
// type-id-list, `noexcept` with no constant-expression, and the one spelling
// `noexcept(true)` - so the common form costs no fold at all.  Anything else
// leaves `condition` denoting the constant expression 15.4p1 makes the answer,
// and null where the declarator wrote no specification of the sort.
bool wrote_nonthrowing(const AstNode& declarator, const AstNode** condition)
{
	for (std::size_t index = 0; index < declarator.children.size(); ++index)
	{
		const AstNode& part = *declarator.children[index];
		if (part.kind == AstKind::NestedDeclarator &&
		    wrote_nonthrowing(part, condition))
		{
			return true;
		}
		if (part.kind != AstKind::FunctionQualifier)
		{
			continue;
		}
		if (part.text == "throw()")
		{
			return true;
		}
		if (part.text.compare(0, 8, "noexcept") != 0)
		{
			continue;
		}
		if (part.children.empty() || part.children[0] == nullptr)
		{
			return true;
		}
		const AstNode& written = *part.children[0];
		if (written.kind == AstKind::KeywordLiteral && written.token == KW_TRUE)
		{
			return true;
		}
		if (*condition == nullptr)
		{
			*condition = &written;
		}
	}
	return false;
}

// 5.3.7p3's first bullet over one `call-expression`: which declaration the call
// reached, or null where it reached none - which is a call through a pointer to
// function, and C++11 leaves an exception-specification no part of such a
// pointer's type, so nothing about that call says it throws nothing.
const SemaEntity* called_declaration(const DumpNode& call)
{
	for (std::size_t index = 0; index < call.children.size(); ++index)
	{
		if (call.children[index]->fact.kind == FactKind::Callee)
		{
			return call.children[index]->fact.entity;
		}
	}
	return nullptr;
}

}  // namespace

// 5.3.7p3 over the resolved tree.  One walk, post-order, with no lookup and no
// ranking repeated: every choice the answer depends on was made while the
// operand was read.
//
// The question is asked of the *lines that name a declaration* and not of the
// expression kinds that make one call: a written call, an operator function
// 13.5 chose, a constructor 8.5 chose, the allocation function 3.7.4.1 gave a
// new-expression its storage and the deallocation function 5.3.5p9 paired with
// a delete-expression each leave a `callee` line of their own, and the end of a
// lifetime 5.3.5p6 writes leaves a destructor-action holding its own.  So one
// arm answers all of them, and no expression kind is left reading a field its
// own writer never fills.
bool ConstexprReading::nonthrowing_tree(const DumpNode& node) const
{
	const SemaFact& fact = node.fact;
	switch (fact.kind)
	{
	case FactKind::Call:
		// 5.3.7p3's first bullet: a call-expression with no callee under it
		// reached no declaration, which is a call through a pointer to
		// function - and C++11 leaves an exception-specification no part of
		// such a pointer's type, so nothing about that call says it throws
		// nothing.  One that did reach a declaration is worth what the callee
		// line below says.
		if (called_declaration(node) == nullptr)
		{
			return false;
		}
		break;

	case FactKind::Callee:
	case FactKind::DestructorAction:
		// 15.4 asked of the declaration this line names, which is the one fact
		// 5.3.7p3 reads and the one place it is read.
		if (fact.entity == nullptr || !fact.entity->nonthrowing)
		{
			return false;
		}
		break;

	case FactKind::Cast:
		// 5.3.7p3's third bullet: a `dynamic_cast` that needs 5.2.7p8's
		// run-time check throws where the check fails, and the reference
		// answers no for the pointer form as well as the reference one.
		if (fact.op == KW_DYNAMIC_CAST)
		{
			return false;
		}
		if (fact.op == OP_COMPL)
		{
			// 5.2.4p2's pseudo-destructor call, whose one child is the
			// postfix-expression 5.2.4p1 leaves as its only effect.  The
			// reference stops here: `p->~T()` over an operand it answers no
			// for on its own - `*p`, `p + 1`, the call that produced it - is
			// yes at every writing of the call, `(*p).~T()` and `(p->~T(), 0)`
			// among them, so its walk reads the call and not what the call
			// evaluates.  The scalar has no destructor for the first bullet to
			// ask 15.4 about, which is the whole of what this line stands for.
			return true;
		}
		break;

	default:
		break;
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index] != nullptr &&
		    !nonthrowing_tree(*node.children[index]))
		{
			return false;
		}
	}
	return true;
}

bool ConstexprReading::nonthrowing_operand(const AstNode& node,
                                           const SemaContext& ctx)
{
	// 5.3.7p3's second bullet is not asked here: a throw-expression is no part
	// of the expression grammar this milestone parses - `noexcept(throw 1)` is
	// refused by the parse and a `throw` statement by the analysis - so an
	// operand holding one never reaches this reading at all.
	if (analyzer_.checking_ > 0 || analyzer_.dependent_reading(*ctx.scope))
	{
		// 14.6p8: which function a call in the pattern reaches is what an
		// argument list settles, so the specification 5.3.7p3 asks about is not
		// known where the pattern stands.  The reading stands an answer in its
		// place and looks up the names 3.4p1 answers where the definition was
		// written, exactly as the size of a dependent operand is stood in for.
		analyzer_.check_expression_names(node, ctx);
		++analyzer_.stood_in_;
		return true;
	}
	if (!analyzer_.lowering())
	{
		// The facts 5.3.7p3 reads off the resolved operand - which line is a
		// call, and which declaration that call reached - are recorded on the
		// tree only in the dialect a lowering reads, so the operator is
		// answered in that dialect and refused in the two earlier ones, whose
		// expression subsets it is no part of.
		throw NotConstant("a constant expression holds an operator PA11 does "
		                  "not evaluate");
	}
	// 5.3.7p1 leaves the operand unevaluated, which is the door 5.1.1p13's
	// third bullet stands in: an id-expression naming a non-static data member
	// with no object at all is one of the operand's names here exactly as it is
	// one of `sizeof`'s.
	const ReadingDepth measuring(analyzer_.unevaluated_);
	DumpNode scratch;
	analyzer_.probe_expression(node, ctx, scratch);
	return nonthrowing_tree(scratch);
}

// 5.3.7p2: a constant of type `bool` and an rvalue, which the dump writes as
// the boolean literal that value is - the line a reader of the resolved program
// then lowers as the constant it already is, with nothing of the operand under
// it because nothing of the operand is evaluated.
AnalyzedValue ConstexprReading::noexcept_value(const AstNode& node,
                                               const SemaContext& ctx,
                                               DumpNode& parent)
{
	if (node.token != KW_NOEXCEPT || node.children.empty() ||
	    node.children[0] == nullptr)
	{
		// 5.2.8's `typeid` is the other operator this node kind spells, and
		// this milestone gives it no meaning.
		throw std::runtime_error("an expression is outside the PA12 subset");
	}
	AnalyzedValue value;
	value.type = analyzer_.types_.fundamental(FT_BOOL);
	value.spelled = value.type;
	value.category = ValueCategory::PRValue;
	value.constant = true;
	value.value = nonthrowing_operand(*node.children[0], ctx) ? 1 : 0;
	value.what = "literal";
	value.payload = value.value != 0 ? "KW_TRUE:true" : "KW_FALSE:false";
	value.node = &analyzer_.model_.open_node(
		parent,
		analyzer_.spell(value.what, value.category, value.type, value.payload));
	return value;
}

// 15.4p1's own condition, where the declarator that wrote it stands in a class.
//
// 9.2p2 makes an exception-specification one of the contexts a class is
// regarded as complete in, so the condition may name a member the walk of the
// body has yet to reach - and the fold at the declarator answers about the
// class as it stood there, which for such a condition is no answer at all.
// What the declaration keeps until the closing brace is the condition itself,
// exactly as 8.3.6p9's default-argument keeps the expression it was written
// with; `settle_specifications` is where the class being complete makes it one.
//
// Only a condition the declarator fold left unanswered is kept: one that folded
// is already the answer 15.4p1 asks for, and no member declared below it can
// change what the names it read mean.
void ConstexprReading::defer_specification(SemaEntity& function,
                                           const AstNode& declarator,
                                           const SemaContext& ctx)
{
	if (function.nonthrowing || ctx.scope == nullptr ||
	    ctx.scope->kind != ScopeKind::Class || ctx.scope->owner == nullptr ||
	    !analyzer_.types_.is_incomplete(ctx.scope->owner->type))
	{
		return;
	}
	const AstNode* condition = nullptr;
	if (!wrote_nonthrowing(declarator, &condition) && condition != nullptr)
	{
		ctx.scope->deferred_specifications.push_back(
			std::make_pair(&function, condition));
	}
}

// 9.2p2: the class is complete here, so every condition kept above is folded
// against the members it declares.  The walk costs one fold per declaration
// that wrote a condition the declarator could not answer and nothing per
// declaration that wrote none, and the answers it settles are read by 15.4p14's
// implicit specifications below it and by every use of the class after it.
void ConstexprReading::settle_specifications(Scope& scope,
                                             const SemaContext& ctx)
{
	std::vector<std::pair<SemaEntity*, const AstNode*> > kept;
	kept.swap(scope.deferred_specifications);
	for (std::size_t index = 0; index < kept.size(); ++index)
	{
		SemaEntity& function = *kept[index].first;
		if (!function.nonthrowing)
		{
			function.nonthrowing = specification_holds(*kept[index].second, ctx);
		}
	}
}

// 15.4p1: the condition is *folded* and not matched against a spelling, so
// `noexcept(true && !false)`, `noexcept(1)` and `noexcept(is_nothrow<T>::value)`
// each say what they come to and not what they were written as.
//
// 14.6p8: a fold that stood a value in for one an argument list has yet to
// settle answers about no declaration this unit has, so it is not an answer
// about this one either.  Anything else the fold refuses leaves the
// specification unknown, which is the one 15.4p14 gives a function whose own
// the translation cannot see - and the count is restored so that nothing around
// this reading sees a value stood in for a question it never asked.
bool ConstexprReading::specification_holds(const AstNode& condition,
                                           const SemaContext& ctx)
{
	const unsigned stood = analyzer_.stood_in_;
	bool folded = false;
	try
	{
		folded = truth(analyzer_.evaluate(condition, ctx)) &&
			analyzer_.stood_in_ == stood;
	}
	catch (const std::exception&)
	{
		folded = false;
	}
	analyzer_.stood_in_ = stood;
	return folded;
}

// 15.4p1: whether the exception-specification written after the
// parameter-clause says the function throws nothing.  C++11 leaves it out of
// the function type, so it is read off the declarator once - condition and all
// - and held on the declaration 5.3.4p15 asks it of.
bool SemaAnalyzer::declarator_nonthrowing(const AstNode& declarator,
                                          const Context& ctx)
{
	const AstNode* condition = nullptr;
	if (wrote_nonthrowing(declarator, &condition))
	{
		return true;
	}
	return condition != nullptr &&
		ConstexprReading(*this).specification_holds(*condition, ctx);
}
