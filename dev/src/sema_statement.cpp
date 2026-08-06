#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"

// The PA12 statement walk.
//
// A statement is read where it is written, against the declarations already
// made, and writes its node under the statement that holds it.  The region a
// statement opens and the node it writes to are separate arguments because
// 3.3.3 and the output format disagree about them: an unbraced substatement of
// a selection or iteration statement is a region of its own that the dump still
// writes directly under the statement.

namespace
{

// True for the declaration forms that are a `simple-declaration` of 7p1, which
// the dump wraps in a node of that name.  An alias-declaration, a
// using-declaration and a using-directive are block-declarations of their own
// and write what they declare directly.
bool is_simple_declaration(AstKind kind)
{
	return kind == AstKind::SimpleDeclaration ||
		kind == AstKind::ClassSpecifier ||
		kind == AstKind::ClassForwardDeclaration ||
		kind == AstKind::EnumSpecifier;
}

bool is_declaration(AstKind kind)
{
	return is_simple_declaration(kind) || kind == AstKind::AliasDeclaration ||
		kind == AstKind::UsingDeclaration || kind == AstKind::UsingDirective ||
		kind == AstKind::NamespaceAliasDefinition ||
		kind == AstKind::StaticAssertDeclaration;
}

}

// 6.4p1 and 6.5p2: a substatement that is not a compound-statement is in a
// region of its own, so two sibling substatements may declare the same name.
SemaAnalyzer::Context SemaAnalyzer::substatement_scope(const Context& ctx)
{
	Context inner = ctx;
	inner.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	return inner;
}

void SemaAnalyzer::semantic_statement(const AstNode& node, const Context& ctx,
                                      DumpNode& parent)
{
	switch (node.kind)
	{
	case AstKind::CompoundStatement:
		block_statement(node, ctx, parent);
		return;

	case AstKind::ExpressionStatement:
	{
		DumpNode& line = model_.open_node(parent, "expression-statement");
		if (!node.children.empty())
		{
			// 6.2p1: the value is discarded, which is still no target for
			// 13.4 to resolve an overloaded name against.
			require_complete_value(expression(*node.children[0], ctx, line));
		}
		return;
	}

	case AstKind::ReturnStatement:
		return_statement(node, ctx, parent);
		return;

	case AstKind::IfStatement:
		selection_statement(node, ctx, parent, "if-statement");
		return;

	case AstKind::SwitchStatement:
		++switches_;
		++breakable_;
		selection_statement(node, ctx, parent, "switch-statement");
		--breakable_;
		--switches_;
		return;

	case AstKind::WhileStatement:
		loop_statement(node, ctx, parent, "while-statement");
		return;

	case AstKind::DoStatement:
		loop_statement(node, ctx, parent, "do-statement");
		return;

	case AstKind::ForStatement:
		for_statement(node, ctx, parent);
		return;

	case AstKind::CaseStatement:
		case_statement(node, ctx, parent, false);
		return;

	case AstKind::DefaultStatement:
		case_statement(node, ctx, parent, true);
		return;

	case AstKind::BreakStatement:
		// 6.6.1p1: a break statement shall occur only in an iteration statement
		// or a switch statement.
		if (breakable_ == 0)
		{
			throw std::runtime_error("a break statement is outside every loop "
			                         "and switch statement");
		}
		model_.open_node(parent, "break-statement");
		return;

	case AstKind::ContinueStatement:
		// 6.6.2p1: a continue statement shall occur only in an iteration
		// statement.
		if (continuable_ == 0)
		{
			throw std::runtime_error("a continue statement is outside every loop");
		}
		model_.open_node(parent, "continue-statement");
		return;

	default:
		break;
	}

	if (!is_declaration(node.kind))
	{
		// A labeled statement, a goto, a throw and a try block are outside the
		// PA12 statement subset.
		throw std::runtime_error("a statement is outside the PA12 subset");
	}

	Context inner = ctx;
	if (is_simple_declaration(node.kind))
	{
		inner.node = &model_.open_node(parent, "simple-declaration");
	}
	else
	{
		inner.node = &parent;
	}
	declaration(node, inner);
}

void SemaAnalyzer::block_statement(const AstNode& node, const Context& ctx,
                                   DumpNode& parent)
{
	DumpNode& line = model_.open_node(parent, "compound-statement");
	Context inner = ctx;
	inner.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		semantic_statement(*node.children[index], inner, line);
	}
}

