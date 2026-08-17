#include "sema_address.h"

#include "sema_analyzer.h"

#include <algorithm>

#include "ast_model.h"
#include "post_token.h"
#include "sema_constexpr.h"
#include "sema_derivation.h"
#include "string_literal.h"

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

}

// 5.19p2's address constant expression.
//
// This is the one kind of constant `SemaConstant` holds that is not a value at
// all.  3.9.1p8's two kinds of arithmetic value say what an object is worth;
// an address says *which object*, and the two questions never meet: no
// conversion brings one to the other, no operator over one is an operator over
// the other, and the only place they touch is 4.12p1's `bool`, where a pointer
// is worth whether it designates anything.
//
// So the file owns both halves of that.  `AddressTable` is the pool of objects
// designated so far, which is what lets an address be one number and therefore
// one interned constant; the readings below are 5.3.1p3's `&`, 5.3.1p1's `*`,
// 4.2p1's decay, 5.7's arithmetic and 5.9/5.10's comparisons, each asked from
// the reading of the operator that stands in the program.

AddressTable::AddressTable()
{
	// 4.10p1: identifier zero is the null pointer value, which designates no
	// object - so every real address is a number a comparison can tell from it
	// and `if (p)` is one test of the identifier.
	held_.push_back(ConstantAddress());
}

AddressTable::Key AddressTable::key_of(const ConstantAddress& address)
{
	return Key(std::make_pair(address.object, address.literal),
	           std::make_pair(address.path,
	                          std::make_pair(address.temporary,
	                                         std::make_pair(address.automatic,
	                                                        address.past_end))));
}

std::uint32_t AddressTable::intern(const ConstantAddress& address)
{
	const Key key = key_of(address);
	const std::map<Key, std::uint32_t>::const_iterator held = ids_.find(key);
	if (held != ids_.end())
	{
		return held->second;
	}
	const std::uint32_t id = static_cast<std::uint32_t>(held_.size());
	held_.push_back(address);
	ids_.insert(std::make_pair(key, id));
	return id;
}

const ConstantAddress& AddressTable::at(std::uint32_t id) const
{
	return held_[id < held_.size() ? id : 0];
}

bool ConstexprReading::holds_address(const SemaConstant& value) const
{
	return value.type != kNoType &&
		analyzer_.types_.kind(analyzer_.types_.strip_cv(value.type)) ==
			TypeKind::Pointer;
}

// 4.10p1: whether the operand is a null pointer constant, which an integral
// value of zero is wherever a pointer belongs.
bool ConstexprReading::null_pointer_operand(const SemaConstant& value) const
{
	if (value.type == kNoType || !value.valued)
	{
		return false;
	}
	const TypeId bare = analyzer_.types_.strip_cv(value.type);
	if (analyzer_.types_.kind(bare) == TypeKind::Fundamental &&
	    analyzer_.types_.fundamental_type(bare) == FT_NULLPTR_T)
	{
		return true;
	}
	return analyzer_.integral_type(bare) != kNoType && value.bits == 0;
}

SemaConstant ConstexprReading::pointer_constant(std::uint32_t address,
                                                TypeId place)
{
	SemaConstant out;
	out.type = place != kNoType
		? place
		: analyzer_.types_.pointer_to(address_type(address));
	out.bits = address;
	return out;
}

// The type of the object an address designates, walked one level per index of
// its path.
//
// 9.2p13's members and 8.3.4p6's elements are the two levels there are, and
// each answers the same question about the index standing at it - so a path n
// deep costs n reads of a type and no search at all.
TypeId ConstexprReading::address_type(std::uint32_t address) const
{
	const ConstantAddress& held = analyzer_.model_.addresses().at(address);
	TypeId type = kNoType;
	if (held.object != nullptr)
	{
		type = held.object->type;
	}
	else if (held.temporary != kNoType)
	{
		type = analyzer_.types_.target(held.temporary);
	}
	else if (!held.literal.empty())
	{
		type = literal_type(held.literal);
	}
	for (std::size_t at = 0; type != kNoType && at < held.path.size(); ++at)
	{
		const TypeId bare = analyzer_.types_.strip_cv(type);
		if (analyzer_.types_.kind(bare) == TypeKind::Array)
		{
			type = analyzer_.types_.target(bare);
			continue;
		}
		std::vector<Subobject> members;
		subobjects(bare, members);
		type = held.path[at] < members.size()
			? members[static_cast<std::size_t>(held.path[at])].type
			: kNoType;
	}
	return type;
}

