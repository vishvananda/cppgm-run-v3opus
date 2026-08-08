#include "sema_analyzer.h"

#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"

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
		DumpNode& line = open_fact(parent, "expression-statement",
		                           FactKind::ExpressionStatement);
		if (!node.children.empty())
		{
			// 1.9p10 and 12.2p3: the expression a statement writes is a
			// full-expression, so the temporaries it creates are destroyed
			// where it ends.
			open_full_expression();
			// 6.2p1: the value is discarded, which is still no target for
			// 13.4 to resolve an overloaded name against.
			const Value value =
				expression(*node.children[0], ctx, line);
			require_complete_value(value);
			// 5p11 and 12.2p1: a prvalue of class type whose value is thrown
			// away is still an object, standing in storage of the function's -
			// so the full-expression that made it is what ends it.  A cast to
			// void is how a program says it is throwing one away, and the
			// object is the operand under it.
			register_discarded_object(value, line, ctx);
			close_full_expression(line);
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
		// 6.6.1p1: a break leaves every block opened inside the switch.
		breakable_frames_.push_back(lifetimes_.size());
		selection_statement(node, ctx, parent, "switch-statement");
		breakable_frames_.pop_back();
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

	case AstKind::LabeledStatement:
	{
		// 6.1p1: a label is in the scope of the whole function it is written
		// in, and it labels the statement written after it.
		DumpNode& line = open_fact(parent, "labeled-statement " + node.text,
		                           FactKind::Label);
		line.fact.spelling = node.text;
		labels_.insert(node.text);
		for (std::size_t index = 0; index < node.children.size(); ++index)
		{
			semantic_statement(*node.children[index], ctx, line);
		}
		return;
	}

	case AstKind::GotoStatement:
	{
		// 6.6.4p1: the goto transfers control to the labeled statement of the
		// name it writes, which 6.1p1 lets stand anywhere in the function.
		DumpNode& line = open_fact(parent, "goto-statement " + node.text,
		                           FactKind::Goto);
		line.fact.spelling = node.text;
		gotos_.push_back(node.text);
		if (lifetimes_pending())
		{
			// 6.6.4p2 and 3.8p1: a goto that leaves a block destroys the
			// objects that block declared, and which blocks it leaves is what
			// the label says - a label this walk may not have reached yet.
			// The jump is refused rather than written as one that ends no
			// lifetime, which is not the program the source wrote.
			throw std::runtime_error("a goto statement is written where an "
			                         "object with a destructor is alive, which "
			                         "this milestone does not end the lifetime "
			                         "of");
		}
		return;
	}

	case AstKind::BreakStatement:
	{
		// 6.6.1p1: a break statement shall occur only in an iteration statement
		// or a switch statement.
		if (breakable_ == 0)
		{
			throw std::runtime_error("a break statement is outside every loop "
			                         "and switch statement");
		}
		DumpNode& line = open_fact(parent, "break-statement", FactKind::Break);
		// 6.6.1p1 and 3.8p1: control passes to the statement after the one the
		// break leaves, so every block between the two is left and the objects
		// they declared are destroyed here.
		leave_lifetimes(breakable_frames_.back(), line);
		return;
	}

	case AstKind::ContinueStatement:
	{
		// 6.6.2p1: a continue statement shall occur only in an iteration
		// statement.
		if (continuable_ == 0)
		{
			throw std::runtime_error("a continue statement is outside every loop");
		}
		DumpNode& line =
			open_fact(parent, "continue-statement", FactKind::Continue);
		// 6.6.2p1: control passes to the loop-continuation portion, which is
		// inside the loop, so the blocks left are the ones the body opened.
		leave_lifetimes(continuable_frames_.back(), line);
		return;
	}

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
		inner.node = &open_fact(parent, "simple-declaration",
		                        FactKind::SimpleDeclaration);
	}
	else
	{
		inner.node = &parent;
	}
	// 1.9p10 and 12.2p3: the initializer of a declaration is a full-expression,
	// so a temporary it created is destroyed after the object it initialized
	// has been - which is where the declaration's own line ends.
	open_full_expression();
	declaration(node, inner);
	close_full_expression(*inner.node);
}

void SemaAnalyzer::block_statement(const AstNode& node, const Context& ctx,
                                   DumpNode& parent)
{
	DumpNode& line = open_fact(parent, "compound-statement", FactKind::Compound);
	Context inner = ctx;
	inner.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	// 6.7p2 and 3.8p1: the objects this block declares are destroyed where
	// control leaves it, so the block is what holds them while it is read.
	open_lifetimes();
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		semantic_statement(*node.children[index], inner, line);
	}
	close_lifetimes(line);
}

