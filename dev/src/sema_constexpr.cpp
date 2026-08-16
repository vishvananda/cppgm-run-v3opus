#include "sema_constexpr.h"

#include <stdexcept>

#include "ast_model.h"
#include "sema_analyzer.h"
#include "sema_reading.h"
#include "sema_scope.h"

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

bool add_overflows(long long left, long long right, long long& out)
{
	const unsigned long long sum = static_cast<unsigned long long>(left) +
		static_cast<unsigned long long>(right);
	out = static_cast<long long>(sum);
	return ((left ^ out) & (right ^ out)) < 0;
}

bool subtract_overflows(long long left, long long right, long long& out)
{
	const unsigned long long difference = static_cast<unsigned long long>(left) -
		static_cast<unsigned long long>(right);
	out = static_cast<long long>(difference);
	return ((left ^ right) & (left ^ out)) < 0;
}

bool multiply_overflows(long long left, long long right, long long& out)
{
	out = static_cast<long long>(static_cast<unsigned long long>(left) *
	                             static_cast<unsigned long long>(right));
	if (left == 0 || right == 0)
	{
		out = 0;
		return false;
	}
	const long long lowest = -1 - 0x7FFFFFFFFFFFFFFFLL;
	if (left == -1)
	{
		return right == lowest;
	}
	if (right == -1)
	{
		return left == lowest;
	}
	return out / left != right;
}
// 7.1.5p3: the declarations a constexpr function's body may write around its
// one return statement.  They declare no object and run nothing, so a fold
// reads them for the names they introduce and for nothing else.
bool is_body_declaration(AstKind kind)
{
	return kind == AstKind::SimpleDeclaration ||
		kind == AstKind::AliasDeclaration ||
		kind == AstKind::UsingDeclaration ||
		kind == AstKind::UsingDirective ||
		kind == AstKind::StaticAssertDeclaration ||
		kind == AstKind::EmptyDeclaration;
}

// 8.3.5p10: the names the definition's own declarator gave its places, in the
// order the parameter-declaration-clause wrote them.  A name is no part of the
// function's type, so it is read from the definition that wrote the body rather
// than from any other declaration of the function; a place the definition left
// unnamed binds nothing, and the body cannot have named it either.
void parameter_names(const AstNode& definition, std::vector<std::string>& out)
{
	const AstNode* declarator = nullptr;
	for (std::size_t index = 0; index < definition.children.size(); ++index)
	{
		if (definition.children[index]->kind == AstKind::Declarator)
		{
			declarator = definition.children[index];
			break;
		}
	}
	if (declarator == nullptr)
	{
		return;
	}
	for (std::size_t index = 0; index < declarator->children.size(); ++index)
	{
		const AstNode& part = *declarator->children[index];
		if (part.kind != AstKind::ParameterClause)
		{
			continue;
		}
		for (std::size_t at = 0; at < part.children.size(); ++at)
		{
			const AstNode& place = *part.children[at];
			if (place.kind != AstKind::ParameterDeclaration)
			{
				return;
			}
			std::string named;
			for (std::size_t which = 0; which < place.children.size(); ++which)
			{
				const AstNode& held = *place.children[which];
				if (held.kind == AstKind::Declarator &&
				    !held.children.empty() &&
				    held.children[0]->kind == AstKind::Identifier)
				{
					named = held.children[0]->text;
				}
			}
			out.push_back(named);
		}
		return;
	}
}

}

ConstexprReading::ConstexprReading(SemaAnalyzer& analyzer)
	: analyzer_(analyzer)
{}

bool ConstexprReading::is_object(const SemaConstant& value) const
{
	return value.type != kNoType &&
		analyzer_.types_.is_class(analyzer_.types_.strip_cv(value.type));
}

TypeId ConstexprReading::entry_of(const SemaConstant& value) const
{
	return analyzer_.types_.value_type(value.type, value.bits);
}

SemaConstant ConstexprReading::constant_of(TypeId entry, const TypeTable& types)
{
	SemaConstant out;
	out.type = types.target(entry);
	out.bits = types.value_bits(entry);
	return out;
}