// 2.14.5p8: the array object a string literal is, as the type of that object.
TypeId ConstexprReading::literal_type(const std::string& spelling) const
{
	PostToken token;
	if (scan_literal(spelling, token) != LiteralForm::String ||
	    token.kind != PostTokenKind::LiteralArray)
	{
		return kNoType;
	}
	// 2.14.5p8 and 8.3.4p1: an array of `n` const elements of the type phase 6
	// gave the literal, whose bound counts 2.14.5p12's terminating null too.
	return analyzer_.types_.array_of(
		analyzer_.types_.qualified(analyzer_.types_.fundamental(token.type),
		                           kCvConst),
		true, token.element_count);
}

// 8.3.4p6 and 5.7p4: how many elements the array an address indexes has, which
// is what says where one past its end is.  An object that is no element of an
// array is 5.7p4's array of one.
unsigned long long ConstexprReading::address_bound(
	const ConstantAddress& held) const
{
	if (held.path.empty())
	{
		return 1;
	}
	// Read out of `held` before the pool is asked for anything: interning the
	// enclosing object may grow it, and `held` is as often an entry of it.
	const unsigned long long last = held.path.back();
	ConstantAddress parent = held;
	parent.path.pop_back();
	parent.past_end = false;
	const TypeId type = analyzer_.types_.strip_cv(
		address_type(analyzer_.model_.addresses().intern(parent)));
	if (analyzer_.types_.kind(type) != TypeKind::Array)
	{
		// 9.2p13's member is no element of an array either, so a pointer to one
		// walks past it and nowhere else.
		return last + 1;
	}
	return analyzer_.types_.bounded(type) ? analyzer_.types_.bound(type) : 0;
}

std::uint32_t ConstexprReading::containing_object(std::uint32_t address,
                                                  TypeId derived)
{
	SemaEntity* const owner = analyzer_.model_.type_owner(
		analyzer_.types_.strip_cv(address_type(address)));
	std::vector<unsigned long long> path;
	if (owner == nullptr ||
	    !Derivation(analyzer_).subobject_path(derived, *owner, path) ||
	    path.empty())
	{
		return 0;
	}
	ConstantAddress held = analyzer_.model_.addresses().at(address);
	if (held.past_end || held.path.size() < path.size())
	{
		return 0;
	}
	const std::size_t taken = held.path.size() - path.size();
	for (std::size_t at = 0; at < path.size(); ++at)
	{
		if (held.path[taken + at] != path[at])
		{
			// The steps the address was reached by are not the ones the
			// derivation names, so the object it designates is a subobject of
			// something else that happens to be of this class.
			return 0;
		}
	}
	held.path.resize(taken);
	return analyzer_.model_.addresses().intern(held);
}

std::uint32_t ConstexprReading::subobject_address(std::uint32_t base,
                                                  unsigned long long index)
{
	ConstantAddress held = analyzer_.model_.addresses().at(base);
	if (held.past_end)
	{
		throw NotConstant("a constant expression reads a subobject of an object "
		                  "one past the end of an array");
	}
	held.path.push_back(index);
	return analyzer_.model_.addresses().intern(held);
}

// 5.19p2: what the object an address designates is holding.
//
// The base is a declaration or 2.14.5p8's literal, and the path down it is read
// against the interned list the base's own constant holds - which is the same
// list a member access and a subscript read, because the path indexes exactly
// what they index.
SemaConstant ConstexprReading::loaded(std::uint32_t address)
{
	const ConstantAddress& held = analyzer_.model_.addresses().at(address);
	if (address == 0)
	{
		throw NotConstant("a constant expression reads through a null pointer");
	}
	if (held.past_end)
	{
		throw NotConstant("a constant expression reads through a pointer one "
		                  "past the end of an object");
	}
	SemaConstant value;
	if (held.object == nullptr && held.temporary == kNoType)
	{
		// 2.14.5p8 with 5.19p2: the literal's own storage, whose elements are
		// the code units phase 6 built and which no declaration named.
		if (held.path.size() != 1)
		{
			throw NotConstant("a constant expression reads a string literal "
			                  "other than one element at a time", false);
		}
		value = analyzer_.string_element(held.literal, held.path[0]);
		value.object = address;
		return value;
	}
	if (held.object == nullptr)
	{
		// 12.2p1: the temporary holds the value it was made from, which is the
		// entry it was interned by.
		value = constant_of(held.temporary, analyzer_.types_);
	}
	else if (!held.object->constant)
	{
		if (analyzer_.checking_ > 0 &&
		    (analyzer_.types_.is_dependent(held.object->type) ||
		     !held.object->covered_constant))
		{
			// 14.6p8: the object this address designates is a declaration of
			// the pattern, and what it holds is what an argument list will say
			// - `static constexpr bool values[] = { is_long<T>::value... };`
			// has as many elements as the pack has and no value until then.  So
			// the reading stands one value in its place, exactly as a name of
			// dependent type takes one, and the specialization reads the object
			// the arguments made.
			++analyzer_.stood_in_;
			SemaConstant stood;
			stood.type = address_type(address);
			stood.bits = 1;
			if (stood.type == kNoType)
			{
				stood.type = analyzer_.types_.fundamental(FT_INT);
			}
			return stood;
		}
		throw NotConstant(held.object->name + " is not a constant expression",
		                  held.object->covered_constant);
	}
	else
	{
		value.type = held.object->type;
		value.bits = held.object->value;
		value.real = held.object->real;
	}
	for (std::size_t at = 0; at < held.path.size(); ++at)
	{
		if (!holds_list(value))
		{
			throw NotConstant("a constant expression reads a subobject of " +
			                  analyzer_.types_.description(value.type) +
			                  ", which holds none", false);
		}
		const std::vector<TypeId>& subobjects =
			analyzer_.types_.type_list_at(
				static_cast<std::uint32_t>(value.bits));
		if (held.path[at] >= subobjects.size())
		{
			throw NotConstant("a constant expression reads outside the object an "
			                  "address designates");
		}
		value = constant_of(subobjects[static_cast<std::size_t>(held.path[at])],
		                    analyzer_.types_);
	}
	value.object = address;
	return value;
}

