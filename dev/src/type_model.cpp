#include "type_model.h"

#include <stdexcept>
#include <utility>

namespace
{

std::string decimal(unsigned long long value)
{
	if (value == 0)
	{
		return "0";
	}
	char digits[24];
	std::size_t length = 0;
	while (value != 0)
	{
		digits[length++] = static_cast<char>('0' + (value % 10));
		value /= 10;
	}
	std::string text;
	text.reserve(length);
	while (length-- > 0)
	{
		text.push_back(digits[length]);
	}
	return text;
}

}

std::size_t TypeTable::KeyHash::operator()(const Key& key) const
{
	std::uint64_t hash = 1469598103934665603ULL;
	const std::uint64_t words[4] = {key.shape, key.operand, key.extra, key.bound};
	for (std::size_t index = 0; index < 4; ++index)
	{
		hash = (hash ^ words[index]) * 1099511628211ULL;
	}
	return static_cast<std::size_t>(hash ^ (hash >> 32));
}

std::size_t TypeTable::ListHash::operator()(const std::vector<TypeId>& list) const
{
	std::uint64_t hash = 1469598103934665603ULL;
	for (std::size_t index = 0; index < list.size(); ++index)
	{
		hash = (hash ^ list[index]) * 1099511628211ULL;
	}
	return static_cast<std::size_t>(hash ^ (hash >> 32));
}

TypeTable::TypeTable()
{
	// Index zero is `kNoType`.  Its record is every field's neutral value, so
	// it doubles as the blank a builder starts from and fills in the two or
	// three fields its category uses.
	Node none;
	none.kind = TypeKind::Fundamental;
	none.cv = 0;
	none.bounded = false;
	none.variadic = false;
	none.fundamental = FT_VOID;
	none.target = kNoType;
	none.bound = 0;
	none.parameters = 0;
	nodes_.push_back(none);
	intern_parameters(std::vector<TypeId>());
}

std::uint32_t TypeTable::intern_parameters(const std::vector<TypeId>& parameters)
{
	const std::pair<std::unordered_map<std::vector<TypeId>, std::uint32_t,
	                                   ListHash>::iterator, bool> entry =
		parameter_ids_.insert(
			std::make_pair(parameters, static_cast<std::uint32_t>(parameter_lists_.size())));
	if (entry.second)
	{
		parameter_lists_.push_back(&entry.first->first);
	}
	return entry.first->second;
}

TypeId TypeTable::intern(const Key& key, const Node& node)
{
	const std::pair<std::unordered_map<Key, TypeId, KeyHash>::iterator, bool> entry =
		ids_.insert(std::make_pair(key, static_cast<TypeId>(nodes_.size())));
	if (entry.second)
	{
		nodes_.push_back(node);
	}
	return entry.first->second;
}

TypeId TypeTable::fundamental(EFundamentalType type)
{
	Node node;
	node.kind = TypeKind::Fundamental;
	node.cv = 0;
	node.bounded = false;
	node.variadic = false;
	node.fundamental = type;
	node.target = kNoType;
	node.bound = 0;
	node.parameters = 0;

	Key key;
	key.shape = static_cast<std::uint32_t>(TypeKind::Fundamental) << 8;
	key.operand = static_cast<std::uint32_t>(type);
	key.extra = 0;
	key.bound = 0;
	return intern(key, node);
}

TypeId TypeTable::pointer_to(TypeId type)
{
	Node node = nodes_[0];
	node.kind = TypeKind::Pointer;
	node.target = type;

	Key key;
	key.shape = static_cast<std::uint32_t>(TypeKind::Pointer) << 8;
	key.operand = type;
	key.extra = 0;
	key.bound = 0;
	return intern(key, node);
}

TypeId TypeTable::reference_to(TypeId type, bool rvalue)
{
	// 8.3.2p6.  An lvalue reference wins over an rvalue one, so only `&&`
	// applied to a type that is not already an lvalue reference stays an
	// rvalue reference.
	if (kind(type) == TypeKind::LValueReference)
	{
		return type;
	}
	if (kind(type) == TypeKind::RValueReference)
	{
		return rvalue ? type : reference_to(target(type), false);
	}

	const TypeKind category =
		rvalue ? TypeKind::RValueReference : TypeKind::LValueReference;
	Node node = nodes_[0];
	node.kind = category;
	node.target = type;

	Key key;
	key.shape = static_cast<std::uint32_t>(category) << 8;
	key.operand = type;
	key.extra = 0;
	key.bound = 0;
	return intern(key, node);
}