void ConstexprReading::data_members(TypeId type,
                                    std::vector<SemaEntity*>& out) const
{
	SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(type));
	if (owner == nullptr || owner->scope == nullptr)
	{
		return;
	}
	const Scope& region = *owner->scope;
	for (std::size_t index = 0; index < region.declarations.size(); ++index)
	{
		SemaEntity& member = *region.declarations[index];
		if (declares_subobject(member, region))
		{
			out.push_back(&member);
		}
	}
}

// 5.2.3p2 and p3: an object of literal class type built where a value belongs.
//
// The clauses initialize the non-static data members in declaration order,
// which is 8.5.1p2 exactly, and 8.5.1p7 value-initializes every member none of
// them reached.  What the object comes to is the interned list of what its
// members hold, so the class and that list together are the object: two
// namings of `S{5}` are one entry, and `S{5}` and `S{6}` are two.
SemaConstant ConstexprReading::object_of(TypeId type,
                                         const std::vector<SemaConstant>& written)
{
	const TypeId bare = analyzer_.types_.strip_cv(type);
	SemaEntity* owner = analyzer_.model_.type_owner(bare);
	if (owner != nullptr && owner->primary != nullptr && !owner->defined)
	{
		// 14.7.1p1 and 3.9p5: building an object of the class is a use that
		// requires it completely defined, and a spelling met while 5.4p2's
		// ambiguity was being probed made the declaration without asking for
		// one - so the ask is made here, where the use stands.
		analyzer_.asked_specialization(*owner);
	}
	analyzer_.require_complete_type(bare);
	owner = analyzer_.model_.type_owner(bare);
	if (owner == nullptr || owner->scope == nullptr || !owner->bases.empty())
	{
		// 10p1's base subobject is one this milestone's object does not hold:
		// nothing names it, so nothing would read it back.
		throw NotConstant(analyzer_.types_.description(bare) +
		                  " is not a class a constant expression builds an "
		                  "object of");
	}
	std::vector<SemaEntity*> members;
	data_members(bare, members);
	if (written.size() > members.size())
	{
		throw NotConstant("a constant expression writes more initializers than " +
		                  analyzer_.types_.description(bare) + " has members");
	}
	std::vector<TypeId> held;
	held.reserve(members.size());
	for (std::size_t index = 0; index < members.size(); ++index)
	{
		const TypeId member = analyzer_.types_.strip_cv(members[index]->type);
		// 8.5.1p2 and p7: the clause written for the member, or the zero
		// 8.5p7's value-initialization gives one no clause reached.
		SemaConstant value;
		value.type = member;
		if (index < written.size())
		{
			value = written[index];
			if (analyzer_.types_.strip_cv(value.type) != member)
			{
				value = analyzer_.convert(value, member);
			}
			value.type = member;
		}
		else if (analyzer_.types_.is_class(member))
		{
			// 8.5p7: a member of class type none of the clauses reached is
			// value-initialized, which for one of these is the object every one
			// of *its* members is value-initialized in.
			const std::vector<SemaConstant> none;
			value = object_of(member, none);
		}
		if (!analyzer_.types_.is_class(member) &&
		    analyzer_.arithmetic_type(member) == kNoType)
		{
			throw NotConstant("a member of " +
			                  analyzer_.types_.description(bare) +
			                  " is outside the values a constant expression "
			                  "holds");
		}
		held.push_back(entry_of(value));
	}
	SemaConstant out;
	out.type = bare;
	out.bits = analyzer_.types_.type_list(held);
	return out;
}

SemaConstant ConstexprReading::called(const AstNode& callee,
                                      const std::vector<SemaConstant>& arguments,
                                      const SemaContext& ctx)
{
	if (callee.kind != AstKind::IdExpression)
	{
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate");
	}
	SemaEntity* const named = analyzer_.resolve(callee.text, ctx, LookupKind::Any);
	if (named == nullptr)
	{
		throw NotConstant(callee.text +
		                  " is written where a constant expression calls and "
		                  "names no function");
	}
	return called_entity(*named, arguments);
}