// 3.10p1 and 8.5p11: the object an address designates, read as an operand.
//
// An operand that stands where a value belongs is worth what the object holds,
// and one that stands where an object does needs only which object it is - so
// the two are one reading here, and the second is what a `static int n;` and a
// declaration whose own fold ran out still answer.
SemaConstant ConstexprReading::held_at(std::uint32_t address)
{
	const ConstantAddress& held = analyzer_.model_.addresses().at(address);
	if (address != 0 && !held.past_end &&
	    (held.object == nullptr || held.object->constant))
	{
		return loaded(address);
	}
	SemaConstant out;
	out.type = address_type(address);
	out.object = address;
	out.valued = false;
	return out;
}

// 5.19 asked of a refusal about the object an address designates: whether it is
// the answer about the *program* or this reading running out.
//
// A refusal that an object holds no value the fold may read is the program's
// error where 5.19 gives that object no constant value - `static int n;` is one
// - and this build's edge where the object's own initializer is one the fold ran
// out on, which is exactly what `SemaEntity::covered_constant` was made to
// carry.  So the two are one question and the answer is the declaration's, one
// name further along.
bool ConstexprReading::covered_object(std::uint32_t address) const
{
	const SemaEntity* const named = analyzer_.model_.addresses().at(address).object;
	return named == nullptr || named->constant || named->covered_constant;
}

