#include "ast_parser.h"

// Names, and the token spans the dump spells them from.
//
// Every name the shared grammar leaves for a later assignment to resolve - a
// template-id, a qualified name, a decltype-specifier - is dumped as it was
// written rather than as a structure.  So the rules here match a name and
// leave the cursor past it; the caller spells the span it covered.

namespace
{

// The tokens `operator-token` names.  `>>` is two terminals, so the first of
// the pair stands for the pair here and the rule consumes both.
bool is_operator_token(unsigned type)
{
	switch (type)
	{
	case OP_PLUS: case OP_MINUS: case OP_STAR: case OP_DIV: case OP_MOD:
	case OP_XOR: case OP_AMP: case OP_BOR: case OP_COMPL: case OP_LNOT:
	case OP_ASS: case OP_LT: case OP_GT: case OP_PLUSASS: case OP_MINUSASS:
	case OP_STARASS: case OP_DIVASS: case OP_MODASS: case OP_XORASS:
	case OP_BANDASS: case OP_BORASS: case OP_LSHIFT: case ST_RSHIFT_1:
	case OP_EQ: case OP_NE: case OP_LE: case OP_GE: case OP_LAND:
	case OP_LOR: case OP_INC: case OP_DEC: case OP_COMMA: case OP_ARROWSTAR:
	case OP_ARROW:
		return true;
	default:
		return false;
	}
}

}

NameKind DeclaredNames::kind_of(const std::string& name) const
{
	for (std::size_t index = scopes_.size(); index-- > 0; )
	{
		const std::unordered_map<std::string, NameKind>::const_iterator found =
			scopes_[index].find(name);
		if (found != scopes_[index].end())
		{
			return found->second;
		}
	}
	return NameKind::Unknown;
}

void AstParser::declare_name(const std::string& name, NameKind kind)
{
	template_id_memo_.clear();
	names_.declare(name, kind);
	if (!prefix_.empty() && !name.empty())
	{
		qualified_[prefix_ + name] = kind;
	}
}

// The kind a declaration introduces, which is a template-name when the
// declaration is the one a template-declaration wraps.
NameKind AstParser::take_declared_kind(NameKind fallback)
{
	if (!template_pending_)
	{
		return fallback;
	}
	template_pending_ = false;
	return NameKind::Template;
}

// What a name denotes, whether it is written plain or qualified.
NameKind AstParser::kind_of_name(const std::string& name) const
{
	if (name.find(':') == std::string::npos)
	{
		return names_.kind_of(name);
	}
	const std::unordered_map<std::string, NameKind>::const_iterator found =
		qualified_.find(name);
	return found == qualified_.end() ? NameKind::Unknown : found->second;
}

bool AstParser::at_close_angle() const
{
	return at(OP_GT) || at(ST_RSHIFT_1) || at(ST_RSHIFT_2);
}

bool AstParser::accept_close_angle()
{
	return accept(OP_GT) || accept(ST_RSHIFT_1) || accept(ST_RSHIFT_2);
}

// Consumes the balanced tokens up to and including the `closer` that matches
// an opener the caller has already consumed.
bool AstParser::skip_balanced(unsigned closer)
{
	int depth = 0;
	for (;;)
	{
		const unsigned type = peek();
		if (type == ST_EOF)
		{
			return false;
		}
		if (depth == 0 && type == closer)
		{
			++pos_;
			return true;
		}
		if (type == OP_LPAREN || type == OP_LSQUARE || type == OP_LBRACE)
		{
			++depth;
		}
		else if (type == OP_RPAREN || type == OP_RSQUARE || type == OP_RBRACE)
		{
			--depth;
		}
		++pos_;
	}
}

// The attribute syntax the assignment accepts but does not model: the
// attribute-specifiers of 7.6.1, `alignas`, and the vendor spelling the intake
// tests use.  None of them reaches the tree, so they are consumed wherever a
// declaration, a class head or a declarator may carry one.
void AstParser::skip_attributes()
{
	for (;;)
	{
		if (at(OP_LSQUARE) && peek(1) == OP_LSQUARE)
		{
			pos_ += 2;
			if (!skip_balanced(OP_RSQUARE) || !accept(OP_RSQUARE))
			{
				return;
			}
			continue;
		}
		if (at(KW_ALIGNAS) && peek(1) == OP_LPAREN)
		{
			pos_ += 2;
			if (!skip_balanced(OP_RPAREN))
			{
				return;
			}
			continue;
		}
		if (at(TT_IDENTIFIER) && spelling() == "__attribute__" && peek(1) == OP_LPAREN)
		{
			pos_ += 2;
			if (!skip_balanced(OP_RPAREN))
			{
				return;
			}
			continue;
		}
		return;
	}
}

