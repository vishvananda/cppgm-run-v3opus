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
                                TypeId type, const std::string& payload) const
{
	std::string text = std::string(what) + " " + category_name(category) + " " +
		types_.description(type);
	if (!payload.empty())
	{
		text += " " + payload;
	}
	return text;
}

std::string SemaAnalyzer::spell(const char* what, ValueCategory category,
                                TypeId type, const AstNode* payload) const
{
	return spell(what, category, type,
	             payload == nullptr ? std::string() : payload_of(*payload));
}

void SemaAnalyzer::respell(const Value& value) const
{
	// The type the line carries is what the dump writes, which parts company
	// with the type the operators see wherever a reference is visible in the
	// result of an expression.
	value.node->text =
		spell(value.what, value.category, value.spelled, value.payload);
	record(value);
}

// The line kinds the expression layer spells, as the node kinds the resolved
// tree names them by.  The spelling is the one fact that says which construct
// a line stands for, so the two are decided in one place.
FactKind SemaAnalyzer::fact_kind(const char* what)
{
	static const struct { const char* name; FactKind kind; } kKinds[] = {
		{"literal", FactKind::Literal},
		{"id-expression", FactKind::Id},
		{"member-expression", FactKind::Member},
		{"call-expression", FactKind::Call},
		{"unary-expression", FactKind::Unary},
		{"postfix-expression", FactKind::Postfix},
		{"binary-expression", FactKind::Binary},
		{"assignment-expression", FactKind::Assignment},
		{"conditional-expression", FactKind::Conditional},
		{"subscript-expression", FactKind::Subscript},
		{"cast-expression", FactKind::Cast},
		{"base-conversion", FactKind::BaseConversion},
		{"temporary-object", FactKind::TemporaryObject},
		{"new-expression", FactKind::NewExpression},
		{"sizeof-expression", FactKind::Sizeof}
	};
	for (std::size_t index = 0; index < sizeof(kKinds) / sizeof(kKinds[0]);
	     ++index)
	{
		if (std::strcmp(what, kKinds[index].name) == 0)
		{
			return kKinds[index].kind;
		}
	}
	return FactKind::None;
}

// 2.14.4p1: the zero of a floating type, spelled as a literal of that type.
// The suffix is what says which of the three widths the value is one of, and a
// value-initialization that left it off would be a value of `double` wherever
// it was written - which is what the object file would then carry.  It is the
// one floating value this translation spells for itself; every other one is the
// digits the program wrote, kept on the node beside the fact.
const char* SemaAnalyzer::floating_zero(TypeId type) const
{
	if (!types_.is_floating(type))
	{
		return "0";
	}
	switch (types_.fundamental_type(type))
	{
	case FT_FLOAT: return "0.0F";
	case FT_LONG_DOUBLE: return "0.0L";
	default: return "0.0";
	}
}

void SemaAnalyzer::record(const Value& value) const
{
	if (!lowering() || value.node == nullptr || value.what == nullptr)
	{
		return;
	}
	SemaFact& fact = value.node->fact;
	fact.kind = fact_kind(value.what);
	fact.type = value.type;
	fact.spelled = value.spelled;
	fact.category = value.category;
	fact.op = value.op;
	fact.operands = value.operands;
	fact.entity = value.entity;
	fact.constant = value.constant;
	fact.value = value.value;
	if (fact.kind == FactKind::Literal &&
	    types_.is_floating(const_cast<TypeTable&>(types_).strip_cv(value.type)))
	{
		fact.constant = true;
		fact.spelling = value.payload;
	}
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
	// 4.5p3: an unscoped enumeration is promoted to the first type that
	// represents every value it has, which its declaration worked out once the
	// enumerators were read.  An enumeration whose values all fit in `int` is
	// promoted through its underlying type, as every other integral type is.
	if (types_.kind(bare) == TypeKind::Enum)
	{
		const SemaEntity* const declared = model_.type_owner(bare);
		if (declared != nullptr && declared->promotion != kNoType)
		{
			return declared->promotion;
		}
	}
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
	const ParseDepth::Frame frame(depth_);
	if (frame.overflowed())
	{
		throw std::runtime_error("an expression nests deeper than the analysis "
		                         "reads");
	}
	// 11.2: the region this expression was written in, which is where the
	// access a class gave a member or a base class is asked about.  Every
	// expression is read against one region, so it is carried here rather than
	// threaded through the conversions and accesses that ask.  A subexpression
	// read against another region - a default member initializer, a default
	// argument - restores this one when it is done.
	Scope* const enclosing = reading_;
	reading_ = ctx.scope;
	Value value = dispatch_expression(node, ctx, parent);
	reading_ = enclosing;
	// Every expression the layer reads leaves here, so this is the one place
	// the facts of a line that was spelled where it was first written - a
	// literal, a name - are recorded.  The forms that respell their line have
	// already recorded it, and recording the same facts again writes the same
	// ones.
	record(value);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::dispatch_expression(const AstNode& node,
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
		return literal_expression(node, ctx, parent);

	case AstKind::CallExpression:
		return call_expression(node, ctx, parent);

	case AstKind::NewExpression:
		return new_expression(node, ctx, parent);

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

	case AstKind::TypeTraitExpression:
		if (node.token == KW_ALIGNOF)
		{
			return alignof_expression(node, ctx, parent);
		}
		break;

	default:
		break;
	}
	throw std::runtime_error("an expression is outside the PA12 subset");
}

SemaAnalyzer::Value SemaAnalyzer::id_expression(const AstNode& node,
                                                const Context& ctx,
                                                DumpNode& parent)
{
	std::vector<SemaEntity*>& found = model_.open_overloads();
	if (child_kind(node, AstKind::CarriedExpression) != nullptr)
	{
		// 7.1.6.2p1: the nested-name-specifier begins with a
		// decltype-specifier, so what says which region the rest of the name is
		// looked up in is the expression the parser kept beside it.  A static
		// data member, an enumerator and a static member function are each
		// named this way, and each is the declaration that region holds.
		return named_value(node,
		                   require(decltype_qualified_name(node, ctx,
		                                                   LookupKind::Any,
		                                                   &found),
		                           node.text),
		                   parent, &found);
	}
	// 14.2: a template-id denotes the specializations its argument list makes
	// rather than a declaration bound to the whole spelling, so the template
	// layer answers before ordinary lookup is asked.
	SemaEntity* named = template_specializations(node.text, ctx, found);
	if (named == nullptr)
	{
		named = resolve(node.text, ctx, LookupKind::Any, &found);
	}
	if (named == nullptr)
	{
		// 1.4p8: the name is one the implementation reserves for a function of
		// its own, so what the program did not declare the implementation
		// declares here.  A call is not the only use of such a name: 13.4's
		// address of it and 4.3's conversion of that address reach it as an
		// id-expression, and each has to find the one declaration the reserved
		// function has.
		named = reserved_function(node.text, &found);
	}
	return named_value(node, require(named, node.text), parent, &found);
}

