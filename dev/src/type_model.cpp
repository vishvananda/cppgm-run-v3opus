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

// A user-defined type is keyed by the entity that declared it, which is a
// small integer like the type identifier an 8.3 category is keyed by, so its
// keys are held apart from theirs by a field neither of them uses otherwise.
const std::uint32_t kUserTypeKeyExtra = 0xFFFFFFFFu;

bool is_user_kind(TypeKind kind)
{
	return kind == TypeKind::Class || kind == TypeKind::Enum ||
		kind == TypeKind::TemplateParameter;
}

}

int builtin_specifier(unsigned token)
{
	switch (token)
	{
	case KW_CHAR: return kSpecChar;
	case KW_CHAR16_T: return kSpecChar16;
	case KW_CHAR32_T: return kSpecChar32;
	case KW_WCHAR_T: return kSpecWchar;
	case KW_BOOL: return kSpecBool;
	case KW_SHORT: return kSpecShort;
	case KW_INT: return kSpecInt;
	case KW_LONG: return kSpecLong;
	case KW_SIGNED: return kSpecSigned;
	case KW_UNSIGNED: return kSpecUnsigned;
	case KW_FLOAT: return kSpecFloat;
	case KW_DOUBLE: return kSpecDouble;
	case KW_VOID: return kSpecVoid;
	default: return -1;
	}
}

// `long` is the one specifier that may appear twice, `signed` and `unsigned`
// exclude each other, and the specifiers that name a type on their own admit
// no company but a signedness.
bool table_10_names_a_type(const unsigned* counted)
{
	unsigned total = 0;
	for (std::size_t index = 0; index < kSimpleTypeSpecifierCount; ++index)
	{
		if (counted[index] > (index == kSpecLong ? 2u : 1u))
		{
			return false;
		}
		total += counted[index];
	}
	if (counted[kSpecSigned] != 0 && counted[kSpecUnsigned] != 0)
	{
		return false;
	}
	const unsigned sign = counted[kSpecSigned] + counted[kSpecUnsigned];

	if (counted[kSpecVoid] != 0 || counted[kSpecBool] != 0 || counted[kSpecChar16] != 0 ||
	    counted[kSpecChar32] != 0 || counted[kSpecWchar] != 0 || counted[kSpecFloat] != 0)
	{
		return total == 1;
	}
	if (counted[kSpecChar] != 0)
	{
		return total == 1 + sign;
	}
	if (counted[kSpecDouble] != 0)
	{
		return counted[kSpecLong] <= 1 && total == 1 + counted[kSpecLong];
	}
	if (counted[kSpecShort] != 0)
	{
		return counted[kSpecLong] == 0 && total == 1 + sign + counted[kSpecInt];
	}
	return total != 0 && total == sign + counted[kSpecInt] + counted[kSpecLong];
}

// A specifier that decides the type on its own is asked about first; what is
// left is the integer types, which `signed`, `unsigned`, `short` and `long`
// choose between.
EFundamentalType table_10_type(const unsigned* counted)
{
	const bool is_unsigned = counted[kSpecUnsigned] != 0;
	if (counted[kSpecVoid] != 0)
	{
		return FT_VOID;
	}
	if (counted[kSpecBool] != 0)
	{
		return FT_BOOL;
	}
	if (counted[kSpecChar] != 0)
	{
		if (is_unsigned)
		{
			return FT_UNSIGNED_CHAR;
		}
		return counted[kSpecSigned] != 0 ? FT_SIGNED_CHAR : FT_CHAR;
	}
	if (counted[kSpecChar16] != 0)
	{
		return FT_CHAR16_T;
	}
	if (counted[kSpecChar32] != 0)
	{
		return FT_CHAR32_T;
	}
	if (counted[kSpecWchar] != 0)
	{
		return FT_WCHAR_T;
	}
	if (counted[kSpecFloat] != 0)
	{
		return FT_FLOAT;
	}
	if (counted[kSpecDouble] != 0)
	{
		return counted[kSpecLong] != 0 ? FT_LONG_DOUBLE : FT_DOUBLE;
	}
	if (counted[kSpecShort] != 0)
	{
		return is_unsigned ? FT_UNSIGNED_SHORT_INT : FT_SHORT_INT;
	}
	if (counted[kSpecLong] >= 2)
	{
		return is_unsigned ? FT_UNSIGNED_LONG_LONG_INT : FT_LONG_LONG_INT;
	}
	if (counted[kSpecLong] == 1)
	{
		return is_unsigned ? FT_UNSIGNED_LONG_INT : FT_LONG_INT;
	}
	return is_unsigned ? FT_UNSIGNED_INT : FT_INT;
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
	none.user = 0;
	nodes_.push_back(none);
	intern_parameters(std::vector<TypeId>());
}