bool AstParser::skip_decltype_specifier()
{
	const Mark start = mark();
	if (!accept(KW_DECLTYPE) || !at(OP_LPAREN))
	{
		reset(start);
		return false;
	}
	++pos_;
	if (!skip_balanced(OP_RPAREN))
	{
		reset(start);
		return false;
	}
	return true;
}

bool AstParser::skip_simple_template_id(bool qualified)
{
	const Mark start = mark();
	const bool memoized = template_id_veto_depth_ < 0;
	const std::size_t key = start.pos * 2 + (start.angle ? 1 : 0);
	if (memoized)
	{
		const std::unordered_map<std::size_t, std::size_t>::const_iterator found =
			template_id_memo_.find(key);
		if (found != template_id_memo_.end())
		{
			if (found->second == 0)
			{
				return false;
			}
			pos_ = found->second - 1;
			return true;
		}
	}
	const bool matched = skip_simple_template_id_body(qualified);
	if (memoized)
	{
		template_id_memo_[key] = matched ? pos_ + 1 : 0;
	}
	return matched;
}

bool AstParser::skip_simple_template_id_body(bool qualified)
{
	const Mark start = mark();
	// An unqualified identifier a declaration in scope made an object is not a
	// template-name, so the `<` after it is the relational operator it also
	// spells.  This is the only place PA10 needs to tell the two apart; a name
	// behind a nested-name-specifier is in a scope PA10 does not model, so it
	// keeps the reading the grammar gives it.
	if (!at(TT_IDENTIFIER) || (!qualified && names_.is_value(spelling())))
	{
		return false;
	}
	++pos_;
	if (!at(OP_LT) || template_id_veto_depth_ == bracket_depth_)
	{
		reset(start);
		return false;
	}
	++pos_;
	BracketGuard guard(*this, true);
	if (!at_close_angle())
	{
		do
		{
			if (!parse_template_argument())
			{
				reset(start);
				return false;
			}
			accept(OP_DOTS);
		}
		while (accept(OP_COMMA));
	}
	if (!accept_close_angle())
	{
		reset(start);
		return false;
	}
	return true;
}

bool AstParser::skip_type_name(bool qualified)
{
	return skip_simple_template_id(qualified) || accept(TT_IDENTIFIER);
}

// `nested-name-specifier`: a root and its suffixes, each ending in `::`.  It
// only matches when at least one `::` follows, so a bare name is left to the
// caller.
bool AstParser::skip_nested_name_specifier()
{
	const Mark start = mark();
	if (!accept(OP_COLON2))
	{
		const Mark root = mark();
		if (skip_decltype_specifier() || skip_type_name())
		{
			if (!accept(OP_COLON2))
			{
				reset(root);
				return false;
			}
		}
		else
		{
			reset(start);
			return false;
		}
	}
	for (;;)
	{
		const Mark suffix = mark();
		accept(KW_TEMPLATE);
		if (!skip_type_name(true) || !accept(OP_COLON2))
		{
			reset(suffix);
			return true;
		}
	}
}

bool AstParser::skip_operator_id(bool& conversion)
{
	const Mark start = mark();
	conversion = false;
	if (!accept(KW_OPERATOR))
	{
		return false;
	}
	// `literal-operator-id`.  Phase 7 has already joined `""` to a suffix that
	// follows it directly, so the name is either one token or two.
	if (at(TT_LITERAL) && spelling().compare(0, 2, "\"\"") == 0)
	{
		const bool joined = spelling().size() > 2;
		++pos_;
		if (joined || accept(TT_IDENTIFIER))
		{
			return true;
		}
		reset(start);
		return false;
	}
	if (accept(KW_NEW) || accept(KW_DELETE))
	{
		if (accept(OP_LSQUARE) && !accept(OP_RSQUARE))
		{
			reset(start);
			return false;
		}
		return true;
	}
	if ((at(OP_LPAREN) && peek(1) == OP_RPAREN) ||
	    (at(OP_LSQUARE) && peek(1) == OP_RSQUARE))
	{
		pos_ += 2;
		return true;
	}
	if (is_operator_token(peek()))
	{
		++pos_;
		if (peek() == ST_RSHIFT_2 && tokens_.type(pos_ - 1) == ST_RSHIFT_1)
		{
			++pos_;
		}
		return true;
	}
	if (skip_conversion_type_id())
	{
		conversion = true;
		return true;
	}
	reset(start);
	return false;
}

