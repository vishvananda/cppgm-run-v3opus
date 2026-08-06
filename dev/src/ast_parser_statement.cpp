#include "ast_parser.h"

// Statements, and the block items that are either a declaration or one.

AstNode* AstParser::parse_block_item()
{
	const Mark start = mark();
	AstNode* declaration = parse_declaration(false);
	if (declaration != nullptr)
	{
		return declaration;
	}
	reset(start);
	return parse_statement();
}

AstNode* AstParser::parse_compound_statement()
{
	const Mark start = mark();
	if (!at(OP_LBRACE))
	{
		return nullptr;
	}
	AstNode* node = make(AstKind::CompoundStatement);
	{
		BracketGuard brackets(*this, false);
		ScopeGuard scope(names_);
		++pos_;
		while (!at(OP_RBRACE) && !at(ST_EOF))
		{
			AstNode* item = parse_block_item();
			if (item == nullptr)
			{
				return fail(start);
			}
			node->add(item);
		}
	}
	if (!accept(OP_RBRACE))
	{
		return fail(start);
	}
	return node;
}

AstNode* AstParser::parse_statement()
{
	switch (peek())
	{
	case OP_LBRACE:
		return parse_compound_statement();
	case KW_IF:
	case KW_SWITCH:
		return parse_selection_statement();
	case KW_WHILE:
	case KW_DO:
		return parse_iteration_statement();
	case KW_FOR:
		return parse_for_statement();
	case KW_BREAK:
	case KW_CONTINUE:
	case KW_GOTO:
	case KW_RETURN:
	case KW_THROW:
		return parse_jump_statement();
	case KW_TRY:
		return parse_try_block();
	case KW_CASE:
	case KW_DEFAULT:
		return parse_labeled_statement();
	default:
		break;
	}
	if (at(TT_IDENTIFIER) && peek(1) == OP_COLON)
	{
		return parse_labeled_statement();
	}
	const Mark start = mark();
	AstNode* node = make(AstKind::ExpressionStatement);
	if (!at(OP_SEMICOLON))
	{
		AstNode* expression = parse_expression();
		if (expression == nullptr)
		{
			return fail(start);
		}
		node->add(expression);
	}
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return node;
}

AstNode* AstParser::parse_labeled_statement()
{
	const Mark start = mark();
	AstNode* node = nullptr;
	if (accept(KW_CASE))
	{
		node = make(AstKind::CaseStatement);
		AstNode* value = parse_expression();
		if (value == nullptr)
		{
			return fail(start);
		}
		node->add(value);
	}
	else if (accept(KW_DEFAULT))
	{
		node = make(AstKind::DefaultStatement);
	}
	else
	{
		node = make_text(AstKind::LabeledStatement, spelling());
		++pos_;
	}
	if (!accept(OP_COLON))
	{
		return fail(start);
	}
	AstNode* body = parse_statement();
	if (body == nullptr)
	{
		return fail(start);
	}
	node->add(body);
	return node;
}

AstNode* AstParser::parse_selection_statement()
{
	const Mark start = mark();
	const bool is_if = at(KW_IF);
	++pos_;
	AstNode* condition = parse_parenthesized_condition();
	if (condition == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(is_if ? AstKind::IfStatement : AstKind::SwitchStatement);
	node->add(condition);
	AstNode* body = parse_statement();
	if (body == nullptr)
	{
		return fail(start);
	}
	if (!is_if)
	{
		node->add(body);
		return node;
	}
	AstNode* branch = make(AstKind::Then);
	branch->add(body);
	node->add(branch);
	if (accept(KW_ELSE))
	{
		AstNode* other = parse_statement();
		if (other == nullptr)
		{
			return fail(start);
		}
		AstNode* alternative = make(AstKind::Else);
		alternative->add(other);
		node->add(alternative);
	}
	return node;
}

AstNode* AstParser::parse_iteration_statement()
{
	const Mark start = mark();
	if (accept(KW_WHILE))
	{
		AstNode* condition = parse_parenthesized_condition();
		if (condition == nullptr)
		{
			return fail(start);
		}
		AstNode* node = make(AstKind::WhileStatement);
		node->add(condition);
		AstNode* body = parse_statement();
		if (body == nullptr)
		{
			return fail(start);
		}
		node->add(body);
		return node;
	}
	++pos_;
	AstNode* node = make(AstKind::DoStatement);
	AstNode* body = parse_statement();
	if (body == nullptr || !accept(KW_WHILE))
	{
		return fail(start);
	}
	node->add(body);
	AstNode* condition = parse_parenthesized_condition();
	if (condition == nullptr || !accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	node->add(condition);
	return node;
}

AstNode* AstParser::parse_parenthesized_condition()
{
	const Mark start = mark();
	if (!at(OP_LPAREN))
	{
		return nullptr;
	}
	AstNode* node = nullptr;
	{
		BracketGuard brackets(*this, false);
		++pos_;
		node = parse_condition();
		if (node == nullptr || !at(OP_RPAREN))
		{
			return fail(start);
		}
		++pos_;
	}
	return node;
}

AstNode* AstParser::parse_condition()
{
	const Mark start = mark();
	AstNode* specifiers = parse_specifier_seq(SpecifierMode::Decl);
	if (specifiers != nullptr)
	{
		AstNode* declarator = parse_declarator(DeclaratorForm::Named);
		if (declarator != nullptr)
		{
			AstNode* initializer = parse_initializer();
			if (initializer != nullptr && at(OP_RPAREN))
			{
				AstNode* declaration = make(AstKind::ConditionDeclaration);
				declaration->add(specifiers);
				declaration->add(declarator);
				declaration->add(initializer);
				declare_init_declarators(specifiers, declarator);
				AstNode* node = make(AstKind::Condition);
				node->add(declaration);
				return node;
			}
		}
	}
	reset(start);
	AstNode* expression = parse_expression();
	if (expression == nullptr)
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::Condition);
	node->add(expression);
	return node;
}

AstNode* AstParser::parse_for_statement()
{
	const Mark start = mark();
	++pos_;
	if (!at(OP_LPAREN))
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::ForStatement);
	ScopeGuard scope(names_);
	{
		BracketGuard brackets(*this, false);
		++pos_;
		AstNode* init = make(AstKind::ForInitStatement);
		if (!accept(OP_SEMICOLON))
		{
			AstNode* item = parse_block_item();
			if (item == nullptr)
			{
				return fail(start);
			}
			init->add(item);
		}
		node->add(init);
		if (!at(OP_SEMICOLON))
		{
			AstNode* condition = parse_condition();
			if (condition == nullptr)
			{
				return fail(start);
			}
			node->add(condition);
		}
		if (!accept(OP_SEMICOLON))
		{
			return fail(start);
		}
		if (!at(OP_RPAREN))
		{
			AstNode* step = parse_expression();
			if (step == nullptr)
			{
				return fail(start);
			}
			AstNode* iteration = make(AstKind::Iteration);
			iteration->add(step);
			node->add(iteration);
		}
		if (!at(OP_RPAREN))
		{
			return fail(start);
		}
		++pos_;
	}
	AstNode* body = parse_statement();
	if (body == nullptr)
	{
		return fail(start);
	}
	node->add(body);
	return node;
}