SemaConstant ConstexprReading::called_entity(
	SemaEntity& named, const std::vector<SemaConstant>& arguments)
{
	SemaEntity* const one = named.kind == SemaKind::Function
		? chosen(named, arguments.size())
		: nullptr;
	if (one == nullptr)
	{
		throw NotConstant(named.name +
		                  " names no one constexpr function a constant "
		                  "expression may call with these arguments");
	}
	// 9.3.1p3: a call written with no object expression is one on the object
	// the function being read stands on, which 5.19p2 has no value for - so a
	// fold reaches only the declarations no object is needed to call.
	if (one->object_member)
	{
		throw NotConstant(named.name +
		                  " is called on an object a constant expression does "
		                  "not name");
	}
	return call(*one, nullptr, arguments);
}

SemaEntity* ConstexprReading::chosen(SemaEntity& named,
                                     std::size_t arguments) const
{
	SemaEntity* found = nullptr;
	for (SemaEntity* each = &named; each != nullptr; each = each->next)
	{
		if (each->kind != SemaKind::Function || !each->constexpr_function ||
		    each->constexpr_body == nullptr)
		{
			continue;
		}
		// 9.3.1p3 put the object parameter in the type, so the places a call
		// writes arguments for are the rest of them.
		const std::vector<TypeId>& places =
			analyzer_.types_.parameters(each->type);
		const std::size_t implicit = each->object_member ? 1 : 0;
		if (places.size() - implicit != arguments)
		{
			continue;
		}
		if (found != nullptr)
		{
			// The set leaves the choice to a ranking of conversions a fold has
			// no typed expressions to make, so it is refused rather than
			// guessed at.
			return nullptr;
		}
		found = each;
	}
	return found;
}

// 12.3.2p1 with 14.3.2p5: an object of class type brought to an integral type.
//
// A conversion function is a member function of no parameters whose name is the
// type it converts to, so what the fold does is call it on the object.  The
// class's own conversions are what 13.3.1.5p1 offers first; a class that
// declares one to the very type the place asked for is chosen over one that
// reaches it by a further standard conversion, which is the whole of the
// ranking a set of constant answers can bear.
SemaConstant ConstexprReading::converted(const SemaConstant& value,
                                         TypeId place)
{
	SemaEntity* const owner =
		analyzer_.model_.type_owner(analyzer_.types_.strip_cv(value.type));
	if (owner == nullptr)
	{
		throw NotConstant("a constant expression converts an object of a type "
		                  "it does not know");
	}
	SemaEntity* found = nullptr;
	for (std::size_t index = 0; index < owner->conversions.size(); ++index)
	{
		SemaEntity& each = *owner->conversions[index];
		if (!each.constexpr_function || each.constexpr_body == nullptr)
		{
			continue;
		}
		const TypeId to = analyzer_.types_.target(each.type);
		if (analyzer_.arithmetic_type(to) == kNoType)
		{
			continue;
		}
		if (place != kNoType && analyzer_.types_.strip_cv(to) == place)
		{
			found = &each;
			break;
		}
		if (found == nullptr)
		{
			found = &each;
		}
	}
	if (found == nullptr)
	{
		throw NotConstant(analyzer_.types_.description(value.type) +
		                  " declares no constexpr conversion function a "
		                  "constant expression reaches a value through");
	}
	const std::vector<SemaConstant> none;
	return call(*found, &value, none);
}

// 7.1.5p3's function-body, read for the one expression it comes to.
const AstNode* ConstexprReading::return_expression(const AstNode& body,
                                                   const SemaContext& inner)
{
	const AstNode* returned = nullptr;
	for (std::size_t index = 0; index < body.children.size(); ++index)
	{
		const AstNode& statement = *body.children[index];
		if (statement.kind == AstKind::ReturnStatement)
		{
			if (returned != nullptr || statement.children.empty())
			{
				return nullptr;
			}
			returned = statement.children[0];
			continue;
		}
		if (!is_body_declaration(statement.kind))
		{
			return nullptr;
		}
		// The names these introduce are what the return expression is read
		// against, so they are declared into the fold's own region - which is
		// what lets a body write `using min = ...;` before it names `min`.
		analyzer_.declaration(statement, inner);
	}
	return returned;
}

void ConstexprReading::bind_constant(const std::string& name,
                                     const SemaConstant& value,
                                     const SemaContext& inner)
{
	SemaEntity& bound =
		analyzer_.model_.create(SemaKind::Variable, name, value.type);
	bound.constant = true;
	bound.value = value.bits;
	bound.region = inner.scope;
	analyzer_.model_.bind(*inner.scope, name, bound);
	analyzer_.model_.declare_in(*inner.scope, bound);
}

