#include "decl_parser.h"

// The declarator half of the PA7 parser: `declarator`, `abstract-declarator`
// and everything 8.3 derives a type from.
//
// One routine parses both a declarator and an abstract declarator, because the
// two differ only in whether a `declarator-id` may appear and because a
// `parameter-declaration` has to be read as a declarator when it can be one.
// The two constructs the grammar cannot tell apart by their first token are
// both decided before descending rather than by trying and undoing:
//
//   - an `(` at the start of a `noptr-declarator` opens a parenthesized
//     declarator unless what follows can start a parameter-declaration-clause,
//     which is 8.2p3's resolution of `int f(int (T))`;
//   - an identifier is a `simple-type-specifier` only while the specifier
//     sequence has no type yet, which is what makes `T x` a declaration of `x`
//     rather than of `T`.

DeclaratorNode* DeclParser::parse_declarator(Namespace& where, bool allow_name,
                                             DeclaratorId& id)
{
	const ParseDepth::Frame frame(depth_);
	if (frame.overflowed())
	{
		throw SemanticError("a declarator nests deeper than the tool supports");
	}

	const Mark saved = mark();
	const DeclaratorId saved_id = id;
	arena_.push_back(DeclaratorNode());
	DeclaratorNode& node = arena_.back();

	parse_ptr_operators(node);

	bool rooted = false;
	if (allow_name && (at(TT_IDENTIFIER) || at(OP_COLON2)))
	{
		if (!parse_declarator_id(where, id))
		{
			id = saved_id;
			reset(saved);
			return nullptr;
		}
		rooted = true;
	}
	else if (at(OP_LPAREN) && !starts_parameter_clause(where))
	{
		const Mark before = mark();
		advance();
		DeclaratorNode* inner = parse_declarator(where, allow_name, id);
		if (inner != nullptr && accept(OP_RPAREN))
		{
			node.inner = inner;
			rooted = true;
		}
		else
		{
			id = saved_id;
			reset(before);
		}
	}

	parse_declarator_suffixes(where, node);

	// Nothing at all is not a declarator: an omitted abstract-declarator is
	// the caller's business, not this one's.
	if (!rooted && node.prefix.empty() && node.suffix.empty())
	{
		id = saved_id;
		reset(saved);
		return nullptr;
	}
	return &node;
}

bool DeclParser::parse_declarator_id(Namespace& where, DeclaratorId& id)
{
	if (at(OP_COLON2) || (at(TT_IDENTIFIER) && peek(1) == OP_COLON2))
	{
		Namespace* scope = nullptr;
		if (!parse_nested_name_specifier(where, scope))
		{
			return false;
		}
		id.qualifier = scope;
		// 3.4.3p3: the names after a qualified declarator-id are looked up in
		// the namespace it names, which is what lets an array bound and an
		// initializer of `int A::a[n];` mean `A::n`.
		value_scope_ = scope;
	}
	if (!at(TT_IDENTIFIER))
	{
		return false;
	}
	id.name = name_at();
	advance();
	return true;
}

void DeclParser::parse_ptr_operators(DeclaratorNode& node)
{
	for (;;)
	{
		DeclaratorOperator op;
		if (accept(OP_STAR))
		{
			op.kind = DeclaratorOperator::kPointer;
			op.cv = parse_cv_qualifiers();
		}
		else if (accept(OP_AMP))
		{
			op.kind = DeclaratorOperator::kLValueReference;
		}
		else if (accept(OP_LAND))
		{
			op.kind = DeclaratorOperator::kRValueReference;
		}
		else
		{
			return;
		}
		node.prefix.push_back(op);
	}
}

unsigned DeclParser::parse_cv_qualifiers()
{
	unsigned cv = kCvNone;
	for (;;)
	{
		if (accept(KW_CONST))
		{
			cv |= kCvConst;
		}
		else if (accept(KW_VOLATILE))
		{
			cv |= kCvVolatile;
		}
		else
		{
			return cv;
		}
	}
}