// 3.10p2 and 5.19p2: the object `node` designates.
//
// This is the lvalue reading beside `evaluate`'s value one, and it exists
// because the two answer different questions about the same expression: `n` is
// worth what the declaration folded to, and `&n` is worth *which* declaration
// it is - which a `static int n;` with no constant value of its own still has.
// So every shape 3.10p1 makes an lvalue is read here, and anything else is read
// as a value and asked what object that value came out of.
//
// `value_fallback` is that last sentence asked as a question, because there is
// one caller for whom the answer is already known: `operand_constant` reaches
// this walk *because* the value reading refused, so reading the same node as a
// value a second time cannot answer and costs the whole subtree twice.  Two of
// them nested is four walks and n of them is 2^n, which is what a call written
// on a call written on a call is.
std::uint32_t ConstexprReading::designated(const AstNode& node,
                                           const SemaContext& ctx,
                                           bool value_fallback)
{
	switch (node.kind)
	{
	case AstKind::ParenthesizedExpression:
		return designated(*node.children[0], ctx, value_fallback);

	case AstKind::Literal:
	{
		// 2.14.5p8: a string literal is an object of static storage duration,
		// which is exactly what 5.19p2's address constant asks for.
		if (literal_type(node.text) == kNoType)
		{
			throw NotConstant("a constant expression takes the address of a "
			                  "literal that names no object", false);
		}
		ConstantAddress held;
		held.literal = node.text;
		return analyzer_.model_.addresses().intern(held);
	}

	case AstKind::IdExpression:
	{
		SemaEntity* const named =
			child_kind(node, AstKind::CarriedExpression) == nullptr
			? analyzer_.resolve(node.text, ctx, LookupKind::Any)
			: analyzer_.decltype_qualified_name(node, ctx, LookupKind::Any);
		return designated_entity(analyzer_.require(named, node.text), node.text);
	}

	case AstKind::MemberExpression:
	{
		if (node.children.size() < 2)
		{
			break;
		}
		// 5.2.5p1: `E.m` designates the subobject of the object `E` designates,
		// and `E->m` the one of the object `E` points to - so the two differ in
		// how the object on the left is reached and in nothing after that.
		const std::uint32_t object = node.token == OP_ARROW
			? pointed_object(analyzer_.evaluate(*node.children[0], ctx))
			: designated(*node.children[0], ctx, value_fallback);
		return member_address(object, node.children[1]->text, ctx);
	}

	case AstKind::SubscriptExpression:
	{
		// 5.2.1p1: `E1[E2]` is `*(E1 + E2)`, so which object it designates is
		// 5.7p5's element - of the array `E1` designates where it is one, and of
		// the array the pointer `E1` came to points into where it is not.
		// 13.5.5p1 gives an operand of class type a third reading, which is a
		// call and designates whatever its answer does, so it falls through to
		// the reading below rather than being answered here.
		const std::uint32_t base =
			array_object(*node.children[0], ctx, value_fallback);
		if (base == 0)
		{
			break;
		}
		return advanced(base, static_cast<long long>(
			                     counted(analyzer_.evaluate(*node.children[1],
			                                                ctx))));
	}

	case AstKind::UnaryExpression:
		if (node.token == OP_STAR)
		{
			// 5.3.1p1: `*p` designates the object `p` points to.
			return pointed_object(analyzer_.evaluate(*node.children[0], ctx));
		}
		break;

	default:
		break;
	}
	// Everything else is read as a value and asked which object that value came
	// out of - which is how a call that hands back a reference, and 13.5.6's
	// `operator->`, reach this reading without a shape of their own here.
	if (!value_fallback)
	{
		throw NotConstant("a constant expression takes the address of something "
		                  "that designates no object", false);
	}
	const SemaConstant value = analyzer_.evaluate(node, ctx);
	if (value.object == 0)
	{
		throw NotConstant("a constant expression takes the address of something "
		                  "that designates no object", false);
	}
	return value.object;
}

// 5.19p2: the object a name designates, which for 8.3.2p1's reference is the
// object it was bound to and for every other declaration is its own storage.
std::uint32_t ConstexprReading::designated_entity(SemaEntity& entity,
                                                  const std::string& spelling)
{
	if (entity.address != 0)
	{
		// The answer is held on the declaration: an address is a fact of the
		// object and not of the naming, so a name read n times interns once.
		return entity.address;
	}
	if (entity.kind != SemaKind::Variable && entity.kind != SemaKind::Parameter &&
	    entity.kind != SemaKind::Function)
	{
		throw NotConstant(spelling +
		                  " names no object or function a constant expression "
		                  "takes the address of", false);
	}
	if (analyzer_.types_.is_reference(entity.type))
	{
		// A reference this reading never bound - one the program declared - is
		// one whose object no fold knows, so what it designates is not a
		// question the values answer.
		throw NotConstant(spelling +
		                  " is a reference a constant expression does not know "
		                  "the object of", false);
	}
	ConstantAddress held;
	held.object = &entity;
	// 3.7.3 with 5.19p2: a place a call filled and an object a body declared
	// belong to the evaluation, and an address constant expression designates
	// an object with static storage duration - so the fact travels with the
	// address and is asked where one becomes a value the program keeps.
	held.automatic = entity.fold_local;
	entity.address = analyzer_.model_.addresses().intern(held);
	return entity.address;
}

// 5.2.5p1 over an address: the subobject `name` denotes of the object it
// designates, which is the index 9.2p13 gave the member.
std::uint32_t ConstexprReading::member_address(std::uint32_t object,
                                               const std::string& name,
                                               const SemaContext& ctx)
{
	const TypeId type = address_type(object);
	SemaConstant standing;
	standing.type = type;
	standing.valued = false;
	SemaEntity* const named = accessed_member(standing, name, ctx);
	if (named == nullptr)
	{
		throw NotConstant(name + " names no member of " +
		                  analyzer_.types_.description(type) +
		                  " a constant expression designates", false);
	}
	if (!named->object_member && named->kind != SemaKind::Function)
	{
		// 9.4p2: a member declared `static` is an object of its own, and the
		// object expression says nothing about which one it is.
		return designated_entity(*named, name);
	}
	std::vector<unsigned long long> path;
	if (!member_path(type, *named, path))
	{
		throw NotConstant(name + " names no subobject of " +
		                  analyzer_.types_.description(type) +
		                  " a constant expression designates", false);
	}
	// 10.2: a name the lookup found in a base is reached through the entry its
	// base class subobject stands at, so the address walks one step per class
	// on the path and the last of them is 9.2p13's member.
	std::uint32_t at = object;
	for (std::size_t step = 0; step < path.size(); ++step)
	{
		at = subobject_address(at, path[step]);
	}
	return at;
}