void SemaAnalyzer::selection_statement(const AstNode& node, const Context& ctx,
                                       DumpNode& parent, const char* what)
{
	DumpNode& line = open_fact(parent, what,
	                           node.kind == AstKind::SwitchStatement
	                               ? FactKind::Switch
	                               : FactKind::If);
	// 6.4p3: the name a condition declares is declared in a region that
	// encloses both substatements.
	Context held = ctx;
	held.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	// 6.4p3 and 3.8p1: that region is the one an object the condition declares
	// belongs to, so its lifetime ends where the statement does and on every
	// path that leaves the statement - not where the block around the statement
	// ends, which a path that never reached the declaration also reaches.
	open_lifetimes();
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
			const bool is_then = child.kind == AstKind::Then;
			DumpNode& arm = open_fact(line, is_then ? "then" : "else",
			                          is_then ? FactKind::Then : FactKind::Else);
			const Context inner = substatement_scope(held);
			for (std::size_t at = 0; at < child.children.size(); ++at)
			{
				semantic_statement(*child.children[at], inner, arm);
			}
			continue;
		}
		semantic_statement(child, substatement_scope(held), line);
	}
	close_lifetimes(line);
}

void SemaAnalyzer::loop_statement(const AstNode& node, const Context& ctx,
                                  DumpNode& parent, const char* what)
{
	DumpNode& line = open_fact(parent, what,
	                           node.kind == AstKind::DoStatement
	                               ? FactKind::Do
	                               : FactKind::While);
	Context held = ctx;
	held.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	++breakable_;
	++continuable_;
	// 6.6.1p1 and 6.6.2p1: both jumps leave every block opened inside the loop.
	// 6.5.2p1 makes the condition's own declaration one the loop re-runs, so it
	// is not a block either of them leaves.
	breakable_frames_.push_back(lifetimes_.size());
	continuable_frames_.push_back(lifetimes_.size());
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
	continuable_frames_.pop_back();
	breakable_frames_.pop_back();
	--continuable_;
	--breakable_;
}

void SemaAnalyzer::for_statement(const AstNode& node, const Context& ctx,
                                 DumpNode& parent)
{
	DumpNode& line = open_fact(parent, "for-statement", FactKind::For);
	// 6.5.3p1: the for-init-statement and the condition are in the region the
	// statement itself opens, which its substatement is enclosed by.
	Context held = ctx;
	held.scope = &model_.open(ScopeKind::Block, *ctx.scope, nullptr, ctx.dump);
	// 6.5.3p1 and 3.8p1: an object the for-init-statement declares belongs to
	// the region the for statement itself opens, so its lifetime ends where the
	// loop does rather than where the block around the loop does.
	open_lifetimes();
	++breakable_;
	++continuable_;
	// 6.6.1p1 and 6.6.2p1: neither jump leaves the for statement's own region -
	// a break lands where that region is closed and a continue stays inside it.
	breakable_frames_.push_back(lifetimes_.size());
	continuable_frames_.push_back(lifetimes_.size());
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::ForInitStatement)
		{
			DumpNode& init = open_fact(line, "for-init-statement",
			                           FactKind::ForInit);
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
			DumpNode& step = open_fact(line, "iteration", FactKind::Iteration);
			// 1.9p10: the expression of the loop-continuation portion is a
			// full-expression of its own, run once per iteration.
			open_full_expression();
			for (std::size_t at = 0; at < child.children.size(); ++at)
			{
				require_complete_value(
					expression(*child.children[at], held, step));
			}
			close_full_expression(step);
			continue;
		}
		semantic_statement(child, substatement_scope(held), line);
	}
	continuable_frames_.pop_back();
	breakable_frames_.pop_back();
	--continuable_;
	--breakable_;
	close_lifetimes(line);
}