void DeclParser::parse_declarator_suffixes(Namespace& where, DeclaratorNode& node)
{
	for (;;)
	{
		if (at(OP_LPAREN))
		{
			const Mark saved = mark();
			advance();
			DeclaratorOperator op;
			op.kind = DeclaratorOperator::kFunction;
			if (!parse_parameter_clause(where, op) || !accept(OP_RPAREN))
			{
				reset(saved);
				return;
			}
			node.suffix.push_back(op);
			continue;
		}
		if (at(OP_LSQUARE))
		{
			DeclaratorOperator op;
			op.kind = DeclaratorOperator::kArray;
			if (!parse_array_suffix(op))
			{
				return;
			}
			node.suffix.push_back(op);
			continue;
		}
		return;
	}
}

bool DeclParser::parse_array_suffix(DeclaratorOperator& op)
{
	const Mark saved = mark();
	advance();

	// `pa8.gram` spells the bound as a constant-expression, which has to be
	// analysed and evaluated; `pa7.gram` spells it as a literal, where the
	// same two questions are the token's own.
	if (image_ != nullptr)
	{
		if (!at(OP_RSQUARE))
		{
			ExprValue bound;
			initializing_ = false;
			if (!parse_expression(bound))
			{
				throw SemanticError("an array bound is not an expression");
			}
			op.bounded = true;
			op.bound = init_->to_array_bound(bound);
		}
		expect(OP_RSQUARE);
		return true;
	}

	if (at(TT_LITERAL))
	{
		// 8.3.4: the bound is a converted constant expression of type
		// `std::size_t` whose value is greater than zero.
		const SemaToken& literal = token_at();
		if (!literal.integral || literal.value == 0)
		{
			reset(saved);
			return false;
		}
		op.bounded = true;
		op.bound = literal.value;
		advance();
	}
	if (!accept(OP_RSQUARE))
	{
		reset(saved);
		return false;
	}
	return true;
}

bool DeclParser::starts_parameter_clause(Namespace& where)
{
	const Mark saved = mark();
	advance();

	bool result = false;
	switch (peek())
	{
	case OP_RPAREN:
	case OP_DOTS:
	case KW_STATIC:
	case KW_THREAD_LOCAL:
	case KW_EXTERN:
	case KW_TYPEDEF:
	case KW_CONST:
	case KW_VOLATILE:
		result = true;
		break;

	default:
		if (builtin_specifier(peek()) >= 0)
		{
			result = true;
		}
		else if (at(TT_IDENTIFIER) || at(OP_COLON2))
		{
			// 8.2p3: a type-name in parentheses is a simple-type-specifier
			// rather than a declarator-id, so `void f(int (T))` declares a
			// parameter of function type when `T` is a type.
			TypeId type = kNoType;
			result = parse_type_name(where, type);
		}
		break;
	}

	reset(saved);
	return result;
}

bool DeclParser::parse_parameter_clause(Namespace& where, DeclaratorOperator& op)
{
	if (accept(OP_DOTS))
	{
		op.variadic = true;
		return true;
	}
	if (at(OP_RPAREN))
	{
		return true;
	}

	std::vector<TypeId> declared;
	std::vector<bool> named;
	for (;;)
	{
		TypeId type = kNoType;
		bool has_name = false;
		if (!parse_parameter(where, type, has_name))
		{
			return false;
		}
		declared.push_back(type);
		named.push_back(has_name);

		if (accept(OP_COMMA))
		{
			if (accept(OP_DOTS))
			{
				op.variadic = true;
				break;
			}
			continue;
		}
		if (accept(OP_DOTS))
		{
			op.variadic = true;
		}
		break;
	}

	// 8.3.5p4: `(void)` is the empty parameter list, whatever spelling of
	// `void` a typedef gave it.
	if (!op.variadic && declared.size() == 1 && !named[0] &&
	    types_.is_plain_void(declared[0]))
	{
		return true;
	}

	op.parameters.reserve(declared.size());
	for (std::size_t index = 0; index < declared.size(); ++index)
	{
		// 8.3.5p4: `void` is the empty parameter list or nothing at all, so a
		// list that got here with a `void` in it declares a parameter of a
		// type no object can have.
		if (types_.is_void(declared[index]))
		{
			throw SemanticError("a parameter is declared with type void");
		}
		op.parameters.push_back(types_.adjust_parameter(declared[index]));
	}
	return true;
}