// 5.19p2: the object a value of pointer type points to.
std::uint32_t ConstexprReading::pointed_object(const SemaConstant& value)
{
	if (!holds_address(value) || !value.valued)
	{
		throw NotConstant("a constant expression reads through something that is "
		                  "not a pointer it holds a value of", false);
	}
	if (value.bits == 0)
	{
		throw NotConstant("a constant expression reads through a null pointer");
	}
	return static_cast<std::uint32_t>(value.bits);
}

// 5.2.1p1 and 5.7p5: the element a subscript names is one of an array or one a
// pointer points into, and which of the two is what the left operand is.
std::uint32_t ConstexprReading::array_object(const AstNode& node,
                                             const SemaContext& ctx,
                                             bool value_fallback)
{
	// 4.2p1: a name of array type is worth the address of its first element and
	// never a value, which `entity_constant` is the one door that answers - so
	// `numbers[2]` over `int numbers[4];` reads that address here like any other
	// pointer, and this reading asks the declaration nothing of its own.
	// 5.19p2's own reading of a string literal stays where it is: its elements
	// are read out of the literal and the array itself is an object of static
	// storage duration, which is what `designated` answers for both.
	if (node.kind != AstKind::Literal && value_fallback)
	{
		const SemaConstant value = analyzer_.evaluate(node, ctx);
		if (holds_address(value))
		{
			return pointed_object(value);
		}
	}
	const std::uint32_t array = designated(node, ctx, value_fallback);
	if (analyzer_.types_.kind(analyzer_.types_.strip_cv(address_type(array))) !=
	    TypeKind::Array)
	{
		// 13.5.5p1: what stands there is no array of this reading's, which is
		// what leaves the subscript to the operator function an operand of class
		// type names.
		return 0;
	}
	// 4.2p1: the array is the first of its own elements here, and the subscript
	// walks on from there.
	return subobject_address(array, 0);
}

// 5.7p5: the address `n` elements on from the one given, refused past 5.7p5's
// own bound - one before the first element and one past the last.
std::uint32_t ConstexprReading::advanced(std::uint32_t address, long long step)
{
	if (address == 0)
	{
		if (step == 0)
		{
			return 0;
		}
		throw NotConstant("a constant expression does arithmetic on a null "
		                  "pointer");
	}
	ConstantAddress held = analyzer_.model_.addresses().at(address);
	const unsigned long long bound = address_bound(held);
	const unsigned long long at = held.path.empty()
		? (held.past_end ? 1u : 0u)
		: held.path.back() + (held.past_end ? 1u : 0u);
	const long long moved = static_cast<long long>(at) + step;
	if (moved < 0 || static_cast<unsigned long long>(moved) > bound)
	{
		// 5.7p5: an operand pointing past one past the last element of the array
		// has undefined behaviour, which no constant expression holds.
		throw NotConstant("a constant expression walks a pointer outside the "
		                  "array it points into");
	}
	const unsigned long long moved_to = static_cast<unsigned long long>(moved);
	if (held.path.empty())
	{
		held.past_end = moved_to != 0;
		return analyzer_.model_.addresses().intern(held);
	}
	held.past_end = false;
	held.path.back() = moved_to;
	return analyzer_.model_.addresses().intern(held);
}

// 5.7p6: how many elements apart two addresses into one array are.
long long ConstexprReading::address_distance(std::uint32_t left,
                                             std::uint32_t right)
{
	const ConstantAddress& from = analyzer_.model_.addresses().at(left);
	const ConstantAddress& to = analyzer_.model_.addresses().at(right);
	if (from.object != to.object || from.literal != to.literal ||
	    from.temporary != to.temporary ||
	    from.path.size() != to.path.size() ||
	    (from.path.empty() ? false
	                       : !std::equal(from.path.begin(), from.path.end() - 1,
	                                     to.path.begin())))
	{
		// 5.7p6: both operands shall point into elements of the same array
		// object, or one past its last element.
		throw NotConstant("a constant expression subtracts pointers into two "
		                  "different objects");
	}
	const long long here = static_cast<long long>(
		from.path.empty() ? 0 : from.path.back()) + (from.past_end ? 1 : 0);
	const long long there = static_cast<long long>(
		to.path.empty() ? 0 : to.path.back()) + (to.past_end ? 1 : 0);
	return here - there;
}

