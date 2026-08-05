#include "recognizer.h"

namespace
{

// A memo entry for a rule that did not match.  A match stores the position it
// ended at, which is always a real index, so one reserved value is enough.
const std::uint32_t kMemoFailed = 0xFFFFFFFFu;

// How many memoized rules may be on the stack at once.
//
// The grammar recurses once per bracket pair of the input, and the worst case
// measured is nested template argument lists at about six memoized rules per
// level.  Refusing past this turns a stack overflow into a `BAD`, and it is
// set where the descent still uses well under a third of a default stack:
// measured, nesting crashes near 12000 levels, so this stops at about 3300,
// which is more than ten times the nesting depth Annex B recommends
// supporting.
const std::size_t kMaxDepth = 20000;

// How many memo entries to keep before dropping them at the next top-level
// declaration boundary.
//
// A memo entry is a fact about a position, so forgetting one costs time and
// never correctness.  Once a top-level declaration has matched, nothing before
// its end is ever re-parsed, so the whole table is dead; dropping it there
// bounds the table by the largest single declaration rather than by the whole
// translation unit, and the threshold keeps the drop from costing anything on
// a file made of small declarations.
const std::size_t kMemoBudget = 1u << 18;

}

Recognizer::Recognizer(const std::vector<ParseToken>& tokens)
	: ParseCursor(tokens)
	, depth_(0)
{}

bool Recognizer::recognize()
{
	while (!at(ST_EOF))
	{
		if (!parse_declaration())
		{
			return false;
		}
		if (memo_.size() > kMemoBudget)
		{
			std::unordered_map<std::uint64_t, std::uint32_t> empty;
			memo_.swap(empty);
		}
	}
	return true;
}

bool Recognizer::memoize(MemoRule rule, Rule body)
{
	const std::uint64_t key =
		(static_cast<std::uint64_t>(pos_) * kMemoRuleCount + rule) * 2 +
		(angle_ ? 1 : 0);

	const std::unordered_map<std::uint64_t, std::uint32_t>::const_iterator found =
		memo_.find(key);
	if (found != memo_.end())
	{
		if (found->second == kMemoFailed)
		{
			return false;
		}
		pos_ = found->second;
		return true;
	}

	if (++depth_ > kMaxDepth)
	{
		--depth_;
		return false;
	}
	const bool matched = (this->*body)();
	--depth_;

	memo_[key] = matched ? static_cast<std::uint32_t>(pos_) : kMemoFailed;
	return matched;
}

// declaration* up to `closer`, which is `ST_EOF` for a translation unit and
// `OP_RBRACE` for a namespace body or a linkage specification.
bool Recognizer::parse_declaration_seq(unsigned closer)
{
	while (!at(closer))
	{
		if (!parse_declaration())
		{
			return false;
		}
	}
	return true;
}

bool Recognizer::parse_declaration()
{
	return memoize(kMemoDeclaration, &Recognizer::parse_declaration_body);
}

bool Recognizer::parse_declaration_body()
{
	switch (peek())
	{
	case OP_SEMICOLON:
		++pos_;  // empty-declaration
		return true;

	case KW_TEMPLATE:
		return parse_explicit_specialization() || parse_template_declaration() ||
			parse_explicit_instantiation();

	case KW_ASM:
		return parse_asm_definition();

	case KW_STATIC_ASSERT:
		return parse_static_assert_declaration();

	case KW_USING:
		return parse_alias_declaration() || parse_using_declaration() ||
			parse_using_directive();

	default:
		break;
	}

	if (at(KW_NAMESPACE) || (at(KW_INLINE) && peek(1) == KW_NAMESPACE))
	{
		if (parse_namespace_alias_definition() || parse_namespace_definition())
		{
			return true;
		}
	}

	if (at(KW_EXTERN))
	{
		if (parse_linkage_specification() || parse_explicit_instantiation())
		{
			return true;
		}
	}

	return parse_function_definition() || parse_block_declaration() ||
		parse_attribute_declaration();
}

bool Recognizer::parse_block_declaration()
{
	switch (peek())
	{
	case KW_ASM:
		return parse_asm_definition();
	case KW_STATIC_ASSERT:
		return parse_static_assert_declaration();
	case KW_USING:
		return parse_alias_declaration() || parse_using_declaration() ||
			parse_using_directive();
	case KW_NAMESPACE:
		return parse_namespace_alias_definition();
	default:
		break;
	}

	// A using-directive is the one block-declaration with leading attributes
	// of its own; every other form reaches them through simple-declaration.
	return parse_opaque_enum_declaration() || parse_simple_declaration() ||
		parse_using_directive();
}

// attribute-specifier* decl-specifier-seq init-declarator-list? OP_SEMICOLON
bool Recognizer::parse_simple_declaration()
{
	const Mark start = mark();
	parse_attribute_specifier_seq();
	if (!parse_decl_specifier_seq())
	{
		return fail(start);
	}
	parse_init_declarator_list();
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

// init-declarator (OP_COMMA init-declarator)*, where an init-declarator is a
// declarator with an optional initializer.
bool Recognizer::parse_init_declarator_list()
{
	const Mark start = mark();
	if (!parse_declarator())
	{
		return fail(start);
	}
	parse_initializer();

	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_declarator())
		{
			reset(item);
			break;
		}
		parse_initializer();
	}
	return true;
}