void SemaAnalyzer::condition(const AstNode& node, const Context& ctx,
                             DumpNode& parent, bool integral)
{
	DumpNode& line = open_fact(parent, "condition", FactKind::Condition);
	// 1.9p10 and 6.4p1: the condition is a full-expression, so the temporaries
	// it created are destroyed before the substatement it selects runs.  6.4p3's
	// declaration is not one of them: the object it declares belongs to the
	// region the statement opened and outlives every path out of the condition.
	open_full_expression();
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (child.kind == AstKind::ConditionDeclaration)
		{
			// 6.4p3: the declaration declares its name in the region the
			// statement opened, and its value is the condition.
			Context inner = ctx;
			DumpNode& declaration = open_fact(line, "condition-declaration",
			                                  FactKind::ConditionDeclaration);
			inner.node = &declaration;
			condition_declaration(child, inner);
			// 6.4p4 and 4p3: the condition is the value of the object the
			// declaration declared, which for an object of class type is what a
			// conversion function of that class hands back.  The declaration
			// stands first and the value of it after, so the statement reads one
			// and then the other.
			condition_of_declaration(declaration, ctx, integral);
			continue;
		}
		Value value = expression(child, ctx, line);
		require_complete_value(value);
		// 4p3 and 6.4.2p2: a condition of class type is what a conversion
		// function of its class hands back - one an `explicit` declaration may
		// answer where a bool is what the statement needs, and one it may not
		// where the statement selects on an integral value.
		if (integral)
		{
			contextual_integral(value, ctx);
		}
		else
		{
			contextual_bool(value, ctx);
		}
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
	close_full_expression(line);
}

// 6.4p4: the value of a condition that declared a name, where that name is of
// class type and the statement needs a value of a built-in one.  4p3's
// conversion is written under the condition-declaration, after the declaration
// it converts, so the reader of the condition runs the declaration and then the
// value the statement branches on.
void SemaAnalyzer::condition_of_declaration(DumpNode& declaration,
                                            const Context& ctx, bool integral)
{
	if (!lowering() || declaration.children.empty())
	{
		return;
	}
	SemaEntity* const declared = declaration.children[0]->fact.entity;
	if (declared == nullptr ||
	    !types_.is_class(types_.strip_cv(declared->type)))
	{
		return;
	}
	Value named;
	named.type = named.spelled = declared->type;
	named.category = ValueCategory::LValue;
	named.entity = declared;
	named.what = "id-expression";
	named.payload = std::string(ast_token_type_name(TT_IDENTIFIER)) + ":" +
		declared->name;
	const std::size_t written = declaration.children.size();
	named.node = &model_.open_node(
		declaration,
		spell(named.what, named.category, named.type, named.payload));
	record(named);
	if (integral)
	{
		contextual_integral(named, ctx);
	}
	else
	{
		contextual_bool(named, ctx);
	}
	if (types_.is_class(types_.strip_cv(named.type)))
	{
		// No conversion answered, so the name written here says nothing the
		// object's own storage does not, and the caller's check is what refuses
		// the condition.
		declaration.children.resize(written);
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
	DumpNode& line = open_fact(parent,
	                           is_default ? "default-statement"
	                                     : "case-statement",
	                           is_default ? FactKind::Default : FactKind::Case);
	for (std::size_t index = 0; index < node.children.size(); ++index)
	{
		const AstNode& child = *node.children[index];
		if (!is_default && index == 0)
		{
			// 6.4.2p2: the constant-expression of a case label shall be a
			// converted constant expression of the switch condition's type.
			const Constant label = evaluate(child, ctx);
			DumpNode& written =
				open_fact(line, spell("literal", ValueCategory::PRValue,
				                      label.type,
				                      spell_value(label.type, label.bits)),
				          FactKind::Literal);
			written.fact.type = label.type;
			written.fact.spelled = label.type;
			written.fact.constant = true;
			written.fact.value = label.bits;
			continue;
		}
		semantic_statement(child, ctx, line);
	}
}

void SemaAnalyzer::return_statement(const AstNode& node, const Context& ctx,
                                    DumpNode& parent)
{
	DumpNode& line = open_fact(parent, "return-statement", FactKind::Return);
	line.fact.type = returns_;
	// 1.9p10 and 6.6.3p2: the operand of a return is a full-expression, and its
	// temporaries are destroyed once the returned object has been created from
	// them - before the blocks the return leaves end their own objects.
	open_full_expression();
	if (!node.children.empty())
	{
		if (types_.is_void(returns_))
		{
			// 6.6.3p3: a function returning `void` may still return an
			// expression, as long as that expression is itself of type `void`.
			const Value value = expression(*node.children[0], ctx, line);
			if (!types_.is_void(value.type))
			{
				throw std::runtime_error("a value is returned from a function "
				                         "that returns void");
			}
		}
		else
		{
			// 6.6.3p2 and 12.2p1: the returned prvalue is the object the caller
			// reads, so a temporary the conversion makes for it is storage the
			// return asked for rather than the expression.
			initialize(*node.children[0], returns_, ctx, line, false,
			           Requested::Returned);
		}
	}
	close_full_expression(line);
	// 6.6p2: a return leaves every block between it and the function, and
	// 3.8p1 ends the lifetime of every object those blocks declared - after the
	// returned value has been read, which is why the actions stand under the
	// return rather than before it.
	unwind_lifetimes(line);
}