TypeId TypeTable::array_of(TypeId element, bool bounded, unsigned long long size)
{
	Node node = nodes_[0];
	node.kind = TypeKind::Array;
	node.bounded = bounded;
	node.target = element;
	node.bound = bounded ? size : 0;

	Key key;
	key.shape = (static_cast<std::uint32_t>(TypeKind::Array) << 8) | (bounded ? 1u : 0u);
	key.operand = element;
	key.extra = 0;
	key.bound = node.bound;
	return intern(key, node);
}

TypeId TypeTable::function_of(TypeId result, const std::vector<TypeId>& parameters,
                              bool is_variadic)
{
	const std::uint32_t list = intern_parameters(parameters);
	Node node = nodes_[0];
	node.kind = TypeKind::Function;
	node.variadic = is_variadic;
	node.target = result;
	node.parameters = list;

	Key key;
	key.shape =
		(static_cast<std::uint32_t>(TypeKind::Function) << 8) | (is_variadic ? 2u : 0u);
	key.operand = result;
	key.extra = list;
	key.bound = 0;
	return intern(key, node);
}

TypeId TypeTable::qualified(TypeId type, unsigned add)
{
	if (add == 0)
	{
		return type;
	}

	// 8.3.4p1: a cv-qualified array is an array of cv-qualified elements, so
	// the qualifiers travel to the element type however many dimensions are in
	// the way and the array is rebuilt around what comes back.  A declarator
	// may write more dimensions than a machine stack has frames, so the way
	// down is a loop over a scratch of the dimensions rather than a descent.
	const std::size_t opened = dimensions_.size();
	while (kind(type) == TypeKind::Array)
	{
		dimensions_.push_back(type);
		type = target(type);
	}
	if (dimensions_.size() != opened)
	{
		TypeId element = qualified(type, add);
		while (dimensions_.size() != opened)
		{
			const TypeId array = dimensions_.back();
			dimensions_.pop_back();
			element = array_of(element, bounded(array), bound(array));
		}
		return element;
	}

	switch (kind(type))
	{
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
	case TypeKind::Function:
		// 8.3.2p1 and 8.3.5p7: cv-qualification introduced through a typedef
		// is ignored rather than applied.
		return type;

	default:
		break;
	}

	const unsigned merged = cv(type) | add;
	if (merged == cv(type))
	{
		return type;
	}
	Node node = nodes_[type];
	node.cv = static_cast<unsigned char>(merged);

	Key key;
	key.shape = (static_cast<std::uint32_t>(node.kind) << 8) | merged;
	key.operand = node.kind == TypeKind::Fundamental
		? static_cast<std::uint32_t>(node.fundamental)
		: node.target;
	key.extra = 0;
	key.bound = 0;
	return intern(key, node);
}

TypeId TypeTable::unqualified(TypeId type)
{
	if (cv(type) == 0)
	{
		return type;
	}
	Node node = nodes_[type];
	node.cv = 0;

	Key key;
	key.shape = static_cast<std::uint32_t>(node.kind) << 8;
	key.operand = node.kind == TypeKind::Fundamental
		? static_cast<std::uint32_t>(node.fundamental)
		: node.target;
	key.extra = 0;
	key.bound = 0;
	return intern(key, node);
}

TypeId TypeTable::adjust_parameter(TypeId type)
{
	if (kind(type) == TypeKind::Array)
	{
		return pointer_to(target(type));
	}
	if (kind(type) == TypeKind::Function)
	{
		return pointer_to(type);
	}
	return unqualified(type);
}

bool TypeTable::is_plain_void(TypeId type) const
{
	return kind(type) == TypeKind::Fundamental && cv(type) == 0 &&
		fundamental_type(type) == FT_VOID;
}