void SemaAnalyzer::selection_statement(const AstNode& node, const Context& ctx,
                                       DumpNode& parent, const char* what)
{
	DumpNode& line = model_.open_node(parent, what);
	// 6.4p3: the name a condition declares is declared in a region that
	// encloses both substatements.
	Context held = ctx;
	held.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::Condition ||
		    child.kind == AstKind::ConditionDeclaration)
		{
			condition(child, held, line, node.kind == AstKind::SwitchStatement);
			continue;
		}
		if (child.kind == AstKind::Then || child.kind == AstKind::Else)
		{
			DumpNode& arm =
				model_.open_node(line, child.kind == AstKind::Then ? "then" : "else");
			const Context inner = substatement_scope(held);
			for (std::size_t at = 0; at < child.children.size(); ++at)
			{
				semantic_statement(*child.children[at], inner, arm);
			}
			continue;
		}
		semantic_statement(child, substatement_scope(held), line);
	}
}

void SemaAnalyzer::loop_statement(const AstNode& node, const Context& ctx,
                                  DumpNode& parent, const char* what)
{
	DumpNode& line = model_.open_node(parent, what);
	Context held = ctx;
	held.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	++breakable_;
	++continuable_;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::Condition ||
		    child.kind == AstKind::ConditionDeclaration)
		{
			condition(child, held, line, false);
			continue;
		}
		semantic_statement(child, substatement_scope(held), line);
	}
	--continuable_;
	--breakable_;
}

void SemaAnalyzer::for_statement(const AstNode& node, const Context& ctx,
                                 DumpNode& parent)
{
	DumpNode& line = model_.open_node(parent, "for-statement");
	// 6.5.3p1: the for-init-statement and the condition are in the region the
	// statement itself opens, which its substatement is enclosed by.
	Context held = ctx;
	held.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	++breakable_;
	++continuable_;
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::ForInitStatement)
		{
			DumpNode& init = model_.open_node(line, "for-init-statement");
			for (std::size_t at = 0; at < child.children.size(); ++at)
			{
				semantic_statement(*child.children[at], held, init);
			}
			continue;
		}
		if (child.kind == AstKind::Condition ||
		    child.kind == AstKind::ConditionDeclaration)
		{
			condition(child, held, line, false);
			continue;
		}
		if (child.kind == AstKind::Iteration)
		{
			DumpNode& step = model_.open_node(line, "iteration");
			for (std::size_t at = 0; at < child.children.size(); ++at)
			{
				require_complete_value(
					expression(*child.children[at], held, step));
			}
			continue;
		}
		semantic_statement(child, substatement_scope(held), line);
	}
	--continuable_;
	--breakable_;
}

void SemaAnalyzer::condition(const AstNode& node, const Context& ctx,
                             DumpNode& parent, bool integral)
{
	DumpNode& line = model_.open_node(parent, "condition");
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::ConditionDeclaration)
		{
			// 6.4p3: the declaration declares its name in the region the
			// statement opened, and its value is the condition.
			Context inner = ctx;
			inner.node = &model_.open_node(line, "condition-declaration");
			condition_declaration(child, inner);
			continue;
		}
		const Value value = expression(child, ctx, line);
		require_complete_value(value);
		// 6.4p4 and 6.4.2p2: an `if` or loop condition is contextually
		// converted to bool, and a switch condition to an integral or
		// enumeration type.
		const bool ok = integral
			? types_.is_integral(value.type)
			: types_.contextually_bool(value.type);
		if (!ok)
		{
			throw std::runtime_error("a condition has no conversion to the type "
			                         "its statement needs");
		}
	}
}

void SemaAnalyzer::case_statement(const AstNode& node, const Context& ctx,
                                  DumpNode& parent, bool is_default)
{
	// 6.4.2p1: a case or default label shall occur only in a switch statement.
	if (switches_ == 0)
	{
		throw std::runtime_error("a case or default label is outside every "
		                         "switch statement");
	}
	DumpNode& line =
		model_.open_node(parent, is_default ? "default-statement" : "case-statement");
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (!is_default && index == 0)
		{
			// 6.4.2p2: the constant-expression of a case label shall be a
			// converted constant expression of the switch condition's type.
			const Constant label = evaluate(child, ctx);
			model_.open_node(line, spell("literal", ValueCategory::PRValue,
			                             label.type, nullptr) + " " +
			                 spell_value(label.type, label.bits));
			continue;
		}
		semantic_statement(child, ctx, line);
	}
}

void SemaAnalyzer::return_statement(const AstNode& node, const Context& ctx,
                                    DumpNode& parent)
{
	DumpNode& line = model_.open_node(parent, "return-statement");
	if (node.children.empty())
	{
		return;
	}
	// 6.6.3p2: a return statement with an expression can be used only in a
	// function returning a value.
	if (types_.is_void(returns_))
	{
		throw std::runtime_error("a value is returned from a function that "
		                         "returns void");
	}
	initialize(*node.children[0], returns_, ctx, line);
}
