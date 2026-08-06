#include "sema_analyzer.h"

#include <cstring>
#include <stdexcept>

#include "ast_model.h"
#include "ast_tokens.h"
#include "literal_scan.h"
#include "post_token.h"
#include "string_literal.h"

// The PA12 expression layer: 5 over the procedural, non-class subset.
//
// An expression is analysed bottom up in one visit.  What an operand denotes -
// its type, its value category, whether it is a constant, and, for a function
// name, the declarations it stands for - travels up in one `Value`, so an
// operator asks its operands rather than the tree beneath them, and no subtree
// is read twice.  The node an operand wrote is carried with it, which is what
// lets the one place a conversion is visible in the output - a null pointer
// constant, and the temporary a reference binds to - rewrite the line the
// operand already wrote instead of the output being built in a second pass.

namespace
{

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

// The argument-list of a call, in either of the two forms PA10 builds: a call
// written with a parenthesized expression-list, and one whose callee named a
// type, which the grammar reads as a functional cast.
const AstNode* arguments_of(const AstNode& node)
{
	const AstNode* list = child_kind(node, AstKind::ArgumentList);
	return list != nullptr ? list : child_kind(node, AstKind::ParenArgumentList);
}

std::string decimal(unsigned long long value)
{
	std::string digits;
	while (value != 0)
	{
		digits.insert(digits.begin(), static_cast<char>('0' + (value % 10)));
		value /= 10;
	}
	return digits.empty() ? std::string("0") : digits;
}

}

// 7.1.6.2 Table 10: the one-token simple-type-specifiers, which are how a
// functional cast written with a keyword names its type.  The grammar reads
// `int(x)` as a call, so the callee's spelling is what says it is a cast.
TypeId SemaAnalyzer::keyword_type(const std::string& spelling) const
{
	static const struct { const char* name; SimpleTypeSpecifier which; } kNames[] = {
		{"char", kSpecChar}, {"char16_t", kSpecChar16},
		{"char32_t", kSpecChar32}, {"wchar_t", kSpecWchar}, {"bool", kSpecBool},
		{"short", kSpecShort}, {"int", kSpecInt}, {"long", kSpecLong},
		{"signed", kSpecSigned}, {"unsigned", kSpecUnsigned},
		{"float", kSpecFloat}, {"double", kSpecDouble}, {"void", kSpecVoid}
	};
	unsigned counted[kSimpleTypeSpecifierCount] = {0};
	std::string::size_type at = 0;
	while (at <= spelling.size())
	{
		const std::string::size_type end = spelling.find(' ', at);
		const std::string word =
			spelling.substr(at, end == std::string::npos ? end : end - at);
		bool known = false;
		for (std::size_t index = 0; index < sizeof(kNames) / sizeof(kNames[0]);
		     ++index)
		{
			if (word == kNames[index].name)
			{
				++counted[kNames[index].which];
				known = true;
				break;
			}
		}
		if (!known)
		{
			return kNoType;
		}
		if (end == std::string::npos)
		{
			break;
		}
		at = end + 1;
	}
	if (!table_10_names_a_type(counted))
	{
		return kNoType;
	}
	return const_cast<TypeTable&>(types_).fundamental(table_10_type(counted));
}

const char* SemaAnalyzer::category_name(ValueCategory category)
{
	switch (category)
	{
	case ValueCategory::LValue: return "lvalue";
	case ValueCategory::XValue: return "xvalue";
	default: return "prvalue";
	}
}

std::string SemaAnalyzer::payload_of(const AstNode& node)
{
	if (node.token != kNoAstToken)
	{
		return std::string(ast_token_type_name(node.token)) + ":" + node.text;
	}
	return node.text;
}

std::string SemaAnalyzer::spell(const char* what, ValueCategory category,
                                TypeId type, const AstNode* payload) const
{
	std::string text = std::string(what) + " " + category_name(category) + " " +
		types_.description(type);
	if (payload != nullptr)
	{
		const std::string extra = payload_of(*payload);
		if (!extra.empty())
		{
			text += " " + extra;
		}
	}
	return text;
}

// 4.5: the type an integral operand is promoted to, which for a floating type
// and for a type already at least as wide as `int` is the type itself.
TypeId SemaAnalyzer::promoted(TypeId type)
{
	const TypeId bare = types_.strip_cv(type);
	if (types_.is_floating(bare))
	{
		return bare;
	}
	// 4.5p3: an unscoped enumeration is promoted through its underlying type.
	const TypeId arithmetic =
		types_.kind(bare) == TypeKind::Enum ? types_.target(bare) : bare;
	if (!types_.is_integral(arithmetic))
	{
		return bare;
	}
	const EFundamentalType fundamental = types_.fundamental_type(arithmetic);
	if (fundamental_type_size(fundamental) > 4)
	{
		return types_.strip_cv(arithmetic);
	}
	const bool fits = fundamental_type_size(fundamental) < 4 ||
		fundamental_type_class(fundamental) == FundamentalTypeClass::SignedIntegral;
	return types_.fundamental(fits ? FT_INT : FT_UNSIGNED_INT);
}

TypeId SemaAnalyzer::decayed(const Value& value)
{
	const TypeId type = value.type;
	// 4.2 and 4.3: an array is the address of its first element and a function
	// is a pointer to itself.
	if (types_.kind(type) == TypeKind::Array)
	{
		return types_.pointer_to(types_.target(type));
	}
	if (types_.kind(type) == TypeKind::Function)
	{
		return types_.pointer_to(type);
	}
	// 4.1: the prvalue an lvalue holds is not cv-qualified.
	return types_.strip_cv(type);
}