bool Recognizer::parse_initializer()
{
	if (parse_brace_or_equal_initializer())
	{
		return true;
	}
	const Mark start = mark();
	if (open_bracket(OP_LPAREN) && parse_expression_list() && accept(OP_RPAREN))
	{
		angle_ = start.angle;
		return true;
	}
	return fail(start);
}

bool Recognizer::parse_brace_or_equal_initializer()
{
	if (at(OP_LBRACE))
	{
		return parse_braced_init_list();
	}
	const Mark start = mark();
	if (accept(OP_ASS) && parse_initializer_clause())
	{
		return true;
	}
	return fail(start);
}

// KW_USING TT_IDENTIFIER attribute-specifier* OP_ASS type-id OP_SEMICOLON
bool Recognizer::parse_alias_declaration()
{
	const Mark start = mark();
	if (!accept(KW_USING) || !accept(TT_IDENTIFIER))
	{
		return fail(start);
	}
	parse_attribute_specifier_seq();
	if (!accept(OP_ASS) || !parse_type_id() || !accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_static_assert_declaration()
{
	const Mark start = mark();
	if (!accept(KW_STATIC_ASSERT) || !open_bracket(OP_LPAREN) ||
	    !parse_constant_expression() || !accept(OP_COMMA) || !accept(TT_LITERAL) ||
	    !accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_asm_definition()
{
	const Mark start = mark();
	if (!accept(KW_ASM) || !open_bracket(OP_LPAREN) || !accept(TT_LITERAL) ||
	    !accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

// KW_INLINE? KW_NAMESPACE TT_IDENTIFIER? OP_LBRACE namespace-body OP_RBRACE
bool Recognizer::parse_namespace_definition()
{
	const Mark start = mark();
	accept(KW_INLINE);
	if (!accept(KW_NAMESPACE))
	{
		return fail(start);
	}
	accept(TT_IDENTIFIER);
	if (!open_bracket(OP_LBRACE) || !parse_declaration_seq(OP_RBRACE) ||
	    !accept(OP_RBRACE))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

// KW_NAMESPACE TT_IDENTIFIER OP_ASS qualified-namespace-specifier OP_SEMICOLON
bool Recognizer::parse_namespace_alias_definition()
{
	const Mark start = mark();
	if (!accept(KW_NAMESPACE) || !accept(TT_IDENTIFIER) || !accept(OP_ASS))
	{
		return fail(start);
	}
	parse_nested_name_specifier();
	if (!at_identifier(kNameNamespace))
	{
		return fail(start);
	}
	++pos_;
	if (!accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

// KW_USING KW_TYPENAME? nested-name-specifier unqualified-id OP_SEMICOLON
// KW_USING OP_COLON2 unqualified-id OP_SEMICOLON
bool Recognizer::parse_using_declaration()
{
	const Mark start = mark();
	if (!accept(KW_USING))
	{
		return fail(start);
	}
	accept(KW_TYPENAME);
	if (!parse_nested_name_specifier() || !parse_unqualified_id() ||
	    !accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

// attribute-specifier* KW_USING KW_NAMESPACE nested-name-specifier?
//     TT_IDENTIFIER OP_SEMICOLON
bool Recognizer::parse_using_directive()
{
	const Mark start = mark();
	parse_attribute_specifier_seq();
	if (!accept(KW_USING) || !accept(KW_NAMESPACE))
	{
		return fail(start);
	}
	parse_nested_name_specifier();
	if (!accept(TT_IDENTIFIER) || !accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

// KW_EXTERN TT_LITERAL OP_LBRACE declaration* OP_RBRACE
// KW_EXTERN TT_LITERAL declaration
bool Recognizer::parse_linkage_specification()
{
	const Mark start = mark();
	if (!accept(KW_EXTERN) || !accept(TT_LITERAL))
	{
		return fail(start);
	}
	if (at(OP_LBRACE))
	{
		if (!open_bracket(OP_LBRACE) || !parse_declaration_seq(OP_RBRACE) ||
		    !accept(OP_RBRACE))
		{
			return fail(start);
		}
		angle_ = start.angle;
		return true;
	}
	if (!parse_declaration())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_attribute_declaration()
{
	const Mark start = mark();
	if (!parse_attribute_specifier() || !parse_attribute_specifier_seq() ||
	    !accept(OP_SEMICOLON))
	{
		return fail(start);
	}
	return true;
}

// attribute-specifier* decl-specifier-seq declarator virt-specifier*
//     function-body
bool Recognizer::parse_function_definition()
{
	const Mark start = mark();
	parse_attribute_specifier_seq();
	if (!parse_decl_specifier_seq() || !parse_declarator())
	{
		return fail(start);
	}
	parse_virt_specifiers();
	if (!parse_function_body())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_function_body()
{
	const Mark start = mark();
	if (accept(OP_ASS))
	{
		if ((accept(KW_DEFAULT) || accept(KW_DELETE)) && accept(OP_SEMICOLON))
		{
			return true;
		}
		return fail(start);
	}
	if (at(KW_TRY))
	{
		// function-try-block: the ctor-initializer sits inside the `try`.
		++pos_;
		parse_ctor_initializer();
		if (parse_compound_statement() && parse_handler_seq())
		{
			return true;
		}
		return fail(start);
	}
	parse_ctor_initializer();
	if (!parse_compound_statement())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_ctor_initializer()
{
	const Mark start = mark();
	if (!accept(OP_COLON) || !parse_mem_initializer_list())
	{
		return fail(start);
	}
	return true;
}

bool Recognizer::parse_mem_initializer_list()
{
	if (!parse_mem_initializer())
	{
		return false;
	}
	accept(OP_DOTS);
	while (at(OP_COMMA))
	{
		const Mark item = mark();
		++pos_;
		if (!parse_mem_initializer())
		{
			return fail(item);
		}
		accept(OP_DOTS);
	}
	return true;
}

// mem-initializer-id (OP_LPAREN expression-list? OP_RPAREN | braced-init-list)
bool Recognizer::parse_mem_initializer()
{
	const Mark start = mark();
	if (!parse_class_or_decltype() && !accept(TT_IDENTIFIER))
	{
		return fail(start);
	}
	if (at(OP_LBRACE))
	{
		if (parse_braced_init_list())
		{
			return true;
		}
		return fail(start);
	}
	if (!open_bracket(OP_LPAREN))
	{
		return fail(start);
	}
	parse_expression_list();
	if (!accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

bool Recognizer::parse_virt_specifiers()
{
	while (at_identifier(kIsOverride) || at_identifier(kIsFinal))
	{
		++pos_;
	}
	return true;
}

bool Recognizer::parse_attribute_specifier_seq()
{
	return memoize(kMemoAttributeSpecifierSeq,
	               &Recognizer::parse_attribute_specifier_seq_body);
}

bool Recognizer::parse_attribute_specifier_seq_body()
{
	while (parse_attribute_specifier())
	{
	}
	return true;
}

bool Recognizer::parse_attribute_specifier()
{
	if (at(KW_ALIGNAS))
	{
		return parse_alignment_specifier();
	}
	if (!at(OP_LSQUARE) || peek(1) != OP_LSQUARE)
	{
		return false;
	}
	const Mark start = mark();
	pos_ += 2;
	angle_ = false;
	if (!parse_attribute_list() || !accept(OP_RSQUARE) || !accept(OP_RSQUARE))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

// KW_ALIGNAS OP_LPAREN (type-id | assignment-expression) OP_DOTS? OP_RPAREN
bool Recognizer::parse_alignment_specifier()
{
	const Mark start = mark();
	if (!accept(KW_ALIGNAS) || !open_bracket(OP_LPAREN))
	{
		return fail(start);
	}
	if (!parse_type_id() && !parse_assignment_expression())
	{
		return fail(start);
	}
	accept(OP_DOTS);
	if (!accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

// attribute-part (OP_COMMA attribute-part)*, where a part may be empty.
bool Recognizer::parse_attribute_list()
{
	if (parse_attribute())
	{
		accept(OP_DOTS);
	}
	while (accept(OP_COMMA))
	{
		if (parse_attribute())
		{
			accept(OP_DOTS);
		}
	}
	return true;
}

bool Recognizer::parse_attribute()
{
	const Mark start = mark();
	if (!accept(TT_IDENTIFIER))
	{
		return false;
	}
	if (accept(OP_COLON2) && !accept(TT_IDENTIFIER))
	{
		return fail(start);
	}
	parse_attribute_argument_clause();
	return true;
}

bool Recognizer::parse_attribute_argument_clause()
{
	const Mark start = mark();
	if (!open_bracket(OP_LPAREN) || !parse_balanced_tokens(OP_RPAREN) ||
	    !accept(OP_RPAREN))
	{
		return fail(start);
	}
	angle_ = start.angle;
	return true;
}

// balanced-token*, up to but not including `closer`.  Any token that is not a
// bracket or the end of the file is an ST_NONPAREN.
bool Recognizer::parse_balanced_tokens(unsigned closer)
{
	while (!at(closer))
	{
		switch (peek())
		{
		case ST_EOF:
		case OP_RPAREN:
		case OP_RSQUARE:
		case OP_RBRACE:
			return false;

		case OP_LPAREN:
			++pos_;
			if (!parse_balanced_tokens(OP_RPAREN) || !accept(OP_RPAREN))
			{
				return false;
			}
			break;

		case OP_LSQUARE:
			++pos_;
			if (!parse_balanced_tokens(OP_RSQUARE) || !accept(OP_RSQUARE))
			{
				return false;
			}
			break;

		case OP_LBRACE:
			++pos_;
			if (!parse_balanced_tokens(OP_RBRACE) || !accept(OP_RBRACE))
			{
				return false;
			}
			break;

		default:
			++pos_;
			break;
		}
	}
	return true;
}