AstNode* AstParser::parse_jump_statement()
{
	const Mark start = mark();
	const unsigned keyword = peek();
	++pos_;
	if (keyword == KW_BREAK || keyword == KW_CONTINUE)
	{
		if (!accept(OP_SEMICOLON))
		{
			return fail(start);
		}
		return make(keyword == KW_BREAK
			? AstKind::BreakStatement
			: AstKind::ContinueStatement);
	}
	if (keyword == KW_GOTO)
	{
		if (!at(TT_IDENTIFIER))
		{
			return fail(start);
		}
		AstNode* node = make_text(AstKind::GotoStatement, spelling());
		++pos_;
		if (!accept(OP_SEMICOLON))
		{
			return fail(start);
		}
		return node;
	}
	AstNode* node = make(keyword == KW_RETURN
		? AstKind::ReturnStatement
		: AstKind::ThrowStatement);
	if (!at(OP_SEMICOLON))
	{
		AstNode* value = keyword == KW_RETURN
			? parse_expression()
			: parse_assignment_expression();
		if (value == nullptr)
		{
			return fail(start);
		}
		node->add(value);
	}
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return node;
}

AstNode* AstParser::parse_try_block()
{
	const Mark start = mark();
	++pos_;
	AstNode* body = parse_compound_statement();
	if (body == nullptr || !at(KW_CATCH))
	{
		return fail(start);
	}
	AstNode* node = make(AstKind::TryBlock);
	node->add(body);
	while (accept(KW_CATCH))
	{
		AstNode* handler = make(AstKind::Handler);
		if (!at(OP_LPAREN))
		{
			return fail(start);
		}
		ScopeGuard scope(names_);
		{
			BracketGuard brackets(*this, false);
			++pos_;
			AstNode* declaration = parse_exception_declaration();
			if (declaration == nullptr || !at(OP_RPAREN))
			{
				return fail(start);
			}
			handler->add(declaration);
			++pos_;
		}
		AstNode* caught = parse_compound_statement();
		if (caught == nullptr)
		{
			return fail(start);
		}
		handler->add(caught);
		node->add(handler);
	}
	return node;
}

AstNode* AstParser::parse_exception_declaration()
{
	const Mark start = mark();
	AstNode* node = make(AstKind::ExceptionDeclaration);
	if (accept(OP_DOTS))
	{
		node->add(make_text(AstKind::Ellipsis, "..."));
		return node;
	}
	AstNode* specifiers = parse_specifier_seq(SpecifierMode::Decl);
	if (specifiers == nullptr)
	{
		return fail(start);
	}
	node->add(specifiers);
	const Mark after_specifiers = mark();
	AstNode* declarator = parse_declarator(DeclaratorForm::Parameter);
	if (declarator == nullptr || !at(OP_RPAREN))
	{
		reset(after_specifiers);
		return node;
	}
	if (!declarator->children.empty())
	{
		node->add(declarator);
		const AstNode* id = declarator_identifier(declarator);
		if (id != nullptr)
		{
			names_.declare(id->text, NameKind::Value);
		}
	}
	return node;
}