bool DeclParser::parse_parameter(Namespace& where, TypeId& type, bool& named)
{
	Specifiers specifiers;
	if (!parse_specifier_seq(where, true, specifiers))
	{
		return false;
	}
	if (!specifiers.has_type_name && specifiers.builtins == 0)
	{
		return false;
	}

	const TypeId base = specifier_type(specifiers);
	DeclaratorId id;
	DeclaratorNode* node = parse_declarator(where, true, id);
	type = node == nullptr ? base : build_type(*node, base);
	named = id.name != kNoName;

	// The parameter has to end where the list can go on or close; anything
	// else means this was not a parameter-declaration after all.
	return at(OP_COMMA) || at(OP_RPAREN) || at(OP_DOTS);
}

TypeId DeclParser::build_type(const DeclaratorNode& node, TypeId base,
                              bool* function_declarator)
{
	// 8.3: the ptr-operators of a declarator apply to the type its
	// decl-specifier-seq gave, the suffixes apply to that from the last one
	// inwards, and the parenthesized declarator is then declared with the
	// result.  Parenthesization nests but does not branch, so one loop walks
	// however deep it goes.
	TypeId type = base;
	bool written_reference = false;
	DeclaratorOperator::Kind outermost = DeclaratorOperator::kPointer;
	bool derived = false;
	for (const DeclaratorNode* current = &node; current != nullptr;
	     current = current->inner)
	{
		for (std::size_t index = 0; index < current->prefix.size(); ++index)
		{
			type = apply(current->prefix[index], type, written_reference);
			outermost = current->prefix[index].kind;
			derived = true;
		}
		for (std::size_t index = current->suffix.size(); index-- > 0; )
		{
			type = apply(current->suffix[index], type, written_reference);
			outermost = current->suffix[index].kind;
			derived = true;
		}
	}
	if (function_declarator != nullptr)
	{
		*function_declarator = derived && outermost == DeclaratorOperator::kFunction;
	}
	return type;
}

TypeId DeclParser::apply(const DeclaratorOperator& op, TypeId type, bool& written_reference)
{
	// 8.3.1p4, 8.3.2p5 and 8.3.4p1 forbid shapes the type table would build
	// happily, and 8.3.2p6 collapses a reference to a reference only when the
	// inner one was denoted by a typedef rather than written here.
	const bool reference = types_.is_reference(type);
	const bool was_written = written_reference;
	written_reference = false;
	switch (op.kind)
	{
	case DeclaratorOperator::kPointer:
		if (reference)
		{
			throw SemanticError("a declarator declares a pointer to a reference");
		}
		return types_.qualified(types_.pointer_to(type), op.cv);

	case DeclaratorOperator::kLValueReference:
	case DeclaratorOperator::kRValueReference:
		if (was_written)
		{
			throw SemanticError("a declarator declares a reference to a reference");
		}
		if (types_.is_void(type))
		{
			throw SemanticError("a declarator declares a reference to void");
		}
		written_reference = true;
		return types_.reference_to(type, op.kind == DeclaratorOperator::kRValueReference);

	case DeclaratorOperator::kArray:
		if (reference || types_.is_void(type) || types_.kind(type) == TypeKind::Function)
		{
			throw SemanticError("a declarator declares an array of a type that has no size");
		}
		if (types_.kind(type) == TypeKind::Array && !types_.bounded(type))
		{
			throw SemanticError("a declarator declares an array of an incomplete type");
		}
		return types_.array_of(type, op.bounded, op.bound);

	default:
		// 8.3.5p8.
		if (types_.kind(type) == TypeKind::Array || types_.kind(type) == TypeKind::Function)
		{
			throw SemanticError("a declarator declares a function returning an array "
			                    "or a function");
		}
		return types_.function_of(type, op.parameters, op.variadic);
	}
}