SemaAnalyzer::Value SemaAnalyzer::named_value(const AstNode& node,
                                              SemaEntity& named,
                                              DumpNode& parent,
                                              const std::vector<SemaEntity*>* found)
{
	// 7.3.3p1: a using-declaration made this class declare what its base
	// declared, and what the name denotes is the base's declaration - the same
	// object, with the same address and the same name in the object file.  What
	// the class declared is what 13.3 ranks and 11p1 gave an access to, so the
	// two part company only here.
	SemaEntity& entity = declared_member(named);
	Value value;
	if (entity.kind == SemaKind::Enumerator ||
	    (entity.kind == SemaKind::Variable && entity.constant &&
	     !entity.object_definition && entity.region != nullptr &&
	     entity.region->kind == ScopeKind::Class))
	{
		// 7.2p10 and 5.19: an enumerator is a constant, and the dump writes the
		// value it stands for rather than the name it was written with.  9.4.2p3
		// gives a static data member of const integral type initialized in its
		// class the same meaning: the class declared it and defined nothing, so
		// the program has its value and no object holding it.
		value.type = entity.type;
		value.spelled = entity.type;
		value.category = ValueCategory::PRValue;
		value.constant = true;
		value.value = entity.value;
		value.entity = &entity;
		value.what = "literal";
		value.payload = spell_value(entity.type, entity.value);
		value.node = &model_.open_node(
			parent, spell(value.what, value.category, value.type, value.payload));
		return value;
	}
	if (entity.kind == SemaKind::Function)
	{
		// 13.4: the name stands for every declaration of it until a target type
		// or a call's arguments choose one, so the line is written then.
		std::vector<SemaEntity*>* set = nullptr;
		if (found != nullptr && !found->empty())
		{
			value.functions = found;
		}
		else
		{
			set = &model_.open_overloads();
			// 13.3.3.1p4 ranks what a using-declaration brought into a class as
			// a member of that class, so the candidate is the declaration the
			// class made and the call resolves it to the one it names.
			set->push_back(&named);
			value.functions = set;
		}
		value.category = ValueCategory::LValue;
		value.name = &node;
		value.node = &model_.open_node(parent, std::string());
		if (value.functions->size() == 1 && named.next == nullptr &&
		    named.template_parameters == nullptr)
		{
			// One declaration, so the name already denotes it and the line can
			// be written where it is read.  14p1 leaves a template denoting no
			// function until a call deduces one, so its line waits for that.
			// 7.3.3p1: what a using-declaration brought into a class is one
			// declaration of this class naming the base's, and the line names
			// the one the call runs.
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
		// 7.3.3p1: 11.2p5's naming class is the class the using-declaration was
		// written in rather than the base that declared the member, which
		// leaves the base-specifier's own access unasked about.
		DumpNode& line = model_.open_node(parent, std::string());
		return member_value(entity, implied_object(entity, line), entity.name,
		                    line, named.shadowed == nullptr);
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
	value.entity = &entity;
	value.what = "id-expression";
	value.payload = payload_of(node);
	value.node = &model_.open_node(
		parent, spell(value.what, value.category, value.type, value.payload));
	record(value);
	return value;
}

void SemaAnalyzer::name_function(Value& value, SemaEntity& selected,
                                 const char* what)
{
	// 7.3.3p1: 13.3 chose among the declarations the class made, and one a
	// using-declaration made names the base's.  Naming a function is a use of
	// it - the body a call runs, the address `&` takes, the symbol the object
	// file holds - and every use reaches the declaration the base wrote, so the
	// two part company here and nowhere below.
	SemaEntity& function = declared_member(selected);
	if (function.primary != nullptr)
	{
		// 14.7.1p1: choosing a specialization is what asks for it, and the
		// declaration it stands for is written once however often it is named.
		instantiate(function);
	}
	// An id-expression writes the name as the program spelled it; a callee
	// writes the one its declaration has.  The two part company wherever a
	// lookup crossed a region - a using-directive, a template-id - and a
	// declaration reached under one name is written under another.
	const std::string named =
		value.name != nullptr ? payload_of(*value.name) : function.dump_name;
	if (value.addressed != nullptr)
	{
		// 5.3.1p3 and 13.4: `&f` is a pointer to the declaration the target
		// chose, and the name under it is that declaration.
		value.addressed->text = spell("id-expression", ValueCategory::LValue,
		                              function.type, named);
		value.addressed->fact.kind = FactKind::Id;
		value.addressed->fact.type = function.type;
		value.addressed->fact.spelled = function.type;
		value.addressed->fact.category = ValueCategory::LValue;
		value.addressed->fact.entity = &function;
		value.type = member_pointer_of(function);
		if (value.type == kNoType)
		{
			value.type = types_.pointer_to(function.type);
		}
		value.spelled = value.type;
		value.category = ValueCategory::PRValue;
		value.what = "unary-expression";
		if (value.node != nullptr)
		{
			respell(value);
		}
		return;
	}
	value.type = function.type;
	value.spelled = function.type;
	value.category = ValueCategory::LValue;
	value.entity = &function;
	if (value.node == nullptr)
	{
		return;
	}
	if (std::strcmp(what, "callee") == 0)
	{
		// The callee of a call is the one line that names a declaration before
		// its type rather than after it, so it is the one line `spell` does not
		// write and the one a conversion is never written around.
		value.node->text =
			"callee " + function.dump_name + " " + types_.description(function.type);
		value.node->fact.kind = FactKind::Callee;
		value.node->fact.type = function.type;
		value.node->fact.spelled = function.type;
		value.node->fact.category = ValueCategory::LValue;
		value.node->fact.entity = &function;
		return;
	}
	value.what = what;
	value.payload = named;
	respell(value);
}

// 5.2.5p1: `E1.E2` and `E1->E2` name a member of the class of the object
// expression, which is one lookup in the region that class declares.  The
// member is not looked up in the region the expression is written in: 3.4.5
// makes the object expression say where to look.
// 5.2.5p2: the class whose members `E1.` or `E1->` names, which for the arrow
// is what the pointer addresses.  `object` is left denoting that object.
Scope& SemaAnalyzer::object_region(const AstNode& node, Value& object)
{
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
	return *owner->scope;
}

SemaAnalyzer::Value SemaAnalyzer::member_expression(const AstNode& node,
                                                    const Context& ctx,
                                                    DumpNode& parent)
{
	// The line the member writes holds the object expression, and what it says
	// is known only once the member is found, so the node is opened for the
	// operand to write under and spelled afterwards.
	DumpNode& line = model_.open_node(parent, std::string());
	Value object = expression(*node.children[0], ctx, line);
	Scope& region = object_region(node, object);
	const AstNode& id = *node.children[1];
	std::vector<SemaEntity*>& found = model_.open_overloads();
	SemaEntity& found_member =
		require(member_named(region, id.text, ctx, found), id.text);
	require_access(found_member, ctx.scope, &region);
	require_protected_object(found, found_member, ctx.scope, &region);
	// 7.3.3p1 and 11.2p5: the class the name was written on is the class that
	// declared what a using-declaration brought in, so the access above was
	// asked of that declaration - and the base subobject the member belongs to
	// is reached without asking about the base-specifier's own access.
	const bool checked_base = found_member.shadowed == nullptr;
	SemaEntity& member = declared_member(found_member);
	if (member.kind != SemaKind::Variable || !member.object_member)
	{
		// 9.4p1 and 7.2p10: the member is not part of the object, so the object
		// expression only said where to look and the name denotes what it would
		// have denoted with the class written before it.  5.2.5p1 still
		// evaluates that expression, so it is dropped only where doing so is
		// what evaluating it comes to.
		require_droppable(*object.node, id.text);
		parent.children.pop_back();
		return named_value(id, found_member, parent, &found);
	}
	// 9.5p1: the member belongs to the object the anonymous class declared,
	// which is itself a member of the class named here, so the access the
	// object expression wrote holds one more - the same object a member named
	// with no object expression is reached through.
	object = through_anonymous_storage(member, object, checked_base);
	return member_value(member, object,
	                    std::string(ast_token_type_name(node.token)) + ":" +
	                    id.text, line, checked_base);
}

// 5.3.1p3: the address of the object a member function is called on, written
// into the node the object expression was read into.  `E1->f()` already has the
// address, and `E1.f()` takes one.
void SemaAnalyzer::address_of_object(Value& object, DumpNode& node,
                                     bool through_pointer)
{
	if (through_pointer)
	{
		// The pointer the program wrote is the argument, so the node the call
		// passes is the one that expression already wrote.
		DumpNode& written = *node.children[0];
		node.text = written.text;
		node.fact = written.fact;
		node.children.swap(written.children);
		object.type = object.spelled = node.fact.type;
		object.node = &node;
		object.category = ValueCategory::PRValue;
		return;
	}
	object.type = object.spelled = types_.pointer_to(object.type);
	object.category = ValueCategory::PRValue;
	object.entity = nullptr;
	object.what = "unary-expression";
	object.op = OP_AMP;
	object.payload = std::string(ast_token_type_name(OP_AMP)) + ":&";
	object.node = &node;
	respell(object);
}

// 5.2.5p1: what `E1.E2` and `E1->E2` name in the class of the object
// expression, with `found` taking the declarations the lookup associated with
// the name.  It is the region's own lookup, plus the one name 5.2.4p2 spells as
// a type rather than as the member it denotes.
SemaEntity* SemaAnalyzer::member_named(Scope& region, const std::string& id,
                                       const Context& ctx,
                                       std::vector<SemaEntity*>& found)
{
	const QualifiedName written(id);
	if (written.qualified())
	{
		// 5.2.5p1 and 5.2.5p2: the id-expression after `.` or `->` may be a
		// qualified-id, and then the class its nested-name-specifier names is
		// where the member is looked up - which has to be the object's own
		// class or one 10.2's chain reaches from it, because the member has to
		// be a member of the object.  3.4.5p1 looks the specifier up in that
		// class first and then where the whole expression stands.
		const std::string prefix = id.substr(0, id.size() -
		                                     written.last().size() - 2);
		SemaEntity* named = model_.lookup_in(region, prefix, LookupKind::Region);
		if (named == nullptr)
		{
			named = resolve(prefix, ctx, LookupKind::Region);
		}
		if (named == nullptr || named->kind != SemaKind::Class ||
		    named->scope == nullptr)
		{
			throw std::runtime_error(prefix + " names no class the member "
			                         "access can be looked up in");
		}
		if (named->scope != &region && !derives_from(region, *named->scope))
		{
			throw std::runtime_error(prefix + " is not the class of the object "
			                         "expression nor a base of it");
		}
		// The member is that class's, so what a call of it runs is that
		// class's function.  10.3 would make this the non-virtual call, and
		// this milestone has no virtual function for it to be one of.
		return member_named(*named->scope, written.last(), ctx, found);
	}
	SemaEntity* const member =
		model_.lookup_in(region, id, LookupKind::Any, &found);
	if (member != nullptr)
	{
		return member;
	}
	SemaEntity* const destructor = destructor_named(region, id, ctx);
	if (destructor != nullptr)
	{
		found.clear();
		found.push_back(destructor);
	}
	return destructor;
}

// 12.4p12 and 5.2.4p2: the destructor `~T` names on an object of class type.
//
// 12.4p1 gives the destructor of a class the one name `~` and the class's own,
// which is what `Scope` binds it under; but 5.2.4p2 writes a *type-name* after
// the `~`, and a typedef-name for the class is one.  So a name the class does
// not bind is still that class's destructor when it names the class, which
// 3.4.5p3 looks for in the class first and then where the expression stands.
// Null for every other member name, which is what leaves the ordinary lookup
// in charge of it.
SemaEntity* SemaAnalyzer::destructor_named(Scope& region, const std::string& id,
                                           const Context& ctx)
{
	if (id.empty() || id[0] != '~' || region.owner == nullptr ||
	    region.owner->destructor == nullptr)
	{
		return nullptr;
	}
	const std::string named = id.substr(1);
	SemaEntity* type = model_.lookup_in(region, named, LookupKind::Type);
	if (type == nullptr)
	{
		type = resolve(named, ctx, LookupKind::Type);
	}
	if (type == nullptr || !names_a_type(*type) ||
	    types_.strip_cv(type->type) != types_.strip_cv(region.owner->type))
	{
		return nullptr;
	}
	return region.owner->destructor;
}

// 5.2.4: `E1.~T()` and `E1->~T()` for a `T` that is not a class type, which
// 5.2.4p2 calls a pseudo-destructor call.
//
// It is told from the destructor of a class by the name after the `~` alone:
// a class declares its destructor and 12.4p12 binds that name in the class, so
// `p->~Box()` is the ordinary member call that path already writes, while for
// every other type there is no region to look a member up in at all.  The
// question is asked before the object expression is read, because reading it
// twice would write it twice.
//
// 5.2.4p1: the type shall be the same as the object's, cv-qualification aside,
// and the only effect of the call is the evaluation of the postfix-expression
// before the `.` or `->`.  That is exactly the value of that expression
// discarded, which is what 5.2.9p4's conversion to `void` is, so it is the node
// the resolved tree holds: no destructor is named, because a scalar has none.
bool SemaAnalyzer::pseudo_destructor_call(const AstNode& node,
                                          const AstNode& callee,
                                          const Context& ctx, DumpNode& parent,
                                          Value& out)
{
	if (callee.kind != AstKind::MemberExpression)
	{
		return false;
	}
	const AstNode& id = *callee.children[1];
	if (id.text.empty() || id.text[0] != '~')
	{
		return false;
	}
	const std::string named = id.text.substr(1);
	SemaEntity* const type = resolve(named, ctx, LookupKind::Type);
	if (type == nullptr || !names_a_type(*type) ||
	    types_.is_class(types_.strip_cv(type->type)))
	{
		return false;
	}
	const TypeId wanted = types_.strip_cv(type->type);
	// 3.9p10 and 5.2.4p2: the type shall be a scalar type - an arithmetic type,
	// an enumeration, a pointer, a pointer to member, or `std::nullptr_t`.  A
	// typedef-name may stand for an array, a function, a reference or `void`,
	// and none of those has a destructor to write a call of.
	if (!types_.is_scalar(wanted))
	{
		throw std::runtime_error("a pseudo-destructor call names a type that is "
		                         "not a scalar type");
	}
	DumpNode& line = model_.open_node(parent, std::string());
	Value object = expression(*callee.children[0], ctx, line);
	if (callee.token == OP_ARROW)
	{
		// 5.2.5p2: `E1->~T()` is `(*E1).~T()`, so what the type is asked of is
		// what the pointer addresses.
		if (types_.kind(object.type) != TypeKind::Pointer)
		{
			throw std::runtime_error("`->` is written on an operand that is not "
			                         "a pointer");
		}
		object.type = types_.target(object.type);
	}
	if (types_.strip_cv(object.type) != wanted)
	{
		throw std::runtime_error("a pseudo-destructor call names a type the "
		                         "object expression does not have");
	}
	const AstNode* const list = arguments_of(node);
	if (list != nullptr && !list->children.empty())
	{
		throw std::runtime_error("a pseudo-destructor call takes no argument");
	}
	out = Value();
	out.type = out.spelled = types_.fundamental(FT_VOID);
	out.category = ValueCategory::PRValue;
	out.what = "cast-expression";
	out.node = &line;
	respell(out);
	return true;
}

void SemaAnalyzer::member_callee(const AstNode& callee, const Context& ctx,
                                 DumpNode& line, Value& target, Value& object)
{
	// The callee's own line is opened first so that it keeps its place among the
	// call's children while 13.3 works out which declaration it names.
	DumpNode& named = model_.open_node(line, std::string());
	DumpNode& object_line = model_.open_node(line, std::string());
	object = expression(*callee.children[0], ctx, object_line);
	const bool through_pointer = callee.token == OP_ARROW;
	Scope& region = object_region(callee, object);
	const AstNode& id = *callee.children[1];
	std::vector<SemaEntity*>& found = model_.open_overloads();
	SemaEntity& found_member =
		require(member_named(region, id.text, ctx, found), id.text);
	require_access(found_member, ctx.scope, &region);
	require_protected_object(found, found_member, ctx.scope, &region);
	// 7.3.3p1: what a using-declaration brought into this class was found here
	// and named the base's declaration, which is what the use reaches.
	const bool checked_base = found_member.shadowed == nullptr;
	SemaEntity& member = declared_member(found_member);
	if (member.kind == SemaKind::Function)
	{
		address_of_object(object, object_line, through_pointer);
		target.functions = &found;
		target.category = ValueCategory::LValue;
		target.node = &named;
		return;
	}
	// 5.2.5p4: the member is not a function, so the call reads whatever the
	// member holds.  The object expression belongs under the member access
	// rather than beside it, and the two nodes the call opened collapse into
	// the one the access writes.
	line.children.pop_back();
	line.children.pop_back();
	if (member.kind != SemaKind::Variable || !member.object_member)
	{
		// 5.2.5p1: the object expression is evaluated even where the member is
		// reached without one, so it is left out only where that is what
		// evaluating it comes to.
		require_droppable(*object.node, id.text);
		target = named_value(id, found_member, line, &found);
		object = Value();
		return;
	}
	DumpNode& access = model_.open_node(line, std::string());
	access.children.push_back(object_line.children[0]);
	object.node = access.children[0];
	object = through_anonymous_storage(member, object, checked_base);
	target = member_value(member, object,
	                      std::string(ast_token_type_name(callee.token)) + ":" +
	                      id.text, access, checked_base);
	object = Value();
}

// 9.3.2p1: a non-static member function named with no object expression is
// called on the object `this` points to, and the call passes it as its first
// argument like any other.
void SemaAnalyzer::implicit_object_argument(
	const std::vector<SemaEntity*>& candidates, DumpNode& line, Value& object)
{
	if (self_ == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		for (const SemaEntity* at = candidates[index]; at != nullptr;
		     at = at->next)
		{
			if (at->object_member)
			{
				object = this_value(line);
				return;
			}
		}
	}
}

// 10p1 and 4.10p3: the base class subobject of the object the operand denotes.
// The operand's line moves under the conversion, in the place it already had,
// so the tree names the subobject and nothing below it has to be re-read.  An
// operand of pointer type converts to a pointer to the base; an object converts
// to the base subobject itself, which is as cv-qualified as the object it is
// part of and is an lvalue exactly where the object was one.
SemaAnalyzer::Value SemaAnalyzer::base_value(const Value& object,
                                             SemaEntity& base, bool checked)
{
	Value value = object;
	const bool through_pointer =
		types_.kind(types_.strip_cv(object.type)) == TypeKind::Pointer;
	const TypeId from =
		through_pointer ? types_.target(object.type) : object.type;
	if (checked)
	{
		require_base_access(model_.type_owner(types_.strip_cv(from)), base);
	}
	TypeId to = types_.qualified(base.type, types_.object_cv(from));
	if (through_pointer)
	{
		to = types_.pointer_to(to);
		value.category = ValueCategory::PRValue;
	}
	value.type = value.spelled = to;
	value.entity = &base;
	value.functions = nullptr;
	value.addressed = nullptr;
	value.name = nullptr;
	value.constant = false;
	value.null_constant = false;
	value.value = 0;
	value.op = 0;
	value.what = "base-conversion";
	value.payload.clear();
	value.node = &model_.wrap_node(*object.node, std::string());
	respell(value);
	return value;
}

// 10.2 and 11.2: the subobject a member found through a base class is a member
// of.  Lookup reached the member through the classes between the object's own
// and the one that declared it, and what the member belongs to is the one base
// subobject at the end of that walk - so the walk says which class was reached
// and 4.10p3 writes the one node every other derived-to-base conversion writes.
// A member the walk never reaches belongs to the object as it stands.
SemaAnalyzer::Value SemaAnalyzer::object_in_declaring_class(
	const Value& object, const SemaEntity& member, bool checked)
{
	const SemaEntity* const named =
		member.storage != nullptr ? member.storage : &member;
	const Scope* const declaring = named->region;
	if (declaring == nullptr || declaring->kind != ScopeKind::Class ||
	    object.node == nullptr)
	{
		return object;
	}
	SemaEntity* reached = nullptr;
	for (SemaEntity* at = model_.type_owner(types_.strip_cv(object.type));
	     at != nullptr && at->scope != declaring; at = at->base)
	{
		reached = at->base;
	}
	if (reached == nullptr)
	{
		return object;
	}
	return base_value(object, *reached, checked);
}

// 5.1.1p1: a parameter named the way the program would name it, which is what
// 12.9p8 passes on to the constructor of the base subobject.  The name is a
// declaration the definition itself made, so nothing is looked up for it.
SemaAnalyzer::Value SemaAnalyzer::parameter_value(SemaEntity& parameter,
                                                  DumpNode& parent)
{
	Value value;
	value.type = parameter.type;
	if (types_.is_reference(value.type))
	{
		// 8.3.2p5: a name of reference type is an lvalue naming what it is
		// bound to, and the reference itself is not part of the expression.
		value.type = types_.target(value.type);
	}
	value.spelled = value.type;
	value.category = ValueCategory::LValue;
	value.entity = &parameter;
	value.what = "id-expression";
	value.payload = parameter.name;
	value.node = &model_.open_node(parent, std::string());
	respell(value);
	return value;
}

// 5.2.5p4: the member the object expression holds, which is an lvalue when the
// object is one and is as cv-qualified as the object it is part of.
SemaAnalyzer::Value SemaAnalyzer::member_value(SemaEntity& member,
                                               const Value& object_written,
                                               const std::string& payload,
                                               DumpNode& node,
                                               bool checked_base)
{
	// 10.2: the member may have been declared in a base of the object's class,
	// and what it is a member of is that class's base subobject.
	const Value object =
		object_in_declaring_class(object_written, member, checked_base);
	if (member.kind != SemaKind::Variable)
	{
		// 5.2.5p4 gives a member function the meaning only a call of it has, and
		// a call is what reads it: `member_call` is where the object it is named
		// on becomes the implicit object argument of 13.3.1.1.1.
		throw std::runtime_error(member.name + " names a member function that is "
		                         "used other than in a call of it");
	}
	Value value;
	// 7.1.1p10: a member declared `mutable` is not const however const the
	// object holding it is, so the const the object carries stops here while
	// its volatile does not.
	const unsigned carried = types_.object_cv(object.type) &
		(member.mutable_member ? ~unsigned(kCvConst) : ~0u);
	value.type = types_.qualified(member.type, carried);
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
	value.what = "member-expression";
	value.entity = &member;
	value.payload = payload;
	respell(value);
	return value;
}

// 9.5p1: the objects an anonymous class declared that stand between an object
// expression and `member`.  A class written inside another anonymous one leaves
// a chain of them - the object of the inner class is a member of the outer -
// and the access holds each in turn from the outermost in, which is the order
// the offsets add up in.
SemaAnalyzer::Value SemaAnalyzer::through_anonymous_storage(
	const SemaEntity& member, Value object, bool checked_base)
{
	std::vector<SemaEntity*> chain;
	for (SemaEntity* at = member.storage; at != nullptr; at = at->storage)
	{
		chain.push_back(at);
	}
	for (std::size_t index = chain.size(); index-- > 0;)
	{
		object = member_value(*chain[index], object, chain[index]->name,
		                      model_.wrap_node(*object.node, std::string()),
		                      checked_base);
	}
	return object;
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
	object.what = "id-expression";
	object.entity = &storage;
	object.payload = storage.name;
	object.node = &model_.open_node(
		line, spell(object.what, object.category, object.type, object.payload));
	record(object);
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
	value.what = "id-expression";
	// 9.3.2p1: `this` is the object parameter 9.3.1p3 gave the function, which
	// is the declaration a use of it names.
	value.entity = self_;
	value.payload = std::string(ast_token_type_name(KW_THIS)) + ":this";
	value.node = &model_.open_node(
		parent, spell(value.what, value.category, value.type, value.payload));
	record(value);
	return value;
}

// 2.14: what one analysed literal token is worth, as the one line the resolved
// tree writes for it.  A literal the program wrote and the literal 2.14.8p3
// hands a literal operator are the same fact, so they are built here once.
SemaAnalyzer::Value SemaAnalyzer::literal_value(const PostToken& token,
                                                const std::string& payload,
                                                DumpNode& parent)
{
	Value value;
	value.category = ValueCategory::PRValue;
	value.what = "literal";
	value.payload = payload;
	if (token.kind == PostTokenKind::LiteralArray)
	{
		// 2.14.5p8: a string literal is an lvalue array of n const characters.
		value.type = types_.array_of(
			types_.qualified(types_.fundamental(token.type), kCvConst), true,
			token.element_count);
		value.category = ValueCategory::LValue;
		value.spelled = value.type;
		value.node = &model_.open_node(
			parent, spell(value.what, value.category, value.type, value.payload));
		record(value);
		// 2.14.5p8: the array the literal is holds the code units the
		// translation read out of it, which no later layer can read back out
		// of the spelling on its own.
		value.node->fact.spelling = token.data;
		return value;
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
		parent, spell(value.what, value.category, value.type, value.payload));
	record(value);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::literal_expression(const AstNode& node,
                                                     const Context& ctx,
                                                     DumpNode& parent)
{
	Value value;
	value.category = ValueCategory::PRValue;
	value.what = "literal";
	value.payload = payload_of(node);
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
			parent, spell(value.what, value.category, value.type, value.payload));
		return value;
	}

	// 2.14: what one terminal of the parse is worth, asked of the layer that
	// read it rather than of its spelling.
	PostToken token;
	const bool string_form = scan_literal(spelling, token) == LiteralForm::String;
	if (token.kind == PostTokenKind::UserDefinedLiteral)
	{
		// 2.14.8p2: the token is a call of the literal operator its ud-suffix
		// names, so what the tree holds for it is that call.
		return user_defined_literal(token, spelling, ctx, parent);
	}
	if (token.kind != (string_form ? PostTokenKind::LiteralArray
	                               : PostTokenKind::Literal))
	{
		throw std::runtime_error(
			string_form ? "a string literal is outside the PA12 subset"
			            : "a literal is outside the PA12 subset");
	}
	return literal_value(token, value.payload, parent);
}

// 2.14.8p3 to p6: the one declaration of a literal-operator-id whose
// parameter-type-list is `wanted`.
//
// The forms name a parameter list rather than a set 13.3 ranks, so this is a
// comparison and not an overload resolution: one walk of the declarations the
// lookup reached, and one comparison of interned type ids per parameter.  A
// member function is none of them, because 13.5.8p1 declares a literal operator
// at namespace scope.
SemaEntity* SemaAnalyzer::literal_operator(
	const std::vector<SemaEntity*>& candidates,
	const std::vector<TypeId>& wanted)
{
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		for (SemaEntity* at = candidates[index]; at != nullptr; at = at->next)
		{
			if (at->kind != SemaKind::Function || at->object_member ||
			    types_.variadic(at->type))
			{
				continue;
			}
			const std::vector<TypeId>& parameters = types_.parameters(at->type);
			if (parameters.size() != wanted.size())
			{
				continue;
			}
			std::size_t same = 0;
			while (same < wanted.size() && parameters[same] == wanted[same])
			{
				++same;
			}
			if (same == wanted.size())
			{
				return at;
			}
		}
	}
	return nullptr;
}