void SemaAnalyzer::require_complete_value(const Value& value)
{
	// 13.4p1: an overloaded function name has no type until a target chooses
	// between its declarations, so an operand position that offers none is the
	// one place the name means nothing.
	if (value.type == kNoType)
	{
		throw std::runtime_error("an overloaded function name is used where no "
		                         "target type chooses between its declarations");
	}
}

SemaAnalyzer::Value SemaAnalyzer::expression(const AstNode& node,
                                             const Context& ctx,
                                             DumpNode& parent)
{
	switch (node.kind)
	{
	case AstKind::ParenthesizedExpression:
		// 5.1.1p6: a parenthesized expression is the expression it holds; the
		// dump writes no node of its own for the parentheses.
		return expression(*node.children[0], ctx, parent);

	case AstKind::IdExpression:
		return id_expression(node, ctx, parent);

	case AstKind::Literal:
	case AstKind::KeywordLiteral:
		return literal_expression(node, parent);

	case AstKind::CallExpression:
		return call_expression(node, ctx, parent);

	case AstKind::SubscriptExpression:
		return subscript_expression(node, ctx, parent);

	case AstKind::MemberExpression:
		return member_expression(node, ctx, parent);

	case AstKind::UnaryExpression:
		return node.token == OP_INC || node.token == OP_DEC
			? increment_expression(node, ctx, parent, false)
			: unary_expression(node, ctx, parent);

	case AstKind::PostfixExpression:
		return increment_expression(node, ctx, parent, true);

	case AstKind::BinaryExpression:
		return binary_expression(node, ctx, parent);

	case AstKind::AssignmentExpression:
		return assignment_expression(node, ctx, parent);

	case AstKind::ConditionalExpression:
		return conditional_expression(node, ctx, parent);

	case AstKind::CastExpression:
		return cast_expression(node, ctx, parent);

	case AstKind::SizeofExpression:
		return sizeof_expression(node, ctx, parent);

	default:
		break;
	}
	throw std::runtime_error("an expression is outside the PA12 subset");
}

SemaAnalyzer::Value SemaAnalyzer::id_expression(const AstNode& node,
                                                const Context& ctx,
                                                DumpNode& parent)
{
	return named_value(node, require(resolve(node.text, ctx, LookupKind::Any),
	                                 node.text), parent);
}

SemaAnalyzer::Value SemaAnalyzer::named_value(const AstNode& node,
                                              SemaEntity& entity,
                                              DumpNode& parent)
{
	Value value;
	if (entity.kind == SemaKind::Enumerator)
	{
		// 7.2p10 and 5.19: an enumerator is a constant, and the dump writes the
		// value it stands for rather than the name it was written with.
		value.type = entity.type;
		value.spelled = entity.type;
		value.category = ValueCategory::PRValue;
		value.constant = true;
		value.value = entity.value;
		value.node = &model_.open_node(
			parent, spell("literal", value.category, value.type, nullptr) + " " +
			spell_value(entity.type, entity.value));
		return value;
	}
	if (entity.kind == SemaKind::Function)
	{
		// 13.4: the name stands for every declaration of it until a target type
		// or a call's arguments choose one, so the line is written then.
		value.functions = &entity;
		value.category = ValueCategory::LValue;
		value.node = &model_.open_node(parent, std::string());
		if (entity.next == nullptr)
		{
			// One declaration, so the name already denotes it and the line can
			// be written where it is read.
			name_function(value, entity, "id-expression");
		}
		return value;
	}
	if (entity.kind != SemaKind::Variable && entity.kind != SemaKind::Parameter)
	{
		throw std::runtime_error(node.text + " does not name an object or a "
		                         "function");
	}
	if (entity.kind == SemaKind::Variable && entity.object_member)
	{
		// 9.3.1p3 and 9.5p1: a member named with no object expression is a
		// member of the object `this` points to, or of the object an anonymous
		// union declared, and the output writes the access it stands for.
		DumpNode& line = model_.open_node(parent, std::string());
		return member_value(entity, implied_object(entity, line), entity.name,
		                    line);
	}
	value.type = entity.type;
	value.category = ValueCategory::LValue;
	if (types_.is_reference(value.type))
	{
		// 8.3.2p5: a name of reference type is an lvalue naming what it is
		// bound to, and the reference itself is not part of the expression.
		value.type = types_.target(value.type);
	}
	value.spelled = value.type;
	value.payload = &node;
	value.what = "id-expression";
	value.node = &model_.open_node(
		parent, spell("id-expression", value.category, value.type, &node));
	return value;
}

void SemaAnalyzer::name_function(Value& value, SemaEntity& function,
                                 const char* what)
{
	const std::string named = std::string(" ") +
		types_.description(function.type) + " " + function.dump_name;
	if (value.addressed != nullptr)
	{
		// 5.3.1p3 and 13.4: `&f` is a pointer to the declaration the target
		// chose, and the name under it is that declaration.
		value.addressed->text =
			std::string("id-expression ") + category_name(ValueCategory::LValue) +
			named;
		value.type = member_pointer_of(function);
		if (value.type == kNoType)
		{
			value.type = types_.pointer_to(function.type);
		}
		value.spelled = value.type;
		value.category = ValueCategory::PRValue;
		if (value.node != nullptr)
		{
			value.node->text = spell("unary-expression", value.category,
			                         value.type, value.payload);
		}
		return;
	}
	value.type = function.type;
	value.spelled = function.type;
	value.category = ValueCategory::LValue;
	if (value.node == nullptr)
	{
		return;
	}
	if (std::strcmp(what, "callee") == 0)
	{
		// The callee of a call is the one line that names a declaration before
		// its type rather than after it.
		value.node->text =
			"callee " + function.dump_name + " " + types_.description(function.type);
		return;
	}
	value.node->text = std::string(what) +
		" " + category_name(ValueCategory::LValue) + named;
}

