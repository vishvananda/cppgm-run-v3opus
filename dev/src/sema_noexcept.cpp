#include "sema_constexpr.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_scope.h"
#include "token_model.h"

// 5.3.7: the `noexcept` operator.
//
// The operator asks one question about an expression the program does not
// evaluate: could evaluating it throw.  15.4 has already answered that of every
// function - `SemaEntity::nonthrowing` is what a declarator's
// exception-specification and 15.4p14's implicit one both settle - so what is
// left here is which functions the operand would call, and that is a question
// only the expression layer can answer: 13.3 chooses between overloads,
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

// 5.3.7p3's second bullet.  A throw-expression is one the expression layer of
// this milestone reads none of, so asking it of the resolved tree would ask it
// of a tree the operand never left; the syntax is where the answer stands, and
// 5.3.7p3 makes it the whole answer wherever it is yes.
bool ConstexprReading::holds_throw(const AstNode& node)
{
	if (node.kind == AstKind::ThrowStatement)
	{
		return true;
	}
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		if (node.children[index] != nullptr && holds_throw(*node.children[index]))
		{
			return true;
		}
	}
	return false;
}

// 5.3.7p3 over the resolved tree.  One walk, post-order, with no lookup and no
// ranking repeated: every choice the answer depends on was made while the
// operand was read.
bool ConstexprReading::nonthrowing_tree(const DumpNode& node) const
{
	const SemaFact& fact = node.fact;
	switch (fact.kind)
	{
	case FactKind::Call:
	{
		// 5.3.7p3's first bullet, and footnote 80's implicit calls with it: a
		// constructor 8.5 chose, a conversion function 13.3.3.1.2 chose and an
		// operator function 13.5 chose each leave a `call-expression` of their
		// own, so they are this line and not a case beside it.
		const SemaEntity* const declaration = called_declaration(node);
		if (declaration == nullptr || !declaration->nonthrowing)
		{
			return false;
		}
		break;
	}

	case FactKind::NewExpression:
	case FactKind::DeleteExpression:
		// Footnote 80: the allocation function 3.7.4.1 gave the storage and the
		// deallocation function 5.3.5p9 chose are calls the expression makes
		// without writing one, and the node carries the declaration each
		// reached.  What either expression *initializes* or destroys is written
		// under it and reaches the arm above on its own.
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
	if (holds_throw(node))
	{
		return false;
	}
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