void ConstexprReading::bind_arguments(SemaEntity& callee,
                                      const SemaConstant* object,
                                      const std::vector<SemaConstant>& arguments,
                                      const SemaContext& inner)
{
	if (object != nullptr)
	{
		// 9.3.1p3 and 9.2p1: what a member named with no object expression
		// denotes is the object the function was called on, so the object's
		// own subobjects are bindings of this region - one constant each,
		// under the name the member was declared with.
		std::vector<SemaEntity*> members;
		data_members(object->type, members);
		const std::vector<TypeId>& held =
			analyzer_.types_.type_list_at(static_cast<std::uint32_t>(object->bits));
		for (std::size_t index = 0;
		     index < members.size() && index < held.size(); ++index)
		{
			bind_constant(members[index]->name,
			              constant_of(held[index], analyzer_.types_), inner);
		}
	}
	// 8.3.5p10 and 5.2.2p4: the places the declarator wrote, each bound to what
	// the argument written for it came to after 8.5's conversion to its type.
	std::vector<std::string> named;
	parameter_names(*callee.constexpr_body, named);
	for (std::size_t index = 0;
	     index < arguments.size() && index < named.size(); ++index)
	{
		if (named[index].empty())
		{
			continue;
		}
		bind_constant(named[index], arguments[index], inner);
	}
}

SemaConstant ConstexprReading::call(SemaEntity& callee,
                                    const SemaConstant* object,
                                    const std::vector<SemaConstant>& arguments)
{
	if (!callee.constexpr_function || callee.constexpr_body == nullptr ||
	    callee.constexpr_region == nullptr)
	{
		throw NotConstant(callee.name +
		                  " is not a constexpr function this unit has defined");
	}
	const TypeId result = analyzer_.types_.target(callee.type);
	// 8.3.5p10: each argument is converted to the type of its place before the
	// body reads it, which is what makes the fold a fact of the converted list
	// and not of the spellings that wrote it.
	const std::vector<TypeId>& places = analyzer_.types_.parameters(callee.type);
	const std::size_t implicit = callee.object_member ? 1 : 0;
	std::vector<SemaConstant> passed;
	passed.reserve(arguments.size());
	std::vector<TypeId> key;
	key.reserve(arguments.size() + 2);
	key.push_back(object == nullptr ? kNoType : entry_of(*object));
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		const TypeId place = index + implicit < places.size()
			? places[index + implicit]
			: kNoType;
		SemaConstant given = arguments[index];
		if (place != kNoType && analyzer_.arithmetic_type(place) != kNoType)
		{
			given = analyzer_.convert(given, place);
			given.type = place;
		}
		passed.push_back(given);
		key.push_back(entry_of(given));
	}
	const std::uint32_t list = analyzer_.types_.type_list(key);
	const TypeId held = analyzer_.model_.folded_call(callee, list);
	if (held != kNoType)
	{
		return constant_of(held, analyzer_.types_);
	}
	unsigned& depth = analyzer_.model_.folding_depth();
	if (depth >= kMaxConstexprDepth)
	{
		throw NotConstant("a constant expression calls constexpr functions "
		                  "more deeply than this implementation reads");
	}
	const ReadingDepth folding(depth);
	// 14.6.1p1 and 3.3.3: a region of the fold's own, so the bindings one call
	// makes are not the ones another sees and the body's own names are looked
	// up over the region its declarator opened.
	SemaContext inner;
	inner.scope = &analyzer_.model_.open(ScopeKind::Block,
	                                     *callee.constexpr_region, nullptr,
	                                     callee.constexpr_region->dump);
	inner.dump = callee.constexpr_region->dump;
	bind_arguments(callee, object, passed, inner);
	const AstNode& body = *callee.constexpr_body;
	const AstNode* compound = nullptr;
	for (std::size_t index = 0; index < body.children.size(); ++index)
	{
		if (body.children[index]->kind == AstKind::CompoundStatement)
		{
			compound = body.children[index];
			break;
		}
	}
	if (compound == nullptr)
	{
		throw NotConstant(callee.name +
		                  " has no function-body a constant expression reads");
	}
	const AstNode* const returned = return_expression(*compound, inner);
	if (returned == nullptr)
	{
		throw NotConstant(callee.name +
		                  " is a constexpr function whose body is outside what "
		                  "7.1.5p3 leaves a constant expression to read");
	}
	SemaConstant answer = analyzer_.evaluate(*returned, inner);
	if (analyzer_.arithmetic_type(result) != kNoType)
	{
		// 6.6.3p2: the value the return statement's expression is converted to
		// the return type, which is what the caller reads.
		answer = analyzer_.convert(answer, result);
		answer.type = result;
	}
	analyzer_.model_.hold_folded_call(callee, list, entry_of(answer));
	return answer;
}