// 5.9 and 5.10 over two addresses: 5.10p1 makes two pointers equal when they
// designate the same object, which is one comparison of the identifiers they
// interned as, and 5.9p2's relational operators order two into one array by the
// index each stands at.
bool ConstexprReading::address_operation(unsigned token,
                                         const SemaConstant& written_left,
                                         const SemaConstant& written_right,
                                         SemaConstant& out)
{
	// 4.2p1: an array operand of one of these operators is the address of its
	// first element, which is what `values + 3` and `values != last` are
	// written on - so the decay is asked here, before either operand is looked
	// at as a pointer, and an operand that is neither leaves the arithmetic
	// operators to the numbers they are written on.
	const SemaConstant given_left = decayed_operand(written_left);
	const SemaConstant given_right = decayed_operand(written_right);
	const bool left_address = holds_address(given_left);
	const bool right_address = holds_address(given_right);
	if (!left_address && !right_address)
	{
		return false;
	}
	if (!given_left.valued || !given_right.valued)
	{
		const SemaConstant& without =
			given_left.valued ? given_right : given_left;
		throw NotConstant("a constant expression writes an operator on an object "
		                  "whose value it does not hold",
		                  covered_object(without.object));
	}
	switch (token)
	{
	case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
		break;

	case OP_PLUS:
	case OP_MINUS:
	{
		if (left_address && right_address)
		{
			if (token != OP_MINUS)
			{
				throw NotConstant("a constant expression adds two pointers");
			}
			out.type = analyzer_.types_.fundamental(FT_LONG_INT);
			out.bits = static_cast<unsigned long long>(address_distance(
				static_cast<std::uint32_t>(given_left.bits),
				static_cast<std::uint32_t>(given_right.bits)));
			return true;
		}
		const SemaConstant& pointer = left_address ? given_left : given_right;
		const SemaConstant& count = left_address ? given_right : given_left;
		if (!left_address && token == OP_MINUS)
		{
			throw NotConstant("a constant expression subtracts a pointer from an "
			                  "integer");
		}
		const long long step = static_cast<long long>(counted(count));
		out = pointer_constant(
			advanced(static_cast<std::uint32_t>(pointer.bits),
			         token == OP_MINUS ? -step : step),
			pointer.type);
		return true;
	}

	default:
		throw NotConstant("a constant expression writes an operator on a pointer "
		                  "that 5.19 gives no value", false);
	}
	// 4.10p1: a null pointer constant written against a pointer is the null
	// pointer value of that pointer's type, which designates no object.
	const std::uint32_t left = left_address
		? static_cast<std::uint32_t>(given_left.bits) : 0;
	const std::uint32_t right = right_address
		? static_cast<std::uint32_t>(given_right.bits) : 0;
	if ((!left_address && !null_pointer_operand(given_left)) ||
	    (!right_address && !null_pointer_operand(given_right)))
	{
		throw NotConstant("a constant expression compares a pointer with a value "
		                  "that is no pointer");
	}
	bool answer = false;
	if (token == OP_EQ || token == OP_NE)
	{
		answer = (left == right) == (token == OP_EQ);
	}
	else
	{
		// 5.9p2: two pointers into one array compare by their indices, which is
		// what the distance between them says.
		const long long apart = address_distance(left, right);
		switch (token)
		{
		case OP_LT: answer = apart < 0; break;
		case OP_GT: answer = apart > 0; break;
		case OP_LE: answer = apart <= 0; break;
		default: answer = apart >= 0; break;
		}
	}
	out.type = analyzer_.types_.fundamental(FT_BOOL);
	out.bits = answer ? 1 : 0;
	return true;
}

// 4.2p1 and 4.3p1: an operand of array or function type read where 5.7's and
// 5.9's operators expect a pointer, which is the one conversion those operators
// apply to an operand before looking at it, and every other operand as it
// stands.
SemaConstant ConstexprReading::decayed_operand(const SemaConstant& value)
{
	const TypeId bare = analyzer_.types_.strip_cv(value.type);
	if (value.object == 0 ||
	    (analyzer_.types_.kind(bare) != TypeKind::Array &&
	     analyzer_.types_.kind(bare) != TypeKind::Function))
	{
		return value;
	}
	return pointer_constant(analyzer_.types_.kind(bare) == TypeKind::Array
		                        ? subobject_address(value.object, 0)
		                        : value.object,
	                        kNoType);
}