// 2.14.8: a user-defined-literal, which p2 makes a call of the literal operator
// its ud-suffix names.
//
// 2.14.8p2 looks the literal-operator-id up by ordinary unqualified lookup from
// where the literal stands - and no further: 3.4.2 is not searched, because the
// literal has no argument whose type could associate a namespace.  What p3 to
// p6 then choose out of that set is not 13.3's ranking but one written
// parameter-type-list per form, so a literal operator taking `char` is no
// candidate for an integer literal and a value is never converted into one it
// does not have.  The cooked forms are the value of the literal, and the raw
// form is the fallback p3 names for a numeric literal no cooked operator
// declares, where the operator is handed the digits the program wrote.
SemaAnalyzer::Value SemaAnalyzer::user_defined_literal(const PostToken& token,
                                                       const std::string& spelling,
                                                       const Context& ctx,
                                                       DumpNode& parent)
{
	const std::string name = "operator\"\"" + token.ud_suffix;
	std::vector<SemaEntity*>& found = model_.open_overloads();
	SemaEntity* const head =
		model_.lookup(*ctx.scope, name, LookupKind::Any, &found);
	if (head == nullptr || head->kind != SemaKind::Function)
	{
		throw std::runtime_error("no literal operator is declared for the "
		                         "ud-suffix of a user-defined literal");
	}
	if (found.empty())
	{
		found.push_back(head);
	}

	// 2.14.8p1: the ud-suffix is the tail of the token, so what is left of the
	// spelling is the literal 2.14 gives the operator - and is what the tree
	// writes under the call, because that is what the program wrote.
	const std::string prefix =
		spelling.size() > token.ud_suffix.size()
			? spelling.substr(0, spelling.size() - token.ud_suffix.size())
			: spelling;
	// The value the literal is, and the parameter-type-list p3 to p6 say the
	// operator that takes it was declared with.
	PostToken cooked;
	cooked.reset(PostTokenKind::Literal);
	std::vector<TypeId> wanted;
	bool cooked_known = true;
	switch (token.ud_kind)
	{
	case UserDefinedLiteralKind::String:
		// 2.14.8p5: `operator "" X(const charT *, std::size_t)`, handed the
		// characters and how many of them there are.
		cooked.reset(PostTokenKind::LiteralArray);
		cooked.type = token.type;
		cooked.element_count = token.element_count;
		cooked.data = token.data;
		wanted.push_back(types_.pointer_to(
			types_.qualified(types_.fundamental(token.type), kCvConst)));
		wanted.push_back(types_.fundamental(FT_UNSIGNED_LONG_INT));
		break;

	case UserDefinedLiteralKind::Character:
		// 2.14.8p6: `operator "" X(charT)`, for the character type 2.14.3 gave
		// the literal.
		cooked.type = token.type;
		cooked.data = token.data;
		wanted.push_back(types_.fundamental(token.type));
		break;

	case UserDefinedLiteralKind::Integer:
	case UserDefinedLiteralKind::Floating:
	default:
	{
		// 2.14.8p3: the value, as `unsigned long long` for an integer literal
		// and `long double` for a floating one.  A literal whose value no such
		// type holds leaves only the raw form.
		const bool integral = token.ud_kind == UserDefinedLiteralKind::Integer;
		PostToken scanned;
		scan_pp_number(prefix, scanned);
		cooked_known = scanned.kind == PostTokenKind::Literal &&
			fundamental_type_is_integral(scanned.type) == integral;
		if (integral)
		{
			cooked.set_integer_value(FT_UNSIGNED_LONG_LONG_INT,
			                         cooked_known ? scanned.integer_value() : 0);
			wanted.push_back(types_.fundamental(FT_UNSIGNED_LONG_LONG_INT));
		}
		else
		{
			cooked.type = FT_LONG_DOUBLE;
			cooked.data = cooked_known
				? scanned.data
				: std::string(fundamental_type_size(FT_LONG_DOUBLE), 0);
			wanted.push_back(types_.fundamental(FT_LONG_DOUBLE));
		}
		break;
	}
	}

	SemaEntity* chosen = cooked_known ? literal_operator(found, wanted) : nullptr;
	bool raw = false;
	if (chosen == nullptr && (token.ud_kind == UserDefinedLiteralKind::Integer ||
	                          token.ud_kind == UserDefinedLiteralKind::Floating))
	{
		// 2.14.8p3: the set declares no cooked operator for the value, so the
		// raw one takes the digits the program wrote as a narrow string.
		std::vector<TypeId> digits;
		digits.push_back(types_.pointer_to(
			types_.qualified(types_.fundamental(FT_CHAR), kCvConst)));
		chosen = literal_operator(found, digits);
		raw = chosen != nullptr;
	}
	if (chosen == nullptr)
	{
		throw std::runtime_error("no literal operator takes the value of a "
		                         "user-defined literal");
	}

	DumpNode& line = model_.open_node(parent, std::string());
	DumpNode& named = model_.open_node(line, std::string());
	std::vector<Value> arguments;
	if (raw)
	{
		PostToken digits;
		digits.reset(PostTokenKind::LiteralArray);
		digits.type = FT_CHAR;
		digits.element_count = prefix.size() + 1;
		digits.data = prefix;
		digits.data.push_back('\0');
		arguments.push_back(literal_value(digits, "\"" + prefix + "\"", line));
	}
	else
	{
		arguments.push_back(literal_value(cooked, prefix, line));
		if (token.ud_kind == UserDefinedLiteralKind::String)
		{
			// 2.14.8p5: the length is the elements of the array less the
			// terminating null character.  It is a number the translation
			// computed rather than one the program wrote, so it is spelled as
			// the constant 2.14.2 gives that number and reaches the parameter
			// through the conversion any argument would take.
			PostToken length;
			length.reset(PostTokenKind::Literal);
			length.set_integer_value(
				FT_INT, static_cast<unsigned long long>(token.element_count - 1));
			arguments.push_back(
				literal_value(length, decimal(token.element_count - 1), line));
		}
	}
	SemaEntity& run = declared_member(*chosen);
	Value callee;
	callee.node = &named;
	name_function(callee, run, "callee");
	return finish_call(line, run.type, arguments, &run, ctx);
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
		if (read.node != nullptr && read.node->fact.kind == FactKind::Member &&
		    read.node->fact.entity != nullptr &&
		    read.node->fact.entity->bit_field)
		{
			// 5.3.3p1: the operand shall not name a bit-field, which occupies
			// bits rather than the storage a number of bytes would describe.
			throw std::runtime_error("sizeof is applied to a bit-field, whose "
			                         "storage 5.3.3p1 does not measure");
		}
		value.value = size_of(read.type);
	}
	value.what = "sizeof-expression";
	value.node = &model_.open_node(
		parent, spell(value.what, value.category, result, value.payload));
	return value;
}