std::uint32_t TypeTable::operand_of(const Node& node)
{
	switch (node.kind)
	{
	case TypeKind::Fundamental:
		return static_cast<std::uint32_t>(node.fundamental);

	case TypeKind::Class:
	case TypeKind::Enum:
	case TypeKind::TemplateParameter:
		// The record identifies the entity that declared the type, which is
		// what tells two of them apart; the underlying type of an enumeration
		// is not, because 7.2p5 lets it be fixed after the name is known.
		return node.user;

	default:
		return node.target;
	}
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

TypeId TypeTable::user_type(TypeKind category, std::uint32_t entity,
                            const UserType& record)
{
	// The type is named by the entity that declared it rather than by what it
	// is made of, so the key holds the entity and the record it points at is
	// shared by every cv-qualified form of the type.
	Key key;
	key.shape = static_cast<std::uint32_t>(category) << 8;
	key.operand = entity;
	key.extra = kUserTypeKeyExtra;
	key.bound = 0;

	const std::unordered_map<Key, TypeId, KeyHash>::const_iterator found =
		ids_.find(key);
	if (found != ids_.end())
	{
		return found->second;
	}

	Node node = nodes_[0];
	node.kind = category;
	node.user = static_cast<std::uint32_t>(user_types_.size());
	user_types_.push_back(record);
	const TypeId id = static_cast<TypeId>(nodes_.size());
	ids_.insert(std::make_pair(key, id));
	nodes_.push_back(node);
	return id;
}

TypeId TypeTable::class_type(std::uint32_t entity, ClassTag tag,
                             const std::string& name)
{
	UserType record;
	record.name = name;
	record.tag = tag;
	record.scoped = false;
	record.complete = false;
	record.size = 0;
	record.align = 1;
	return user_type(TypeKind::Class, entity, record);
}

TypeId TypeTable::enum_type(std::uint32_t entity, bool scoped,
                            const std::string& name, TypeId underlying)
{
	UserType record;
	record.name = name;
	record.tag = ClassTag::Struct;
	record.scoped = scoped;
	record.complete = true;
	record.size = 0;
	record.align = 0;
	const TypeId id = user_type(TypeKind::Enum, entity, record);
	nodes_[id].target = underlying;
	return id;
}

TypeId TypeTable::template_parameter_type(std::uint32_t entity, bool is_template,
                                          const std::string& name)
{
	UserType record;
	record.name = name;
	record.tag = ClassTag::Struct;
	record.scoped = is_template;
	record.complete = false;
	record.size = 0;
	record.align = 1;
	return user_type(TypeKind::TemplateParameter, entity, record);
}

void TypeTable::rename(TypeId type, const std::string& name)
{
	user_types_[nodes_[type].user].name = name;
}

void TypeTable::complete_class(TypeId type, unsigned long long size,
                               unsigned long long align)
{
	UserType& record = user_types_[nodes_[type].user];
	record.complete = true;
	record.size = size;
	record.align = align;
}

bool TypeTable::is_scoped_enum(TypeId type) const
{
	return kind(type) == TypeKind::Enum && user_at(type).scoped;
}

ClassTag TypeTable::class_tag(TypeId type) const
{
	return user_at(type).tag;
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
	key.operand = operand_of(node);
	key.extra = is_user_kind(node.kind) ? kUserTypeKeyExtra : 0;
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
	key.operand = operand_of(node);
	key.extra = is_user_kind(node.kind) ? kUserTypeKeyExtra : 0;
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
	if (kind(type) == TypeKind::Class)
	{
		// 9.2p2: a class is incomplete until its member specification closes,
		// and a class only ever declared is incomplete for the whole unit.
		return !user_at(type).complete;
	}
	if (kind(type) == TypeKind::TemplateParameter)
	{
		return true;
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

	const unsigned long long unit = element_size(type);
	if (unit != 0 && count > (~0ULL) / unit)
	{
		throw std::overflow_error("an object of the program is too large");
	}
	return count * unit;
}

// Every type the assignment gives a size to has it equal to its alignment, the
// mock function stub's four bytes included, apart from the two a declaration
// lays out: a class takes what its definition made of it, and an enumeration
// takes its underlying type's.
unsigned long long TypeTable::element_size(TypeId type) const
{
	switch (kind(type))
	{
	case TypeKind::Fundamental:
		return fundamental_type_size(fundamental_type(type));

	case TypeKind::Class:
		return user_at(type).size;

	case TypeKind::Enum:
		return element_size(target(type));

	case TypeKind::TemplateParameter:
		return 0;

	case TypeKind::Function:
		return 4;

	default:
		return 8;
	}
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

	case TypeKind::Class:
		return user_at(type).align;

	case TypeKind::Enum:
		return object_align(target(type));

	case TypeKind::TemplateParameter:
		return 0;

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

		case TypeKind::Class:
			switch (user_at(type).tag)
			{
			case ClassTag::Struct: out += "struct "; break;
			case ClassTag::Class: out += "class "; break;
			case ClassTag::Union: out += "union "; break;
			}
			out += user_at(type).name;
			return;

		case TypeKind::Enum:
			out += user_at(type).scoped ? "enum class " : "enum ";
			out += user_at(type).name;
			return;

		case TypeKind::TemplateParameter:
			// 14.1p2: a parameter declared with `template` names a template
			// rather than a type, which the dump spells apart from one.
			out += user_at(type).scoped ? "template-parameter " : "typename ";
			out += user_at(type).name;
			return;
		}
		type = node.target;
	}
}