bool TypeTable::is_void(TypeId type) const
{
	return kind(type) == TypeKind::Fundamental && fundamental_type(type) == FT_VOID;
}

bool TypeTable::is_incomplete(TypeId type) const
{
	// A declarator may write more dimensions than a machine stack has frames,
	// so the way to the element type is a loop.
	while (kind(type) == TypeKind::Array)
	{
		if (!bounded(type))
		{
			return true;
		}
		type = target(type);
	}
	return is_void(type);
}

TypeId TypeTable::strip_cv(TypeId type)
{
	return unqualified(type);
}

unsigned TypeTable::object_cv(TypeId type) const
{
	// A declarator may write more dimensions than a machine stack has frames,
	// so the way to the element type is a loop.
	while (kind(type) == TypeKind::Array)
	{
		type = target(type);
	}
	return cv(type);
}

unsigned long long TypeTable::object_size(TypeId type) const
{
	unsigned long long count = 1;
	while (kind(type) == TypeKind::Array)
	{
		if (!bounded(type))
		{
			return 0;
		}
		const unsigned long long dimension = bound(type);
		if (dimension != 0 && count > (~0ULL) / dimension)
		{
			throw std::overflow_error("an object of the program is too large");
		}
		count *= dimension;
		type = target(type);
	}

	// Every type the assignment gives a size to has it equal to its alignment,
	// the mock function stub's four bytes included; a fundamental type is the
	// one whose size the ABI states rather than derives.
	const unsigned long long unit = object_align(type);
	if (unit != 0 && count > (~0ULL) / unit)
	{
		throw std::overflow_error("an object of the program is too large");
	}
	return count * (kind(type) == TypeKind::Fundamental
		? fundamental_type_size(fundamental_type(type))
		: unit);
}

// The assignment fixes the alignment of every fundamental type at its size, of
// every pointer and reference at 8, and of the mock function stub at 4; an
// array takes its element's.
unsigned long long TypeTable::object_align(TypeId type) const
{
	while (kind(type) == TypeKind::Array)
	{
		type = target(type);
	}
	switch (kind(type))
	{
	case TypeKind::Fundamental:
		return fundamental_type_size(fundamental_type(type));

	case TypeKind::Function:
		return 4;

	default:
		return 8;
	}
}

std::string TypeTable::description(TypeId type) const
{
	std::string text;
	append_description(type, text);
	return text;
}

void TypeTable::append_parameters(TypeId type, std::string& out) const
{
	const std::vector<TypeId>& list = parameters(type);
	for (std::size_t index = 0; index < list.size(); ++index)
	{
		if (index != 0)
		{
			out += ", ";
		}
		append_description(list[index], out);
	}
	if (variadic(type))
	{
		if (!list.empty())
		{
			out += ", ";
		}
		out += "...";
	}
}

// A type is described from the outside in, and every category but a parameter
// list has exactly one type inside it, so the description walks that chain as a
// loop: a declarator may build a chain longer than a machine stack.  Only the
// parameters of a function branch, and a parameter list can only be as deep as
// the declarator that wrote it, which the parser bounds.
void TypeTable::append_description(TypeId type, std::string& out) const
{
	for (;;)
	{
		const Node& node = nodes_[type];
		if ((node.cv & kCvConst) != 0)
		{
			out += "const ";
		}
		if ((node.cv & kCvVolatile) != 0)
		{
			out += "volatile ";
		}

		switch (node.kind)
		{
		case TypeKind::Fundamental:
			out += fundamental_type_name(node.fundamental);
			return;

		case TypeKind::Pointer:
			out += "pointer to ";
			break;

		case TypeKind::LValueReference:
			out += "lvalue-reference to ";
			break;

		case TypeKind::RValueReference:
			out += "rvalue-reference to ";
			break;

		case TypeKind::Array:
			if (node.bounded)
			{
				out += "array of ";
				out += decimal(node.bound);
				out += " ";
			}
			else
			{
				out += "array of unknown bound of ";
			}
			break;

		case TypeKind::Function:
			out += "function of (";
			append_parameters(type, out);
			out += ") returning ";
			break;
		}
		type = node.target;
	}
}