// 5.3.6p1: `alignof` yields the alignment an object of its operand's type
// requires, as a prvalue of `std::size_t`.  The operand is a type-id and is
// unevaluated, so the operator's own line carries nothing under it - which is
// the one line 5.3.3's `sizeof` writes too, and what the references spell both
// with.
SemaAnalyzer::Value SemaAnalyzer::alignof_expression(const AstNode& node,
                                                     const Context& ctx,
                                                     DumpNode& parent)
{
	const TypeId result = types_.fundamental(FT_UNSIGNED_LONG_INT);
	if (node.children.empty() || node.children[0]->kind != AstKind::TypeId)
	{
		// 5.3.6p1 gives `alignof` a type-id and nothing else.
		throw std::runtime_error("alignof is applied to something other than a "
		                         "type-id");
	}
	const TypeId type = type_id_type(*node.children[0], ctx);
	if (types_.is_incomplete(type))
	{
		// 5.3.6p3: the type shall be complete, which is what has an alignment.
		throw std::runtime_error("alignof names an incomplete type");
	}
	Value value;
	value.type = result;
	value.spelled = result;
	value.category = ValueCategory::PRValue;
	value.constant = true;
	value.value = types_.object_align(type);
	value.what = "sizeof-expression";
	value.node = &model_.open_node(
		parent, spell(value.what, value.category, result, value.payload));
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
	// 13.5.5p1: a subscript on a class object is a call of a member operator
	// function, which is the only kind `operator[]` may be declared as.
	std::vector<Value> operands;
	operands.push_back(left);
	operands.push_back(right);
	Value chosen;
	if (operator_expression(OP_LSQUARE, ctx, line, operands, true, chosen))
	{
		return chosen;
	}
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
	value.what = "subscript-expression";
	value.op = OP_LSQUARE;
	respell(value);
	return value;
}

