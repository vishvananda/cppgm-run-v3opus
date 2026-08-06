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

void DeclaredNames::declare(const std::string& name, NameKind kind)
{
	if (!name.empty())
	{
		scopes_.back().names[name] = kind;
		++version_;
	}
}

void DeclaredNames::declare_member(const std::string& name, NameKind kind)
{
	declare(name, kind);
	if (!prefix_.empty() && !name.empty())
	{
		qualified_[prefix_ + name] = kind;
		++version_;
	}
}

void DeclaredNames::alias(const std::string& name, const std::string& target)
{
	if (!name.empty() && !target.empty())
	{
		aliases_[name] = target;
		++version_;
	}
}

void DeclaredNames::nominate(const std::string& target)
{
	if (!target.empty())
	{
		scopes_.back().nominated.push_back(target + "::");
		++version_;
	}
}

NameKind DeclaredNames::spelled_kind(const std::string& spelling) const
{
	// 7.3.2p1 lets an alias name a namespace that is itself named by one, so
	// the rewrite is a walk, bounded by the aliases there are so that a
	// spelling written through a cycle is answered rather than followed.
	std::string written = spelling;
	for (std::size_t step = 0; step <= aliases_.size(); ++step)
	{
		const std::unordered_map<std::string, NameKind>::const_iterator found =
			qualified_.find(written);
		if (found != qualified_.end())
		{
			return found->second;
		}
		const std::string::size_type colons = written.find("::");
		if (colons == std::string::npos)
		{
			return NameKind::Unknown;
		}
		const std::unordered_map<std::string, std::string>::const_iterator named =
			aliases_.find(written.substr(0, colons));
		if (named == aliases_.end())
		{
			return NameKind::Unknown;
		}
		written = named->second + written.substr(colons);
	}
	return NameKind::Unknown;
}

NameKind DeclaredNames::kind_of(const std::string& name) const
{
	const bool qualified = name.find(':') != std::string::npos;
	if (qualified)
	{
		const NameKind kind = spelled_kind(name);
		if (kind != NameKind::Unknown)
		{
			return kind;
		}
	}
	else
	{
		for (std::size_t index = scopes_.size(); index-- > 0; )
		{
			const std::unordered_map<std::string, NameKind>::const_iterator found =
				scopes_[index].names.find(name);
			if (found != scopes_[index].names.end())
			{
				return found->second;
			}
		}
	}
	// 7.3.4p2: a name no scope declares may still be one the using-directives
	// in scope reach.  That question is asked only once every scope has been
	// asked the cheap one, so a name that is declared costs no probe of a
	// directive however many are written.
	for (std::size_t index = scopes_.size(); index-- > 0; )
	{
		const std::vector<std::string>& nominated = scopes_[index].nominated;
		for (std::size_t at = 0; at < nominated.size(); ++at)
		{
			const NameKind kind = spelled_kind(nominated[at] + name);
			if (kind != NameKind::Unknown)
			{
				return kind;
			}
		}
	}
	return NameKind::Unknown;
}

// The kind a declaration introduces, which is a template-name when the
// declaration is the one a template-declaration wraps.  Which of the two
// template-names it is follows from what the declaration would otherwise have
// declared: 14p1 lets a template-declaration declare a class, an alias or a
// function, and only the last of those names no type.
NameKind AstParser::take_declared_kind(NameKind fallback)
{
	if (!template_pending_)
	{
		return fallback;
	}
	template_pending_ = false;
	return fallback == NameKind::Value ? NameKind::FunctionTemplate
	                                  : NameKind::Template;
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
void AstParser::skip_attributes(std::vector<AstNode*>* alignments)
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
			const Mark opened = mark();
			++pos_;
			if (alignments != nullptr)
			{
				// 7.6.2p1: the operand is either a type-id or a
				// constant-expression, and the type-id form asks for the
				// alignment `alignof` gives that type.  Writing it as that
				// `alignof` leaves one expression for the layout to evaluate
				// rather than two forms for it to tell apart.
				AstNode* written = parse_parenthesized_operand(true);
				if (written != nullptr)
				{
					if (written->kind == AstKind::TypeId)
					{
						AstNode* const asked =
							make(AstKind::TypeTraitExpression);
						asked->token = static_cast<std::uint16_t>(KW_ALIGNOF);
						asked->text = "alignof";
						asked->add(written);
						written = asked;
					}
					AstNode* const specifier = make(AstKind::AlignmentSpecifier);
					specifier->add(written);
					alignments->push_back(specifier);
					continue;
				}
			}
			++pos_;
			if (!skip_balanced(OP_RPAREN))
			{
				reset(opened);
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

// Everything besides the names in scope that a position's answer turns on.
//
// A veto forbids a template-id at the one bracket depth the template-argument
// that set it is being read at, and every rule the reading descends into runs
// deeper than that, so `veto == depth` is the whole of its effect here: a
// nested position sees no veto and shares its answer with an unvetoed reading.
std::size_t AstParser::template_id_memo_key(const Mark& start, bool qualified) const
{
	return start.pos * 8 + (start.angle ? 4u : 0u) +
		(template_id_veto_depth_ == bracket_depth_ ? 2u : 0u) +
		(qualified ? 1u : 0u);
}

bool AstParser::skip_simple_template_id(bool qualified)
{
	const Mark start = mark();
	if (template_id_memo_version_ != names_.version())
	{
		template_id_memo_.clear();
		template_id_memo_version_ = names_.version();
	}
	const std::size_t key = template_id_memo_key(start, qualified);
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
	const Frame frame(depth_);
	if (frame.overflowed())
	{
		return false;
	}
	const bool matched = skip_simple_template_id_body(qualified);
	// A template-argument may hold a lambda, whose body declares names, so the
	// reading just taken can be the thing that made the table out of date.  A
	// reading the depth limit cut short is not a fact about the position
	// either, so neither is remembered.
	if (template_id_memo_version_ == names_.version() && !frame.overflowed())
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
bool AstParser::skip_qualified_type_name(std::string* template_name)
{
	const Mark start = mark();
	Mark component = start;
	if (skip_nested_name_specifier())
	{
		accept(KW_TEMPLATE);
		component = mark();
		if (!skip_type_name(true))
		{
			reset(start);
			return false;
		}
	}
	else if (!skip_type_name())
	{
		return false;
	}
	// 14.2p1: a template-id is a template-name and an argument list, and the
	// list belongs to the use rather than to the name a declaration made.  So
	// what the names in scope are asked about is the name up to the identifier
	// the last component starts with, which is the whole of an ordinary name.
	if (template_name != nullptr && tokens_.type(component.pos) == TT_IDENTIFIER)
	{
		*template_name = tokens_.flatten(start.pos, component.pos + 1);
	}
	return true;
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