// 4.2p1, 4.10p1 and 4.4p1: `value` read where the place is of pointer type.
//
// Three conversions reach one: an array is the address of its first element, a
// null pointer constant is the null pointer value, and a pointer is itself with
// the place's own cv-qualification.  Everything else is no pointer at all,
// which is 5.19's answer about the program rather than this reading's edge.
bool ConstexprReading::at_pointer_place(const SemaConstant& value, TypeId place,
                                        SemaConstant& out, bool contextual)
{
	if (analyzer_.types_.kind(analyzer_.types_.strip_cv(place)) !=
	    TypeKind::Pointer)
	{
		return false;
	}
	if (holds_address(value) && value.valued)
	{
		out = pointer_constant(static_cast<std::uint32_t>(value.bits), place);
		return true;
	}
	if (null_pointer_operand(value))
	{
		out = pointer_constant(0, place);
		return true;
	}
	const TypeId bare = analyzer_.types_.strip_cv(value.type);
	if (value.object != 0 &&
	    (analyzer_.types_.kind(bare) == TypeKind::Array ||
	     analyzer_.types_.kind(bare) == TypeKind::Function))
	{
		// 4.2p1 and 4.3p1: an array becomes a pointer to its first element and
		// a function a pointer to itself, which is the object each designates.
		out = pointer_constant(
			analyzer_.types_.kind(bare) == TypeKind::Array
				? subobject_address(value.object, 0)
				: value.object,
			place);
		return true;
	}
	if (is_object(value))
	{
		// 5.19p3: a converted constant expression may reach its place through a
		// user-defined conversion, and a place of pointer type is one such place
		// like any other - so the same door `at_arithmetic_place` opens stands
		// here, and 13.3 chooses which conversion function it is.
		SemaConstant reached;
		if (at_pointer_place(converted(value, place, contextual), place, reached,
		                     contextual))
		{
			out = reached;
			return true;
		}
	}
	throw NotConstant("a constant expression initializes " +
	                  analyzer_.types_.description(place) + " from " +
	                  analyzer_.types_.description(value.type) +
	                  ", which is no address it holds",
	                  value.valued || covered_object(value.object));
}

// 8.5.3p4 and 5.2.9p11: which object a reference to `bound` binds to, where the
// object the operand designates is of another class in the same derivation.
//
// A reference to a base binds to the base class subobject, which is the entry
// that step of the object's list holds; one to a derived class - which
// `static_cast` is what writes - names the object that subobject is part of,
// which is the address one step up the path.  Neither is a copy: 8.5.3p4 binds
// the reference to storage that is already there, so both directions are the
// one address walked and no object built.
std::uint32_t ConstexprReading::bound_object(const SemaConstant& value,
                                             TypeId bound)
{
	const TypeId wanted = analyzer_.types_.strip_cv(bound);
	const TypeId held = analyzer_.types_.strip_cv(value.type);
	if (!analyzer_.types_.is_class(wanted) || !analyzer_.types_.is_class(held) ||
	    wanted == held)
	{
		return value.object;
	}
	Derivation derivation(analyzer_);
	SemaEntity* const base = derivation.base_in(held, wanted);
	if (base != nullptr)
	{
		const SemaConstant inside = base_subobject(value, *base);
		return inside.object != 0 ? inside.object : value.object;
	}
	if (derivation.base_in(wanted, held) == nullptr)
	{
		return value.object;
	}
	const std::uint32_t around = containing_object(value.object, wanted);
	if (around == 0)
	{
		// 5.2.9p11: the object the operand designates is no base class
		// subobject of an object of the class named, which is undefined
		// behaviour and no constant expression.
		throw NotConstant("a constant expression binds " +
		                  analyzer_.types_.description(bound) +
		                  " to something that is no base class subobject of "
		                  "such an object");
	}
	return around;
}