// 5.2.5p1: `E1.E2` and `E1->E2` name a member of the class of the object
// expression, which is one lookup in the region that class declares.  The
// member is not looked up in the region the expression is written in: 3.4.5
// makes the object expression say where to look.
SemaAnalyzer::Value SemaAnalyzer::member_expression(const AstNode& node,
                                                    const Context& ctx,
                                                    DumpNode& parent)
{
	// The line the member writes holds the object expression, and what it says
	// is known only once the member is found, so the node is opened for the
	// operand to write under and spelled afterwards.
	DumpNode& line = model_.open_node(parent, std::string());
	Value object = expression(*node.children[0], ctx, line);
	require_complete_value(object);
	if (node.token == OP_ARROW)
	{
		// 5.2.5p2: `E1->E2` is `(*E1).E2`, and the dump writes the one node the
		// arrow was written as.
		if (types_.kind(object.type) != TypeKind::Pointer)
		{
			throw std::runtime_error("`->` is written on an operand that is not "
			                         "a pointer to a class");
		}
		object.type = types_.target(object.type);
		object.category = ValueCategory::LValue;
	}
	if (!types_.is_class(types_.strip_cv(object.type)))
	{
		throw std::runtime_error("a member is named of an operand that is not "
		                         "of class type");
	}
	SemaEntity* const owner = model_.type_owner(types_.strip_cv(object.type));
	if (owner == nullptr || owner->scope == nullptr)
	{
		throw std::runtime_error("a member is named of an incomplete class");
	}
	const AstNode& id = *node.children[1];
	SemaEntity& member = require(
		model_.lookup_in(*owner->scope, id.text, LookupKind::Any), id.text);
	if (member.storage != nullptr)
	{
		// 9.5p1: the member belongs to the object the anonymous union declared,
		// which is itself a member of the class named here, so the access the
		// object expression wrote holds one more - the same object a member
		// named with no object expression is reached through.
		object = member_value(*member.storage, object, member.storage->name,
		                      model_.wrap_node(*object.node, std::string()));
	}
	return member_value(member, object,
	                    std::string(ast_token_type_name(node.token)) + ":" +
	                    id.text, line);
}

// 5.2.5p4: the member the object expression holds, which is an lvalue when the
// object is one and is as cv-qualified as the object it is part of.
SemaAnalyzer::Value SemaAnalyzer::member_value(SemaEntity& member,
                                               const Value& object,
                                               const std::string& payload,
                                               DumpNode& node)
{
	if (member.kind != SemaKind::Variable)
	{
		// 5.2.5p4 gives a member function the meaning only a call of it has,
		// and 13.3.1.1.1 chooses that call among the member's declarations
		// against the object it is named on, which is outside the PA12 subset.
		throw std::runtime_error(member.name + " names a member that is not a "
		                         "data member, which PA12 does not read");
	}
	Value value;
	value.type = types_.qualified(member.type, types_.object_cv(object.type));
	if (types_.is_reference(member.type))
	{
		// 8.3.2p5: a member of reference type names what it is bound to, which
		// the object it is part of does not qualify.
		value.type = types_.target(member.type);
	}
	value.spelled = value.type;
	// 5.2.5p4: the member of a prvalue object is an xvalue; PA12 reaches a
	// member of an object the program named, which is an lvalue.
	value.category = object.category == ValueCategory::LValue
		? ValueCategory::LValue
		: ValueCategory::XValue;
	value.node = &node;
	node.text = "member-expression " + std::string(category_name(value.category)) +
		" " + types_.description(value.type) + " " + payload;
	return value;
}

// 9.3.1p3 and 9.5p1: the object a member named with no object expression is a
// member of, which is the one `this` points to, or the one an anonymous union
// declared - and that object is itself a member wherever the union was written
// in a class, so the same question is asked of it.
SemaAnalyzer::Value SemaAnalyzer::implied_object(const SemaEntity& member,
                                                 DumpNode& line)
{
	if (member.storage == nullptr)
	{
		Value object = this_value(line);
		object.type = types_.target(object.type);
		object.category = ValueCategory::LValue;
		return object;
	}
	SemaEntity& storage = *member.storage;
	if (storage.object_member)
	{
		DumpNode& inner = model_.open_node(line, std::string());
		return member_value(storage, implied_object(storage, inner),
		                    storage.name, inner);
	}
	Value object;
	object.type = storage.type;
	object.spelled = object.type;
	object.category = ValueCategory::LValue;
	object.node = &model_.open_node(
		line, "id-expression " + std::string(category_name(object.category)) +
		" " + types_.description(object.type) + " " + storage.name);
	return object;
}