// `conversion-type-id`: a type-specifier-seq and pointer operators only, so
// that the parentheses of `operator int()` are the parameter clause.
bool AstParser::skip_conversion_type_id()
{
	const Mark start = mark();
	if (parse_specifier_seq(SpecifierMode::Type) == nullptr)
	{
		reset(start);
		return false;
	}
	while (parse_ptr_operator() != nullptr)
	{
		while (at(KW_CONST) || at(KW_VOLATILE))
		{
			++pos_;
		}
	}
	return true;
}

bool AstParser::skip_unqualified_id(bool qualified)
{
	if (at(KW_OPERATOR))
	{
		bool conversion = false;
		return skip_operator_id(conversion);
	}
	if (at(OP_COMPL))
	{
		const Mark start = mark();
		++pos_;
		if (skip_decltype_specifier() || skip_type_name(qualified))
		{
			return true;
		}
		reset(start);
		return false;
	}
	return skip_type_name(qualified);
}

// `qualified-type-name`, which is where the grammar names a type without
// deciding what it denotes.
bool AstParser::skip_qualified_type_name()
{
	const Mark start = mark();
	if (skip_nested_name_specifier())
	{
		accept(KW_TEMPLATE);
		if (skip_type_name(true))
		{
			return true;
		}
		reset(start);
		return false;
	}
	return skip_type_name();
}

AstNode* AstParser::parse_id_expression()
{
	const Mark start = mark();
	accept(KW_TYPENAME);
	const Mark name = mark();
	const bool qualified = skip_nested_name_specifier();
	if (qualified)
	{
		accept(KW_TEMPLATE);
	}
	if (!skip_unqualified_id(qualified))
	{
		return fail(start);
	}
	return make_text(AstKind::IdExpression, spelled(name));
}

// The name after `.` or `->`.  A member name is not looked up before a
// semantic assignment exists, so `a.b < c` is a comparison unless `template`
// says the name is a template.
AstNode* AstParser::parse_member_id()
{
	const Mark start = mark();
	if (skip_nested_name_specifier())
	{
		accept(KW_TEMPLATE);
		if (!skip_unqualified_id(true))
		{
			return fail(start);
		}
		return make_text(AstKind::Identifier, spelled(start));
	}
	if (accept(KW_TEMPLATE))
	{
		if (!skip_unqualified_id(true))
		{
			return fail(start);
		}
		return make_text(AstKind::Identifier, spelled(start));
	}
	if (at(KW_OPERATOR) || at(OP_COMPL))
	{
		if (!skip_unqualified_id())
		{
			return fail(start);
		}
		if (tokens_.type(start.pos) == KW_OPERATOR)
		{
			return make_text(AstKind::Identifier,
			                 "operator" + tokens_.flatten(start.pos + 1, pos_));
		}
		return make_text(AstKind::Identifier, spelled(start));
	}
	if (!accept(TT_IDENTIFIER))
	{
		return fail(start);
	}
	return make_text(AstKind::Identifier, spelled(start));
}

AstNode* AstParser::parse_declarator_id()
{
	const Mark start = mark();
	const bool qualified = skip_nested_name_specifier();
	if (qualified)
	{
		accept(KW_TEMPLATE);
	}
	const Mark unqualified = mark();
	if (!skip_unqualified_id(qualified))
	{
		return fail(start);
	}
	if (!qualified && tokens_.type(unqualified.pos) == KW_OPERATOR)
	{
		return make_text(AstKind::Identifier,
		                 "operator" + tokens_.flatten(unqualified.pos + 1, pos_));
	}
	return make_text(AstKind::Identifier, spelled(start));
}

// A constructor, destructor, conversion function or operator function name, as
// the special-member rules of the grammar spell one.  Empty when the cursor is
// not at one.
std::string AstParser::parse_special_member_name()
{
	const Mark start = mark();
	const bool qualified = skip_nested_name_specifier();
	const Mark unqualified = mark();
	if (accept(OP_COMPL))
	{
		if (!skip_type_name(qualified))
		{
			reset(start);
			return std::string();
		}
	}
	else if (at(KW_OPERATOR))
	{
		bool conversion = false;
		if (!skip_operator_id(conversion) || (qualified && !conversion))
		{
			reset(start);
			return std::string();
		}
		if (!qualified)
		{
			return "operator" + tokens_.flatten(unqualified.pos + 1, pos_);
		}
	}
	else if (!skip_type_name(qualified))
	{
		reset(start);
		return std::string();
	}
	return spelled(start);
}