SemaConstant ConstexprReading::id_constant(const AstNode& node,
                                           const SemaContext& ctx)
{
	// 7.1.6.2p1: a nested-name-specifier that begins with a decltype-specifier
	// reaches its region through the expression the parser kept beside the
	// name, which no spelling holds - so 5.19's reading asks the same question
	// of an id-expression that every other reader of one asks.
	SemaEntity* const named =
		child_kind(node, AstKind::CarriedExpression) == nullptr
		? analyzer_.resolve(node.text, ctx, LookupKind::Any)
		: analyzer_.decltype_qualified_name(node, ctx, LookupKind::Any);
	SemaEntity& entity = analyzer_.require(named, node.text);
	if (!entity.constant)
	{
		if (analyzer_.checking_ > 0 && analyzer_.types_.is_dependent(entity.type))
		{
			// 14.6p8: what a name that depends on a template parameter is
			// worth, an argument list is what says.  The reading stands one
			// value in its place, as it does for the size of a dependent type.
			++analyzer_.stood_in_;
			SemaConstant stood;
			stood.type = analyzer_.types_.fundamental(FT_INT);
			stood.bits = 1;
			return stood;
		}
		throw NotConstant(node.text + " is not a constant expression");
	}
	SemaConstant out;
	out.type = entity.type;
	out.bits = entity.value;
	return out;
}

SemaConstant ConstexprReading::unary_constant(const AstNode& node,
                                              const SemaContext& ctx)
{
	const SemaConstant operand = analyzer_.promote(analyzer_.evaluate(*node.children[0], ctx));
	SemaConstant out;
	out.type = operand.type;
	switch (node.token)
	{
	case OP_PLUS:
		out.bits = operand.bits;
		break;

	case OP_MINUS:
	{
		if (analyzer_.is_signed(out.type) && analyzer_.width_of(out.type) == 64 &&
		    operand.bits == (1ULL << 63))
		{
			throw NotConstant("a constant expression overflows");
		}
		out.bits = 0ULL - operand.bits;
		break;
	}

	case OP_COMPL:
		out.bits = ~operand.bits;
		break;

	case OP_LNOT:
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = operand.bits == 0 ? 1 : 0;
		return out;

	default:
		throw NotConstant("a constant expression holds an operator PA11 "
		                         "does not analyzer_.evaluate");
	}
	return analyzer_.convert(out, out.type);
}