// 9.3.2p1: `this` is a prvalue pointer to the object the member function being
// read was called on.  A member named with no object expression denotes the
// same object, and the output spells it the same way, so both write this line.
SemaAnalyzer::Value SemaAnalyzer::this_value(DumpNode& parent)
{
	if (self_ == nullptr)
	{
		throw std::runtime_error("`this` is written outside a member function");
	}
	Value value;
	value.type = self_->type;
	value.spelled = value.type;
	value.category = ValueCategory::PRValue;
	value.node = &model_.open_node(
		parent, "id-expression " + std::string(category_name(value.category)) +
		" " + types_.description(value.type) + " " +
		ast_token_type_name(KW_THIS) + ":this");
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::literal_expression(const AstNode& node,
                                                     DumpNode& parent)
{
	Value value;
	value.category = ValueCategory::PRValue;
	value.payload = &node;
	value.what = "literal";
	const std::string& spelling = node.text;
	if (node.kind == AstKind::KeywordLiteral)
	{
		if (node.token == KW_THIS)
		{
			// 9.3.2p1: `this` is not a literal; the grammar reads it where a
			// primary-expression is written, and it denotes an object.
			return this_value(parent);
		}
		if (node.token == KW_NULLPTR)
		{
			value.type = types_.fundamental(FT_NULLPTR_T);
		}
		else
		{
			value.type = types_.fundamental(FT_BOOL);
			value.constant = true;
			value.value = node.token == KW_TRUE ? 1 : 0;
		}
		value.spelled = value.type;
		value.node = &model_.open_node(
			parent, spell("literal", value.category, value.type, &node));
		return value;
	}

	PostToken token;
	const char first = spelling.empty() ? '\0' : spelling[0];
	if (first == '"' || (spelling.find('"') != std::string::npos))
	{
		StringLiteralSequence sequence;
		sequence.add(spelling);
		sequence.build(token);
		if (token.kind != PostTokenKind::LiteralArray)
		{
			throw std::runtime_error("a string literal is outside the PA12 subset");
		}
		// 2.14.5p8: a string literal is an lvalue array of n const characters.
		value.type = types_.array_of(
			types_.qualified(types_.fundamental(token.type), kCvConst), true,
			token.element_count);
		value.category = ValueCategory::LValue;
		value.spelled = value.type;
		value.node = &model_.open_node(
			parent, spell("literal", value.category, value.type, &node));
		return value;
	}
	if (first >= '0' && first <= '9')
	{
		scan_pp_number(spelling, token);
	}
	else
	{
		scan_character_literal(spelling, token);
	}
	if (token.kind != PostTokenKind::Literal)
	{
		throw std::runtime_error("a literal is outside the PA12 subset");
	}
	value.type = types_.fundamental(token.type);
	value.spelled = value.type;
	if (fundamental_type_is_integral(token.type))
	{
		value.constant = true;
		value.value = 0;
		for (std::size_t index = token.data.size(); index-- > 0;)
		{
			value.value = (value.value << 8) |
				static_cast<unsigned char>(token.data[index]);
		}
		// 4.10p1: a null pointer constant is an integral constant expression
		// that evaluates to zero.
		value.null_constant = value.value == 0;
	}
	value.node = &model_.open_node(
		parent, spell("literal", value.category, value.type, &node));
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::sizeof_expression(const AstNode& node,
                                                    const Context& ctx,
                                                    DumpNode& parent)
{
	// 5.3.3p6: the result is a prvalue of type `std::size_t`, and the operand
	// is unevaluated, so the dump writes the node alone.
	const TypeId result = types_.fundamental(FT_UNSIGNED_LONG_INT);
	Value value;
	value.type = result;
	value.spelled = result;
	value.category = ValueCategory::PRValue;
	value.constant = true;
	const AstNode& operand = *node.children[0];
	if (operand.kind == AstKind::TypeId)
	{
		value.value = size_of(type_id_type(operand, ctx));
	}
	else
	{
		// The operand is unevaluated, so nothing it names is written, but it is
		// still looked up: 5.3.3p1 needs its type.
		DumpNode scratch;
		const Value read = expression(operand, ctx, scratch);
		require_complete_value(read);
		value.value = size_of(read.type);
	}
	value.node = &model_.open_node(
		parent, spell("sizeof-expression", value.category, result, nullptr));
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::subscript_expression(const AstNode& node,
                                                       const Context& ctx,
                                                       DumpNode& parent)
{
	DumpNode& line = model_.open_node(parent, std::string());
	const Value left = expression(*node.children[0], ctx, line);
	const Value right = expression(*node.children[1], ctx, line);
	require_complete_value(left);
	require_complete_value(right);
	// 5.2.1p1: one operand is a pointer to a completely-defined object type and
	// the other an unscoped enumeration or integral type; `a[b]` is `*(a + b)`.
	const TypeId left_type = decayed(left);
	const TypeId right_type = decayed(right);
	const bool left_is_pointer = types_.is_object_pointer(left_type);
	const TypeId pointer = left_is_pointer ? left_type : right_type;
	const TypeId index = left_is_pointer ? right_type : left_type;
	if (!types_.is_object_pointer(pointer) || !types_.is_integral(index) ||
	    types_.is_scoped_enum(index))
	{
		throw std::runtime_error("a subscript expression has no pointer and "
		                         "integral operand");
	}
	if (!left_is_pointer && line.children.size() == 2)
	{
		// 5.2.1p1: `E1[E2]` is `*((E1)+(E2))`, which is the same expression
		// however the two operands were written, so the dump writes the
		// pointer operand first.
		DumpNode* const held = line.children[0];
		line.children[0] = line.children[1];
		line.children[1] = held;
	}
	Value value;
	value.type = types_.target(pointer);
	value.spelled = value.type;
	value.category = ValueCategory::LValue;
	value.node = &line;
	line.text = spell("subscript-expression", value.category, value.type, nullptr);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::cast_expression(const AstNode& node,
                                                  const Context& ctx,
                                                  DumpNode& parent)
{
	const AstNode* type_id = child_kind(node, AstKind::TypeId);
	if (type_id == nullptr)
	{
		throw std::runtime_error("a cast expression names no type");
	}
	const TypeId target = type_id_type(*type_id, ctx);
	DumpNode& line = model_.open_node(parent, std::string());
	const AstNode& operand = *node.children[node.children.size() - 1];
	Value source = expression(operand, ctx, line);

	Value value;
	value.type = target;
	value.spelled = target;
	value.category = ValueCategory::PRValue;
	if (types_.is_reference(target))
	{
		// 5.2.9p1: a cast to an lvalue reference is an lvalue and one to an
		// rvalue reference to an object type is an xvalue, and the operand
		// itself is what the result names.
		value.type = types_.target(target);
		value.category = types_.kind(target) == TypeKind::LValueReference
			? ValueCategory::LValue
			: ValueCategory::XValue;
		value.spelled = target;
		if (source.node != nullptr && source.what != nullptr)
		{
			// 5.2.9p1: the cast names the same object, so the operand's own
			// line is rewritten with what the cast made of it.
			source.node->text =
				spell(source.what, value.category, target, source.payload);
		}
		// The cast writes no node of its own: the operand's line already says
		// what the cast made of it.
		parent.children.pop_back();
		for (std::size_t index = 0; index < line.children.size(); ++index)
		{
			parent.children.push_back(line.children[index]);
		}
		value.node = source.node;
		return value;
	}
	if (source.type == kNoType && source.functions != nullptr)
	{
		SemaEntity* chosen = resolve_target(source, target);
		if (chosen == nullptr)
		{
			throw std::runtime_error("no declaration of an overloaded function "
			                         "name has the type a cast asks for");
		}
		name_function(source, *chosen, "id-expression");
	}
	if (types_.kind(types_.strip_cv(target)) == TypeKind::MemberPointer &&
	    source.type == types_.strip_cv(target))
	{
		// 5.2.9p2: a cast to the type the operand has converts nothing, and a
		// pointer to member holds which member it names rather than an address,
		// so there is nothing for the output to write around the operand.
		parent.children.pop_back();
		for (std::size_t index = 0; index < line.children.size(); ++index)
		{
			parent.children.push_back(line.children[index]);
		}
		return source;
	}
	line.text = spell("cast-expression", value.category, target,
	                  node.token == kNoAstToken ? nullptr : &node);
	value.node = &line;
	return value;
}

// 5.19 and the course builtins: `__builtin_constant_p` answers whether its
// operand is one of the constants the translation propagates, and
// `__builtin_abort` is a zero-argument call PA12 recognises without lowering.
bool SemaAnalyzer::builtin_call(const std::string& name, const AstNode& node,
                                const Context& ctx, DumpNode& parent,
                                Value& out)
{
	if (name == "__builtin_constant_p")
	{
		const AstNode* list = arguments_of(node);
		bool known = false;
		if (list != nullptr && !list->children.empty())
		{
			DumpNode scratch;
			try
			{
				evaluate(*list->children[0], ctx);
				known = true;
			}
			catch (const NotConstant&)
			{
				known = false;
			}
			// The operand is unevaluated but still has to name what it names.
			if (!known)
			{
				expression(*list->children[0], ctx, scratch);
			}
		}
		out = Value();
		out.type = types_.fundamental(FT_INT);
		out.spelled = out.type;
		out.constant = true;
		out.value = known ? 1 : 0;
		out.node = &model_.open_node(
			parent, spell("literal", ValueCategory::PRValue, out.type, nullptr) +
			" " + decimal(out.value));
		return true;
	}
	if (name != "__builtin_abort")
	{
		return false;
	}
	const TypeId type = types_.function_of(types_.fundamental(FT_VOID),
	                                       std::vector<TypeId>(), false);
	out = Value();
	out.type = types_.fundamental(FT_VOID);
	out.spelled = out.type;
	out.node = &model_.open_node(
		parent, spell("call-expression", ValueCategory::PRValue, out.type, nullptr));
	model_.open_node(*out.node,
	                 "callee __builtin_abort " + types_.description(type));
	return true;
}

// 5p9: the type the usual arithmetic conversions bring two arithmetic operands
// to.  A floating operand decides for both; otherwise both are promoted and the
// wider, or the unsigned of two of one width, wins.
TypeId SemaAnalyzer::arithmetic_result(TypeId left, TypeId right)
{
	const TypeId a = promoted(left);
	const TypeId b = promoted(right);
	if (types_.is_floating(a) || types_.is_floating(b))
	{
		if (!types_.is_floating(a))
		{
			return b;
		}
		if (!types_.is_floating(b))
		{
			return a;
		}
		return types_.object_size(a) >= types_.object_size(b) ? a : b;
	}
	return common_type(a, b);
}

// 5.3.1p3: a pointer to a data member is formed only where `&` is written on a
// qualified-id, which names the member of its class rather than a member of any
// object - so the operand is not read as the access a name of it stands for.
// Anything else `&` is written on names an object or a function.
SemaAnalyzer::Value SemaAnalyzer::member_address(const AstNode& node,
                                                 const Context& ctx,
                                                 DumpNode& parent)
{
	Value value;
	const AstNode& written = *node.children[0];
	if (written.kind != AstKind::IdExpression ||
	    !QualifiedName(written.text).qualified())
	{
		return value;
	}
	SemaEntity* const entity = resolve(written.text, ctx, LookupKind::Any);
	if (entity == nullptr || entity->kind != SemaKind::Variable ||
	    !entity->object_member || entity->region->owner == nullptr)
	{
		// A qualified name of anything else - a static member, a variable of a
		// namespace, a function - is the operand `&` reads it as.
		return value;
	}
	DumpNode& line = model_.open_node(parent, std::string());
	model_.open_node(line, "id-expression " +
	                 std::string(category_name(ValueCategory::LValue)) + " " +
	                 types_.description(entity->type) + " " + entity->dump_name);
	value.type = types_.member_pointer_to(entity->region->owner->type,
	                                      entity->type);
	value.spelled = value.type;
	value.category = ValueCategory::PRValue;
	value.node = &line;
	line.text = spell("unary-expression", value.category, value.type, &node);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::unary_expression(const AstNode& node,
                                                   const Context& ctx,
                                                   DumpNode& parent)
{
	if (node.token == OP_AMP)
	{
		const Value member = member_address(node, ctx, parent);
		if (member.type != kNoType)
		{
			return member;
		}
	}
	DumpNode& line = model_.open_node(parent, std::string());
	const Value operand = expression(*node.children[0], ctx, line);
	Value value;
	value.category = ValueCategory::PRValue;

	switch (node.token)
	{
	case OP_AMP:
		if (operand.type == kNoType && operand.functions != nullptr)
		{
			// 13.4p1: `&f` is one of the contexts a target type resolves an
			// overloaded name in, so the set travels up as it does through the
			// name itself and both lines are written where it is chosen.
			value = operand;
			value.node = &line;
			value.addressed = operand.node;
			value.payload = &node;
			return value;
		}
		// 5.3.1p3: the result is a pointer to the object or function the
		// operand names, so the operand has to name one.
		require_complete_value(operand);
		if (operand.category != ValueCategory::LValue)
		{
			throw std::runtime_error("the operand of unary & is not an lvalue");
		}
		value.type = operand.functions == nullptr
			? kNoType
			: member_pointer_of(*operand.functions);
		if (value.type == kNoType)
		{
			value.type = types_.pointer_to(operand.type);
		}
		break;

	case OP_STAR:
	{
		// 5.3.1p1: the operand is a pointer, and the result is the lvalue it
		// points to.
		require_complete_value(operand);
		const TypeId pointer = decayed(operand);
		if (types_.kind(pointer) != TypeKind::Pointer)
		{
			throw std::runtime_error("the operand of unary * is not a pointer");
		}
		value.type = types_.target(pointer);
		value.category = ValueCategory::LValue;
		break;
	}

	case OP_LNOT:
		require_complete_value(operand);
		if (!types_.contextually_bool(operand.type))
		{
			throw std::runtime_error("the operand of ! has no conversion to bool");
		}
		value.type = types_.fundamental(FT_BOOL);
		break;

	case OP_COMPL:
		require_complete_value(operand);
		if (!types_.is_integral(operand.type) ||
		    types_.is_scoped_enum(operand.type))
		{
			throw std::runtime_error("the operand of ~ is not integral");
		}
		value.type = promoted(operand.type);
		break;

	case OP_PLUS:
	case OP_MINUS:
		require_complete_value(operand);
		if (node.token == OP_PLUS && types_.is_object_pointer(decayed(operand)))
		{
			value.type = decayed(operand);
			break;
		}
		if (!types_.is_arithmetic(types_.strip_cv(operand.type)) &&
		    !(types_.kind(operand.type) == TypeKind::Enum &&
		      !types_.is_scoped_enum(operand.type)))
		{
			throw std::runtime_error("the operand of unary + or - is not "
			                         "arithmetic");
		}
		value.type = promoted(operand.type);
		break;

	default:
		throw std::runtime_error("a unary operator is outside the PA12 subset");
	}

	value.spelled = value.type;
	value.node = &line;
	line.text = spell("unary-expression", value.category, value.type, &node);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::increment_expression(const AstNode& node,
                                                       const Context& ctx,
                                                       DumpNode& parent,
                                                       bool postfix)
{
	DumpNode& line = model_.open_node(parent, std::string());
	const Value operand = expression(*node.children[0], ctx, line);
	require_complete_value(operand);
	// 5.2.6p1 and 5.3.2p1: the operand is a modifiable lvalue of arithmetic or
	// pointer type, and `--` does not take a `bool`.
	if (operand.category != ValueCategory::LValue ||
	    (types_.cv(operand.type) & kCvConst) != 0)
	{
		throw std::runtime_error("the operand of ++ or -- is not a modifiable "
		                         "lvalue");
	}
	const TypeId bare = types_.strip_cv(operand.type);
	if (!types_.is_arithmetic(bare) && !types_.is_object_pointer(bare))
	{
		throw std::runtime_error("the operand of ++ or -- is neither arithmetic "
		                         "nor a pointer");
	}
	// 5.2.6p1 and 5.3.2p1: the operand of `--` shall not be of type bool.
	if (node.token == OP_DEC && types_.fundamental_type(bare) == FT_BOOL &&
	    types_.kind(bare) == TypeKind::Fundamental)
	{
		throw std::runtime_error("the operand of -- is a bool");
	}
	Value value;
	value.type = postfix ? bare : operand.type;
	value.spelled = value.type;
	value.category = postfix ? ValueCategory::PRValue : ValueCategory::LValue;
	value.node = &line;
	line.text = spell(postfix ? "postfix-expression" : "unary-expression",
	                  value.category, value.type, &node);
	return value;
}

// 5.9p2: the composite pointer type of two operands, which is what a
// comparison brings them both to.  A null pointer constant takes the other
// operand's type, and two object pointers whose pointees differ only in
// cv-qualification meet at the union of those qualifiers.
TypeId SemaAnalyzer::composite_pointer(const Value& left, const Value& right)
{
	const TypeId a = decayed(left);
	const TypeId b = decayed(right);
	const bool a_pointer = types_.kind(a) == TypeKind::Pointer;
	const bool b_pointer = types_.kind(b) == TypeKind::Pointer;
	const bool a_null = left.null_constant ||
		types_.fundamental_type(a) == FT_NULLPTR_T;
	const bool b_null = right.null_constant ||
		types_.fundamental_type(b) == FT_NULLPTR_T;
	if (a_pointer && b_null)
	{
		return a;
	}
	if (b_pointer && a_null)
	{
		return b;
	}
	if (!a_pointer || !b_pointer)
	{
		return kNoType;
	}
	if (a == b)
	{
		return a;
	}
	const TypeId left_pointee = types_.target(a);
	const TypeId right_pointee = types_.target(b);
	const unsigned cv = types_.cv(left_pointee) | types_.cv(right_pointee);
	if (types_.strip_cv(left_pointee) == types_.strip_cv(right_pointee))
	{
		return types_.pointer_to(types_.qualified(left_pointee, cv));
	}
	// 4.10p2: an object pointer converts to `cv void*`, which is where two
	// pointers to unrelated object types meet.
	if (types_.is_void(left_pointee) && types_.is_object_pointer(b))
	{
		return types_.pointer_to(types_.qualified(left_pointee, cv));
	}
	if (types_.is_void(right_pointee) && types_.is_object_pointer(a))
	{
		return types_.pointer_to(types_.qualified(right_pointee, cv));
	}
	return kNoType;
}

SemaAnalyzer::Value SemaAnalyzer::binary_expression(const AstNode& node,
                                                    const Context& ctx,
                                                    DumpNode& parent)
{
	DumpNode& line = model_.open_node(parent, std::string());
	const Value left = expression(*node.children[0], ctx, line);
	const Value right = expression(*node.children[1], ctx, line);

	Value value;
	value.category = ValueCategory::PRValue;
	if (node.token == OP_COMMA)
	{
		// 5.18p1: the result is the right operand, and it keeps its category.
		// The left one is discarded, which is still no target for 13.4 to
		// resolve an overloaded name against.
		require_complete_value(left);
		require_complete_value(right);
		value.type = right.type;
		value.category = right.category;
		value.spelled = right.spelled;
	}
	else
	{
		require_complete_value(left);
		require_complete_value(right);
		value.type = binary_result(node.token, left, right);
		value.spelled = value.type;
	}
	value.node = &line;
	line.text = spell("binary-expression", value.category, value.spelled, &node);
	return value;
}

// 5.6 to 5.15: the type each built-in binary operator gives its operands.
TypeId SemaAnalyzer::binary_result(unsigned op, const Value& left,
                                   const Value& right)
{
	const TypeId a = decayed(left);
	const TypeId b = decayed(right);
	const bool arithmetic_operands =
		(types_.is_arithmetic(a) || types_.kind(a) == TypeKind::Enum) &&
		(types_.is_arithmetic(b) || types_.kind(b) == TypeKind::Enum) &&
		!types_.is_scoped_enum(a) && !types_.is_scoped_enum(b);

	switch (op)
	{
	case OP_LAND:
	case OP_LOR:
		// 5.14 and 5.15: both operands are contextually converted to bool.
		if (!types_.contextually_bool(a) || !types_.contextually_bool(b))
		{
			throw std::runtime_error("an operand of && or || has no conversion "
			                         "to bool");
		}
		return types_.fundamental(FT_BOOL);

	case OP_EQ:
	case OP_NE:
	case OP_LT:
	case OP_GT:
	case OP_LE:
	case OP_GE:
	{
		// 5.9p2 and 5.10p1: two operands of one enumeration type are compared
		// as they are, which is the only way a scoped enumeration is compared
		// at all; otherwise the usual arithmetic conversions bring two
		// arithmetic or unscoped enumeration operands to one type.  Two
		// pointers are compared after conversion to their composite type.
		if (types_.kind(a) == TypeKind::Enum && a == b)
		{
			return types_.fundamental(FT_BOOL);
		}
		if (arithmetic_operands)
		{
			return types_.fundamental(FT_BOOL);
		}
		if (types_.fundamental_type(a) == FT_NULLPTR_T &&
		    types_.fundamental_type(b) == FT_NULLPTR_T &&
		    (op == OP_EQ || op == OP_NE))
		{
			return types_.fundamental(FT_BOOL);
		}
		if (composite_pointer(left, right) == kNoType)
		{
			throw std::runtime_error("a comparison has operands of unrelated "
			                         "types");
		}
		return types_.fundamental(FT_BOOL);
	}

	case OP_PLUS:
	case OP_MINUS:
	{
		// 5.7p1: a pointer and an integral operand, or two pointers to the same
		// object type for `-`.
		const bool a_pointer = types_.is_object_pointer(a);
		const bool b_pointer = types_.is_object_pointer(b);
		if (a_pointer && b_pointer && op == OP_MINUS)
		{
			if (types_.strip_cv(types_.target(a)) !=
			    types_.strip_cv(types_.target(b)))
			{
				throw std::runtime_error("two pointers to different types are "
				                         "subtracted");
			}
			return types_.fundamental(FT_LONG_INT);
		}
		if (a_pointer && types_.is_integral(b) && !types_.is_scoped_enum(b))
		{
			return a;
		}
		if (b_pointer && types_.is_integral(a) && !types_.is_scoped_enum(a) &&
		    op == OP_PLUS)
		{
			return b;
		}
		if (!arithmetic_operands)
		{
			throw std::runtime_error("an operand of + or - is neither arithmetic "
			                         "nor a pointer");
		}
		return arithmetic_result(a, b);
	}

	case OP_STAR:
	case OP_DIV:
		if (!arithmetic_operands)
		{
			throw std::runtime_error("an operand of * or / is not arithmetic");
		}
		return arithmetic_result(a, b);

	case OP_MOD:
	case OP_AMP:
	case OP_BOR:
	case OP_XOR:
		if (!arithmetic_operands || !types_.is_integral(a) ||
		    !types_.is_integral(b))
		{
			throw std::runtime_error("an operand of an integral operator is not "
			                         "integral");
		}
		return arithmetic_result(a, b);

	case OP_LSHIFT:
	case OP_RSHIFT:
		// 5.8p1: the operands are promoted separately and the result has the
		// type of the promoted left operand.
		if (!arithmetic_operands || !types_.is_integral(a) ||
		    !types_.is_integral(b))
		{
			throw std::runtime_error("an operand of a shift is not integral");
		}
		return promoted(a);

	default:
		break;
	}
	throw std::runtime_error("a binary operator is outside the PA12 subset");
}

SemaAnalyzer::Value SemaAnalyzer::assignment_expression(const AstNode& node,
                                                        const Context& ctx,
                                                        DumpNode& parent)
{
	DumpNode& line = model_.open_node(parent, std::string());
	const Value left = expression(*node.children[0], ctx, line);
	require_complete_value(left);
	// 5.17p1: the left operand is a modifiable lvalue and the result is that
	// lvalue.
	if (left.category != ValueCategory::LValue ||
	    (types_.cv(left.type) & kCvConst) != 0)
	{
		throw std::runtime_error("the left operand of an assignment is not a "
		                         "modifiable lvalue");
	}

	if (node.token == OP_ASS)
	{
		initialize(*node.children[1], types_.strip_cv(left.type), ctx, line);
	}
	else
	{
		const Value right = expression(*node.children[1], ctx, line);
		require_complete_value(right);
		// 5.17p7: a compound assignment behaves as the operator it names
		// followed by an assignment, so the operator's own rules decide
		// whether the operands go together.
		Value target = left;
		target.type = types_.strip_cv(left.type);
		Value result;
		result.type = binary_result(compound_operator(node.token), target, right);
		result.spelled = result.type;
		// 5.17p7: the result is then assigned to the left operand, so it has to
		// be a value the left operand can hold.
		if (!match_by_value(result, target.type).viable)
		{
			throw std::runtime_error("the result of a compound assignment has "
			                         "no conversion to the type it assigns to");
		}
	}

	Value value;
	value.type = left.type;
	value.spelled = value.type;
	value.category = ValueCategory::LValue;
	value.node = &line;
	line.text = spell("assignment-expression", value.category, value.type, &node);
	return value;
}

// 5.17p7: the built-in operator a compound assignment is written from.
unsigned SemaAnalyzer::compound_operator(unsigned op)
{
	switch (op)
	{
	case OP_PLUSASS: return OP_PLUS;
	case OP_MINUSASS: return OP_MINUS;
	case OP_STARASS: return OP_STAR;
	case OP_DIVASS: return OP_DIV;
	case OP_MODASS: return OP_MOD;
	case OP_XORASS: return OP_XOR;
	case OP_BANDASS: return OP_AMP;
	case OP_BORASS: return OP_BOR;
	case OP_LSHIFTASS: return OP_LSHIFT;
	case OP_RSHIFTASS: return OP_RSHIFT;
	default: break;
	}
	throw std::runtime_error("an assignment operator is outside the PA12 subset");
}

SemaAnalyzer::Value SemaAnalyzer::conditional_expression(const AstNode& node,
                                                         const Context& ctx,
                                                         DumpNode& parent)
{
	DumpNode& line = model_.open_node(parent, std::string());
	const Value condition_value = expression(*node.children[0], ctx, line);
	require_complete_value(condition_value);
	if (!types_.contextually_bool(condition_value.type))
	{
		throw std::runtime_error("the condition of ?: has no conversion to bool");
	}
	const Value left = expression(*node.children[1], ctx, line);
	const Value right = expression(*node.children[2], ctx, line);
	require_complete_value(left);
	require_complete_value(right);

	Value value;
	value.category = ValueCategory::PRValue;
	// 5.16p4: two lvalues of the same type make the result an lvalue.
	if (left.category == ValueCategory::LValue &&
	    right.category == ValueCategory::LValue && left.type == right.type)
	{
		value.type = left.type;
		value.category = ValueCategory::LValue;
	}
	else if (types_.strip_cv(decayed(left)) == types_.strip_cv(decayed(right)))
	{
		value.type = types_.strip_cv(decayed(left));
	}
	else if ((types_.is_arithmetic(decayed(left)) ||
	          types_.kind(decayed(left)) == TypeKind::Enum) &&
	         (types_.is_arithmetic(decayed(right)) ||
	          types_.kind(decayed(right)) == TypeKind::Enum))
	{
		// 5.16p5: the usual arithmetic conversions bring two arithmetic
		// operands to one type.
		value.type = arithmetic_result(decayed(left), decayed(right));
	}
	else
	{
		value.type = composite_pointer(left, right);
		if (value.type == kNoType)
		{
			throw std::runtime_error("the operands of ?: have no common type");
		}
	}
	value.spelled = value.type;
	value.node = &line;
	line.text = spell("conditional-expression", value.category, value.type,
	                  nullptr);
	return value;
}