// The operand's line takes the place the cast's would have had, in the order
// the parent already holds: 5.2.9p1 gives the two the same one node wherever
// the cast converts nothing the output describes.
void SemaAnalyzer::lift_operand(DumpNode& parent, DumpNode& line)
{
	parent.children.pop_back();
	for (std::size_t index = 0; index < line.children.size(); ++index)
	{
		parent.children.push_back(line.children[index]);
	}
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
	if (source.type == kNoType && source.functions != nullptr)
	{
		// 13.4p1: a cast is one of the contexts whose target type chooses one
		// declaration of an overloaded name, whether or not it is a reference.
		SemaEntity* chosen = resolve_target(source, target);
		if (chosen == nullptr)
		{
			throw std::runtime_error("no declaration of an overloaded function "
			                         "name has the type a cast asks for");
		}
		name_function(source, *chosen, "id-expression");
	}

	Value value;
	value.type = target;
	value.spelled = target;
	value.category = ValueCategory::PRValue;
	value.what = "cast-expression";
	value.op = node.token == kNoAstToken ? 0u : node.token;
	if (node.token != kNoAstToken)
	{
		value.payload = payload_of(node);
	}
	if (types_.is_reference(target))
	{
		return cast_to_reference(target, source, parent, line, value);
	}
	if (types_.kind(types_.strip_cv(target)) == TypeKind::Pointer &&
	    types_.kind(types_.strip_cv(source.type)) == TypeKind::Pointer)
	{
		const TypeId to = types_.target(types_.strip_cv(target));
		const TypeId from = types_.target(types_.strip_cv(source.type));
		// 5.2.9p11 and 4.10p3: a cast to a pointer to a base class of the
		// operand's class is that conversion, so it names the base subobject
		// rather than reinterpreting the address - and 11.2p4 asks here whether
		// the base-specifier's access reaches this expression.
		SemaEntity* const base = derived_from(from, to);
		if (base != nullptr)
		{
			source = base_value(source, *base);
			lift_operand(parent, line);
			source.type = source.spelled = target;
			source.category = ValueCategory::PRValue;
			return source;
		}
		// 5.2.9p11 the other way round: a pointer to a base class casts to a
		// pointer to a class derived from it, which is well formed only where
		// that base is accessible.  The subobject begins where the derived object
		// does, so the address is the one the operand held and the cast writes no
		// conversion around it - but the access is asked for all the same.
		SemaEntity* const from_base = derived_from(to, from);
		if (from_base != nullptr)
		{
			require_base_access(model_.type_owner(types_.strip_cv(to)),
			                    *from_base);
		}
	}
	if (types_.kind(types_.strip_cv(target)) == TypeKind::MemberPointer &&
	    source.type == types_.strip_cv(target))
	{
		// 5.2.9p2: a cast to the type the operand has converts nothing, and a
		// pointer to member holds which member it names rather than an address,
		// so there is nothing for the output to write around the operand.
		lift_operand(parent, line);
		return source;
	}
	line.text = spell(value.what, value.category, target, value.payload);
	value.node = &line;
	record(value);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::cast_to_reference(TypeId target, Value& source,
                                                    DumpNode& parent,
                                                    DumpNode& line, Value value)
{
	{
		// 5.2.9p1: a cast to an lvalue reference is an lvalue and one to an
		// rvalue reference to an object type is an xvalue.
		const TypeId referenced = types_.target(target);
		value.type = referenced;
		value.category = types_.kind(target) == TypeKind::LValueReference
			? ValueCategory::LValue
			: ValueCategory::XValue;
		// 8.5.3p5: an lvalue reference that is not to a non-volatile const binds
		// only an lvalue, and 5.4p4 offers a cast no reading that would bind one
		// to a value naming no object.
		const bool const_lvalue = (types_.cv(referenced) & kCvConst) != 0 &&
			(types_.cv(referenced) & kCvVolatile) == 0;
		if (types_.kind(target) == TypeKind::LValueReference && !const_lvalue &&
		    source.category != ValueCategory::LValue)
		{
			throw std::runtime_error("a cast to a non-const lvalue reference is "
			                         "written on an operand that names no object");
		}
		// 8.5.3p4: reference-related is the referenced type and the operand's
		// being one type up to cv-qualification, and a related reference binds
		// the value it was given rather than a conversion of it - which is what
		// 5.4p4 leaves to const_cast where a static_cast would convert nothing.
		// So the operand's own line is what says what the cast made of it, and
		// the cast writes no node.
		if (bare_type(source.type) == bare_type(referenced) &&
		    source.node != nullptr && source.what != nullptr)
		{
			source.category = value.category;
			source.type = referenced;
			source.spelled = target;
			respell(source);
			lift_operand(parent, line);
			return source;
		}
		// 8.5.3p4: reference-related is also the referenced type being a base
		// class of the operand's, and such a reference binds the base subobject
		// of the object the operand names rather than a conversion of its value -
		// which is the one `base-conversion` node 4.10p3 writes everywhere else.
		SemaEntity* const to_base =
			source.node != nullptr && source.what != nullptr
				? derived_from(source.type, referenced)
				: nullptr;
		if (to_base != nullptr)
		{
			source = base_value(source, *to_base);
			source.category = value.category;
			source.type = referenced;
			source.spelled = target;
			respell(source);
			lift_operand(parent, line);
			return source;
		}
		// 5.2.9p11: an lvalue of a base class casts to a reference to a class
		// derived from it, and what the result names is the object that base
		// subobject is part of.  The subobject begins where the derived object
		// does, so the storage the operand named is the storage the cast names,
		// and 11.2p4 asks here whether the base-specifier's access reaches.
		SemaEntity* const from_base = derived_from(referenced, source.type);
		if (from_base != nullptr)
		{
			require_base_access(model_.type_owner(types_.strip_cv(referenced)),
			                    *from_base);
			value.payload.clear();
			value.node = &line;
			respell(value);
			return value;
		}
		// The operand is converted to the referenced type and the reference
		// binds the temporary that holds it, which is a value of its own.  The
		// node stands for that temporary rather than for the terminals the cast
		// was written from, as the one an argument conversion writes does.
		if (!match_by_value(source, referenced).viable)
		{
			throw std::runtime_error("a cast to a reference type binds neither "
			                         "the operand nor a conversion of it");
		}
		value.payload.clear();
		value.node = &line;
		respell(value);
		return value;
	}
}

// 1.4p8: the functions this implementation reserves a name for, as the type
// each has and what a backend may assume of a call of one.  A program declares
// none of them, so a use of the name is what declares the one it names - and
// the declaration is an ordinary one, which is what lets 13.3, 4.10's argument
// conversions and the lowering read it the way they read any other function.
SemaEntity* SemaAnalyzer::reserved_function(const std::string& written,
                                            std::vector<SemaEntity*>* found)
{
	// 3.4.3.2p1: 1.4p8 puts a reserved function in the global namespace, so
	// `::__builtin_strlen` names the one `__builtin_strlen` names.  No other
	// nested-name-specifier does: a name the implementation reserves is the
	// global namespace's alone, and one written under any other region names
	// what that region declares or nothing.
	const std::string name =
		written.compare(0, 2, "::") == 0 ? written.substr(2) : written;
	const TypeId voided = types_.fundamental(FT_VOID);
	const TypeId size = types_.fundamental(FT_UNSIGNED_LONG_INT);
	const TypeId any = types_.pointer_to(voided);
	const TypeId source = types_.pointer_to(types_.qualified(voided, kCvConst));
	const TypeId text = types_.pointer_to(
		types_.qualified(types_.fundamental(FT_CHAR), kCvConst));
	std::vector<TypeId> parameters;
	TypeId result = voided;
	unsigned char which = kNotBuiltin;
	if (name == "__builtin_memcpy" || name == "__builtin_memmove")
	{
		// 17.6.5.6: the copies of `<cstring>`, which take the storage to write,
		// the storage to read and how many bytes, and give back the first.
		which = name == "__builtin_memcpy" ? kBuiltinMemcpy : kBuiltinMemmove;
		result = any;
		parameters.push_back(any);
		parameters.push_back(source);
		parameters.push_back(size);
	}
	else if (name == "__builtin_strlen")
	{
		which = kBuiltinStrlen;
		result = size;
		parameters.push_back(text);
	}
	else if (name == "__builtin_unreachable")
	{
		which = kBuiltinUnreachable;
	}
	else
	{
		return nullptr;
	}
	Context global;
	global.scope = &model_.global();
	global.dump = model_.global().dump;
	SemaEntity& entity = declare_function(
		name, types_.function_of(result, parameters, false), global, false);
	entity.builtin = which;
	if (found != nullptr)
	{
		found->push_back(&entity);
	}
	return &entity;
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
		out.what = "literal";
		out.payload = decimal(out.value);
		out.node = &model_.open_node(
			parent, spell(out.what, ValueCategory::PRValue, out.type, out.payload));
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
	out.what = "call-expression";
	out.node = &model_.open_node(
		parent, spell(out.what, ValueCategory::PRValue, out.type, out.payload));
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
SemaAnalyzer::Value SemaAnalyzer::member_address(const AstNode& node,
                                                 const SemaEntity& entity,
                                                 DumpNode& parent)
{
	Value value;
	DumpNode& line = model_.open_node(parent, std::string());
	model_.open_node(line, spell("id-expression", ValueCategory::LValue,
	                             entity.type, entity.dump_name));
	value.type = types_.member_pointer_to(entity.region->owner->type,
	                                      entity.type);
	value.spelled = value.type;
	value.category = ValueCategory::PRValue;
	value.node = &line;
	value.what = "unary-expression";
	value.payload = payload_of(node);
	respell(value);
	return value;
}

SemaAnalyzer::Value SemaAnalyzer::unary_expression(const AstNode& node,
                                                   const Context& ctx,
                                                   DumpNode& parent)
{
	const AstNode& written = *node.children[0];
	// 5.3.1p3: whether `&` was written on a qualified-id that names a member of
	// a class is the one question that decides how its operand is read, so the
	// one lookup it takes is spent here and its answer handed on rather than
	// asked for again below.  A name `resolve` reaches nothing for is a
	// template-id, which the expression layer answers.
	SemaEntity* named = nullptr;
	std::vector<SemaEntity*>* found = nullptr;
	if (node.token == OP_AMP && written.kind == AstKind::IdExpression &&
	    QualifiedName(written.text).qualified())
	{
		found = &model_.open_overloads();
		// 7.1.6.2p1: a nested-name-specifier that begins with a
		// decltype-specifier reaches its region through the expression the
		// parser kept beside the name, which no spelling holds.
		named = child_kind(written, AstKind::CarriedExpression) == nullptr
			? resolve(written.text, ctx, LookupKind::Any, found)
			: decltype_qualified_name(written, ctx, LookupKind::Any, found);
		if (named != nullptr && named->kind == SemaKind::Variable &&
		    named->object_member && named->region->owner != nullptr)
		{
			return member_address(node, *named, parent);
		}
	}
	DumpNode& line = model_.open_node(parent, std::string());
	// A qualified name of anything else - a static member, a variable of a
	// namespace, a function - is the operand `&` reads it as.
	const Value operand = named != nullptr
		? named_value(written, *named, line, found)
		: expression(written, ctx, line);
	Value value;
	// 13.5p1: a unary operator written on an operand of class or enumeration
	// type is a call, whichever operator it is.
	std::vector<Value> operands;
	operands.push_back(operand);
	if (operator_expression(node.token, ctx, line, operands, false, value))
	{
		return value;
	}
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
			value.payload = payload_of(node);
			return value;
		}
		// 5.3.1p3: the result is a pointer to the object or function the
		// operand names, so the operand has to name one.
		require_complete_value(operand);
		if (operand.category != ValueCategory::LValue)
		{
			throw std::runtime_error("the operand of unary & is not an lvalue");
		}
		// The operand denotes a function whose name resolved where it was read,
		// which is the one declaration the set then holds.
		value.type = operand.functions == nullptr
			? kNoType
			: member_pointer_of(*(*operand.functions)[0]);
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
	value.what = "unary-expression";
	value.op = node.token;
	value.payload = payload_of(node);
	respell(value);
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
	// 13.5.7p1: `x++` is read as `x++0`, so the candidates of a postfix
	// increment are gathered over two operands and the second is a zero the
	// program did not write.
	std::vector<Value> operands;
	operands.push_back(operand);
	if (postfix)
	{
		Value zero;
		zero.type = zero.spelled = types_.fundamental(FT_INT);
		zero.category = ValueCategory::PRValue;
		zero.constant = true;
		zero.what = "literal";
		zero.payload = "0";
		zero.node = &model_.open_node(
			line, spell(zero.what, zero.category, zero.type, zero.payload));
		record(zero);
		operands.push_back(zero);
	}
	Value chosen;
	if (operator_expression(node.token, ctx, line, operands, false, chosen))
	{
		return chosen;
	}
	if (postfix)
	{
		// Nothing was chosen, so the operand 13.5.7p1 added is not one the
		// built-in operator reads and the line it wrote leaves the tree.
		line.children.pop_back();
	}
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
	value.what = postfix ? "postfix-expression" : "unary-expression";
	value.op = node.token;
	value.payload = payload_of(node);
	respell(value);
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
	// 5.9p2 and 4.10p3: where one class derives from the other, the two meet at
	// a pointer to the base, which the derived one converts to.
	if (derived_from(left_pointee, right_pointee) != nullptr)
	{
		return types_.pointer_to(types_.qualified(right_pointee, cv));
	}
	if (derived_from(right_pointee, left_pointee) != nullptr)
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
	Value left = expression(*node.children[0], ctx, line);
	Value right = expression(*node.children[1], ctx, line);

	Value value;
	// 13.5p1: an operand of class or enumeration type makes the operator a
	// call, and 13.3 says which function it calls.
	std::vector<Value> operands;
	operands.push_back(left);
	operands.push_back(right);
	if (operator_expression(node.token, ctx, line, operands, false, value))
	{
		return value;
	}
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
	value.what = "binary-expression";
	value.op = node.token;
	value.operands = node.token == OP_COMMA
		? value.type
		: binary_operand_type(node.token, left, right);
	// 5.9p2 and 4.10p3: where the one type both operands are brought to is a
	// pointer to a base of an operand's class, that operand becomes a pointer
	// to its base subobject, which the tree names rather than leaving the
	// address to be adjusted by whoever reads the comparison.
	convert_operand_to_base(left, value.operands);
	convert_operand_to_base(right, value.operands);
	value.payload = payload_of(node);
	respell(value);
	return value;
}

// 5.16p3: the operand of a conditional whose result is an lvalue of a base of
// its own class, which is that operand's base subobject.
void SemaAnalyzer::convert_arm_to_base(Value& arm, TypeId result)
{
	if (arm.node == nullptr)
	{
		return;
	}
	SemaEntity* const base = derived_from(arm.type, result);
	if (base != nullptr)
	{
		arm = base_value(arm, *base);
	}
}

// 5.9p2: an operand brought to the composite pointer type of two pointers to
// related classes is converted to point at its own base subobject.
void SemaAnalyzer::convert_operand_to_base(Value& operand, TypeId operands)
{
	if (operands == kNoType || operand.node == nullptr ||
	    types_.kind(types_.strip_cv(operands)) != TypeKind::Pointer ||
	    types_.kind(types_.strip_cv(operand.type)) != TypeKind::Pointer)
	{
		return;
	}
	SemaEntity* const base =
		derived_from(types_.target(types_.strip_cv(operand.type)),
		             types_.target(types_.strip_cv(operands)));
	if (base != nullptr)
	{
		operand = base_value(operand, *base);
	}
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

// 5p9, 5.7p1 and 5.9p2: what both operands of a built-in binary operator are
// converted to.  For an arithmetic or bitwise operator that is the result type
// itself; the operators whose result says nothing about their operands are the
// comparisons, the shifts, and the pointer forms of `+` and `-`.
TypeId SemaAnalyzer::binary_operand_type(unsigned op, const Value& left,
                                         const Value& right)
{
	const TypeId a = decayed(left);
	const TypeId b = decayed(right);
	switch (op)
	{
	case OP_LAND:
	case OP_LOR:
		return types_.fundamental(FT_BOOL);

	case OP_LSHIFT:
	case OP_RSHIFT:
		// 5.8p1: the operands are promoted separately, so the left one alone
		// says what the operation is done at.
		return promoted(a);

	case OP_EQ:
	case OP_NE:
	case OP_LT:
	case OP_GT:
	case OP_LE:
	case OP_GE:
	{
		if (types_.kind(a) == TypeKind::Enum && a == b)
		{
			return a;
		}
		const bool arithmetic =
			(types_.is_arithmetic(a) || types_.kind(a) == TypeKind::Enum) &&
			(types_.is_arithmetic(b) || types_.kind(b) == TypeKind::Enum) &&
			!types_.is_scoped_enum(a) && !types_.is_scoped_enum(b);
		if (arithmetic)
		{
			return arithmetic_result(a, b);
		}
		const TypeId composite = composite_pointer(left, right);
		return composite != kNoType ? composite : a;
	}

	case OP_PLUS:
	case OP_MINUS:
	{
		const bool a_pointer = types_.is_object_pointer(a);
		const bool b_pointer = types_.is_object_pointer(b);
		if (a_pointer && b_pointer)
		{
			return a;
		}
		if (a_pointer)
		{
			return a;
		}
		if (b_pointer)
		{
			return b;
		}
		return arithmetic_result(a, b);
	}

	default:
		break;
	}
	return arithmetic_result(a, b);
}

SemaAnalyzer::Value SemaAnalyzer::assignment_expression(const AstNode& node,
                                                        const Context& ctx,
                                                        DumpNode& parent)
{
	DumpNode& line = model_.open_node(parent, std::string());
	const Value left = expression(*node.children[0], ctx, line);
	require_complete_value(left);
	// 13.5.3p1: `operator=` shall be a non-static member function, so an
	// assignment to a class object is a call of one where the class has one -
	// and where it has one, that is what the assignment means, so a right
	// operand none of them accepts is an error rather than a fall-back.
	if (node.token == OP_ASS && types_.is_class(types_.strip_cv(left.type)))
	{
		SemaEntity* const owner =
			model_.type_owner(types_.strip_cv(left.type));
		if (owner != nullptr && owner->scope != nullptr &&
		    model_.lookup_in(*owner->scope, "operator=",
		                     LookupKind::Any) != nullptr)
		{
			std::vector<Value> operands;
			operands.push_back(left);
			operands.push_back(expression(*node.children[1], ctx, line));
			Value chosen;
			if (!operator_expression(node.token, ctx, line, operands, true,
			                         chosen))
			{
				throw std::runtime_error("no assignment operator of the class "
				                         "accepts the right operand");
			}
			return chosen;
		}
	}
	// 5.17p1: the left operand is a modifiable lvalue and the result is that
	// lvalue.
	if (left.category != ValueCategory::LValue ||
	    (types_.cv(left.type) & kCvConst) != 0)
	{
		throw std::runtime_error("the left operand of an assignment is not a "
		                         "modifiable lvalue");
	}

	TypeId compound_type = kNoType;
	if (node.token == OP_ASS)
	{
		initialize(*node.children[1], types_.strip_cv(left.type), ctx, line);
	}
	else
	{
		const Value right = expression(*node.children[1], ctx, line);
		require_complete_value(right);
		// 13.5p1: a compound assignment on a class or enumeration operand is a
		// call like any other operator, and unlike `=` it may be a non-member.
		std::vector<Value> operands;
		operands.push_back(left);
		operands.push_back(right);
		Value chosen;
		if (operator_expression(node.token, ctx, line, operands, false, chosen))
		{
			return chosen;
		}
		// 5.17p7: a compound assignment behaves as the operator it names
		// followed by an assignment, so the operator's own rules decide
		// whether the operands go together.
		Value target = left;
		target.type = types_.strip_cv(left.type);
		Value result;
		result.type = binary_result(compound_operator(node.token), target, right);
		result.spelled = result.type;
		compound_type = binary_operand_type(compound_operator(node.token),
		                                    target, right);
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
	value.what = "assignment-expression";
	value.op = node.token;
	value.operands = compound_type;
	value.payload = payload_of(node);
	respell(value);
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
	Value left = expression(*node.children[1], ctx, line);
	Value right = expression(*node.children[2], ctx, line);
	require_complete_value(left);
	require_complete_value(right);

	Value value;
	value.category = ValueCategory::PRValue;
	// 5.16p3: each operand is converted to an lvalue reference to the other's
	// type, and the one conversion that binds says what the result denotes.  Two
	// lvalues of one type both bind, which is 5.16p4; two whose types differ only
	// in cv-qualification bind one way only, and the result is the lvalue of the
	// more qualified of the two; anything else binds neither way and the result
	// is the prvalue the rules below give it.
	const bool as_left = left.category == ValueCategory::LValue &&
		right.category == ValueCategory::LValue &&
		binds_reference(right, types_.reference_to(left.type, false));
	const bool as_right = left.category == ValueCategory::LValue &&
		right.category == ValueCategory::LValue &&
		binds_reference(left, types_.reference_to(right.type, false));
	if (as_left || as_right)
	{
		value.type = as_right ? right.type : left.type;
		value.category = ValueCategory::LValue;
		// 5.16p3 and 4.10p3: where the operand that bound is of a class derived
		// from the other's, what the result denotes is its base subobject, so
		// the arm that reached it names that subobject.
		convert_arm_to_base(as_right ? left : right, value.type);
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
		// 5.16p6 and 4.10p3: the operands are brought to that composite pointer
		// type, so the one that pointed at a derived class points at its own base
		// subobject - the same conversion 5.9p2 writes for a comparison of the
		// two, asked here of the same base-specifier's access.
		convert_operand_to_base(left, value.type);
		convert_operand_to_base(right, value.type);
	}
	value.spelled = value.type;
	value.node = &line;
	value.what = "conditional-expression";
	respell(value);
	return value;
}