SemaConstant ConstexprReading::binary_constant(const AstNode& node,
                                               const SemaContext& ctx)
{
	// 5.14p1 and 5.15p1: the right operand of `&&` and `||` is evaluated only
	// when the left one does not decide the answer.
	if (node.token == OP_LAND || node.token == OP_LOR)
	{
		const bool left = analyzer_.evaluate(*node.children[0], ctx).bits != 0;
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		if (left == (node.token == OP_LOR))
		{
			out.bits = left ? 1 : 0;
			return out;
		}
		out.bits = analyzer_.evaluate(*node.children[1], ctx).bits != 0 ? 1 : 0;
		return out;
	}

	const SemaConstant left = analyzer_.promote(analyzer_.evaluate(*node.children[0], ctx));
	const SemaConstant right = analyzer_.promote(analyzer_.evaluate(*node.children[1], ctx));
	const bool comparison = node.token == OP_LT || node.token == OP_GT ||
		node.token == OP_LE || node.token == OP_GE || node.token == OP_EQ ||
		node.token == OP_NE;
	// 5.8p1: a shift takes 5p10's conversions on neither operand.  Each is
	// promoted on its own and the result has the type of the promoted left one,
	// so an unsigned count does not make the value it shifts unsigned.
	const bool shifted = node.token == OP_LSHIFT || node.token == OP_RSHIFT;
	const TypeId type =
		shifted ? left.type : analyzer_.common_type(left.type, right.type);
	const unsigned long long lhs = analyzer_.convert(left, type).bits;
	const unsigned long long rhs = shifted ? right.bits : analyzer_.convert(right, type).bits;
	const bool sign = analyzer_.is_signed(type);
	const long long signed_lhs = static_cast<long long>(lhs);
	const long long signed_rhs = static_cast<long long>(rhs);

	if (comparison)
	{
		bool answer = false;
		switch (node.token)
		{
		case OP_LT: answer = sign ? signed_lhs < signed_rhs : lhs < rhs; break;
		case OP_GT: answer = sign ? signed_lhs > signed_rhs : lhs > rhs; break;
		case OP_LE: answer = sign ? signed_lhs <= signed_rhs : lhs <= rhs; break;
		case OP_GE: answer = sign ? signed_lhs >= signed_rhs : lhs >= rhs; break;
		case OP_EQ: answer = lhs == rhs; break;
		default: answer = lhs != rhs; break;
		}
		SemaConstant out;
		out.type = analyzer_.types_.fundamental(FT_BOOL);
		out.bits = answer ? 1 : 0;
		return out;
	}

	SemaConstant out;
	out.type = type;
	long long result = 0;
	bool overflowed = false;
	switch (node.token)
	{
	case OP_PLUS:
		overflowed = add_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs + rhs;
		break;

	case OP_MINUS:
		overflowed = subtract_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs - rhs;
		break;

	case OP_STAR:
		overflowed = multiply_overflows(signed_lhs, signed_rhs, result);
		out.bits = lhs * rhs;
		break;

	case OP_DIV:
	case OP_MOD:
		if (rhs == 0)
		{
			throw NotConstant("a constant expression divides by zero");
		}
		if (sign)
		{
			overflowed = signed_lhs == (-1 - 0x7FFFFFFFFFFFFFFFLL) && signed_rhs == -1;
			result = node.token == OP_DIV ? signed_lhs / signed_rhs
			                              : signed_lhs % signed_rhs;
			out.bits = static_cast<unsigned long long>(result);
			break;
		}
		out.bits = node.token == OP_DIV ? lhs / rhs : lhs % rhs;
		break;

	case OP_LSHIFT:
	case OP_RSHIFT:
	{
		const unsigned long long count =
			analyzer_.is_signed(right.type) && static_cast<long long>(rhs) < 0
				? static_cast<unsigned long long>(analyzer_.width_of(type))
				: rhs;
		if (count >= analyzer_.width_of(type))
		{
			throw NotConstant("a constant expression shifts by more than "
			                         "the width of its type");
		}
		if (node.token == OP_LSHIFT)
		{
			out.bits = lhs << count;
			result = static_cast<long long>(out.bits);
			// 5.8p2: a signed left operand shall be non-negative, and the value
			// shall be representable in the *unsigned* type of the same width -
			// so `1LL << 63` is the sign bit and not an overflow, which is what
			// the bits shifted past the width say.
			overflowed = sign &&
				(signed_lhs < 0 ||
				 (count != 0 && (lhs >> (analyzer_.width_of(type) - count)) != 0));
			break;
		}
		out.bits = sign ? static_cast<unsigned long long>(signed_lhs >> count)
		                : lhs >> count;
		break;
	}

	case OP_AMP: out.bits = lhs & rhs; break;
	case OP_BOR: out.bits = lhs | rhs; break;
	case OP_XOR: out.bits = lhs ^ rhs; break;

	default:
		throw NotConstant("a constant expression holds an operator PA11 "
		                         "does not analyzer_.evaluate");
	}

	// 5p4: an operation whose result its type cannot represent has undefined
	// behaviour, so it is not a constant expression.  5.8p2's shift is the one
	// operation whose signed result may hold the sign bit and still be the
	// value the clause names, so it answers for itself.
	const SemaConstant narrowed = analyzer_.convert(out, type);
	if (sign && (overflowed || (!shifted && narrowed.bits != out.bits)))
	{
		throw NotConstant("a constant expression overflows");
	}
	return narrowed;
}