// 8.3.2p1 and 8.5.3: `value` read where the place is a reference.
//
// A reference binds to an object, so what the place holds is that object and
// not a copy of what it is worth - which is the whole of why a fold has to know
// one from the other: `&value` inside a body reached through `const int &` is
// the address of the *argument*.  12.2p1 gives a prvalue argument a temporary
// to bind to, which is storage this evaluation made, so it takes an address of
// its own that 5.19p2 refuses where it becomes a value the program keeps.
SemaConstant ConstexprReading::at_reference_place(const SemaConstant& value,
                                                  TypeId place)
{
	const TypeId bound = analyzer_.types_.target(analyzer_.types_.strip_cv(place));
	SemaConstant out;
	out.type = place;
	if (value.object != 0)
	{
		out.bits = bound_object(value, bound);
		out.object = static_cast<std::uint32_t>(out.bits);
		return out;
	}
	if (!value.valued)
	{
		throw NotConstant("a constant expression binds a reference to an object "
		                  "it designates no storage of", false);
	}
	// 12.2p1: the temporary, interned by what it holds - two calls that wrote
	// one value bind one temporary, which is what keeps a fold keyed on its
	// arguments finding the answer it already has.
	//
	// 8.5.3p5 says what object that is: one of the *reference's* type where a
	// conversion stands between the two, and one of the value's own where the
	// reference names 4.10p3's base class subobject, because the storage 12.2p1
	// gives is the whole object and the reference binds inside it.
	SemaConstant made_of =
		Derivation(analyzer_).base_in(value.type, bound) != nullptr
			? value
			: at_class_place(value, bound);
	ConstantAddress made;
	made.temporary = entry_of(made_of);
	made.automatic = true;
	made_of.object = analyzer_.model_.addresses().intern(made);
	out.bits = bound_object(made_of, bound);
	out.object = static_cast<std::uint32_t>(out.bits);
	return out;
}

// 2.14.5p8: the object a string literal is, which is what a name of an array
// is - storage of static duration this expression designates and reads no
// value out of until a subscript names an element.
bool ConstexprReading::literal_object(const std::string& spelling,
                                      SemaConstant& out)
{
	const TypeId type = literal_type(spelling);
	if (type == kNoType)
	{
		return false;
	}
	ConstantAddress held;
	held.literal = spelling;
	out.type = type;
	out.valued = false;
	out.object = analyzer_.model_.addresses().intern(held);
	return true;
}

// 5.2.1p1 over a pointer operand: `E1[E2]` is `*(E1 + E2)`.
SemaConstant ConstexprReading::subscripted(const SemaConstant& pointer,
                                           const SemaConstant& index)
{
	return loaded(advanced(pointed_object(pointer),
	                       static_cast<long long>(counted(index))));
}

// 5.19p2 asked of an operand a call is about to rank: what the expression is
// worth where it has a value, and which object it designates where it has not.
//
// 8.3.2p1's reference place is why the second reading exists at all: `static
// int n;` is an object whose address is a constant expression and whose value
// is not one, and a call written `address_of(n)` reaches its place through the
// first and its body through neither.  Which of the two the place wanted is
// 13.3's answer and is not known here, so an operand that has no value is read
// for the object it designates and refused again by every place that asks for
// one.
SemaConstant ConstexprReading::operand_constant(const AstNode& node,
                                                const SemaContext& ctx)
{
	try
	{
		return analyzer_.evaluate(node, ctx);
	}
	catch (const NotConstant& refused)
	{
		std::uint32_t object = 0;
		try
		{
			// The value reading has already refused this node, so the lvalue walk
			// asks for no second one: `designated`'s own last resort is that same
			// reading, and running it again answers nothing and costs the whole
			// operand once more per level of nesting.
			object = designated(node, ctx, false);
		}
		catch (const NotConstant&)
		{
			throw refused;
		}
		const SemaConstant out = held_at(object);
		if (out.type == kNoType)
		{
			throw refused;
		}
		return out;
	}
}

// 5.2.2p1: the declaration a call written on something that is no function name
// runs, where what stands there designates a function - 8.3.1p1's pointer to
// one, or 8.3.2p1's reference, which names it without an address at all.  Null
// where the operand designates no function, which leaves 13.5.4p1's object of
// class type to the reading that owns it.
SemaEntity* ConstexprReading::called_through(const SemaConstant& value)
{
	const TypeId bare = analyzer_.types_.strip_cv(value.type);
	std::uint32_t object = 0;
	if (analyzer_.types_.kind(bare) == TypeKind::Function)
	{
		object = value.object;
	}
	else if (holds_address(value) && value.valued &&
	         analyzer_.types_.kind(analyzer_.types_.strip_cv(
		         analyzer_.types_.target(bare))) == TypeKind::Function)
	{
		object = static_cast<std::uint32_t>(value.bits);
	}
	if (object == 0)
	{
		return nullptr;
	}
	SemaEntity* const named = analyzer_.model_.addresses().at(object).object;
	return named != nullptr && named->kind == SemaKind::Function ? named
	                                                             : nullptr;
}

// 5.19p2 asked of a pointer a declaration is about to keep: an address constant
// expression designates an object with static storage duration, and one this
// evaluation gave storage to is gone by the time the program reads it.
bool ConstexprReading::static_address(const SemaConstant& value) const
{
	if (!holds_address(value) || value.bits == 0)
	{
		return true;
	}
	return !analyzer_.model_.addresses()
		        .at(static_cast<std::uint32_t>(value.bits))
		        .automatic;
}