// 5.2.3p1, 5.2.3p2/p3 and 5.2.2p1: the one shape the grammar hands on as a
// call, which is a cast written in functional notation, an object of literal
// class type, or a call of a function - and which of the three it is, is
// settled by the one lookup of the name before the parentheses.
SemaConstant ConstexprReading::call_or_cast(const AstNode& node,
                                           const SemaContext& ctx)
{
	// 5.2.3p1: `T(x)` is the cast `(T)x` written in functional notation,
	// which the grammar hands on as a call because it cannot say whether
	// the name before the parentheses is a type.  A call of a *function*
	// is the other reading of the shape and is 5.19p2's constexpr
	// function, so only the arm whose callee names an arithmetic type
	// folds here.
	const AstNode& callee = *node.children[0];
	TypeId target = kNoType;
	if (callee.kind == AstKind::IdExpression)
	{
		target = analyzer_.keyword_type(callee.text);
	}
	if (target == kNoType && callee.kind == AstKind::IdExpression)
	{
		// 7.1.6.2p1: the simple-type-specifier may be a typedef-name or an
		// enum-name the program declared, which 3.4 answers for - and a
		// name that reaches no region at all is a call and not a cast.
		try
		{
			SemaEntity* const named =
				analyzer_.resolve(callee.text, ctx, LookupKind::Type);
			target = named == nullptr ? kNoType : named->type;
		}
		catch (const std::exception&)
		{
			target = kNoType;
		}
	}
	// 5.2.3p3: `T{x}` is written where `T(x)` is, and the braces put
	// 8.5.4's one initializer-clause where the operand stands.
	const AstNode* list =
		node.children.size() < 2 ? nullptr : node.children[1];
	if (list != nullptr && list->braced)
	{
		list = list->children.empty() ? nullptr : list->children[0];
	}
	if (list == nullptr)
	{
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate");
	}
	// 14.5.3p4: what the parentheses hold is the run a `pattern...` entry
	// stands for, which is the same reading the analyzer_.lowering of this cast is
	// given - 5.2.3's one rule has one answer here too.  It is read once
	// here, because a cast, an object of class type and 5.2.2's call all
	// take the operands the same list wrote.
	InitializerClauses written(list, analyzer_, ctx);
	if (written.list.unsettled())
	{
		// 14.6p8: a run no argument list has settled says neither how many
		// operands the cast has nor what they are worth, so the reading
		// stands a value in for it exactly as it does for `sizeof...`.
		// The stand-in is 1 rather than 0 because 8.3.4p1 gives an array
		// no bound of zero.
		++analyzer_.stood_in_;
		SemaConstant out;
		out.type = target == kNoType ? analyzer_.types_.fundamental(FT_INT) : target;
		out.bits = 1;
		return out;
	}
	std::vector<SemaConstant> operands;
	operands.reserve(written.list.size());
	while (!written.spent())
	{
		const SemaContext inner = written.in(ctx);
		const AstNode& clause = written.next();
		++written.at;
		operands.push_back(analyzer_.evaluate(clause, inner));
	}
	if (target != kNoType && analyzer_.types_.is_class(analyzer_.types_.strip_cv(target)))
	{
		// 5.2.3p2 and p3: an object of literal class type, which at a value
		// place is what 12.3.2p1's conversion function reads.
		return object_of(target, operands);
	}
	if (target == kNoType)
	{
		// 5.2.2p1 with 7.1.5p2: the other reading of the shape, which is a
		// call of a function - and only a constexpr function whose body
		// this unit has read is one 5.19p2 folds.
		return called(callee, operands, ctx);
	}
	if (analyzer_.arithmetic_type(target) == kNoType || operands.size() > 1)
	{
		throw NotConstant("a constant expression calls something this "
		                  "milestone does not evaluate");
	}
	if (operands.empty())
	{
		// 5.2.3p2: `T()` is the value-initialization 8.5p7 writes, which
		// for an arithmetic type is the zero 8.5p6 converts to it.
		SemaConstant out;
		out.type = target;
		out.bits = 0;
		return out;
	}
	SemaConstant out = analyzer_.convert(operands[0], target);
	out.type = target;
	return out;
}
