#include "type_model.h"

#include <cmath>
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
	nodes_.push_back(Node());
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
	// 14.3.3p1: an entry standing for a template is told from another by the
	// declaration it names, which is the record, exactly as a class is.
	case TypeKind::TemplateName:
		// The record identifies the entity that declared the type, which is
		// what tells two of them apart; the underlying type of an enumeration
		// is not, because 7.2p5 lets it be fixed after the name is known.
		return node.user;

	default:
		return node.target;
	}
}

std::uint32_t TypeTable::shape_of(const Node& node)
{
	// The category, then the flags that are part of what the type is: its
	// cv-qualifiers, whether an array wrote a bound, whether a function takes
	// further arguments, and 8.3.5p1's ref-qualifier.  Each has a bit of its
	// own, so no two of them can make one shape.
	return (static_cast<std::uint32_t>(node.kind) << 8) | node.cv |
		(node.bounded ? 1u << 4 : 0u) | (node.variadic ? 1u << 5 : 0u) |
		(static_cast<std::uint32_t>(node.ref_qualifier) << 6);
}

std::uint32_t TypeTable::extra_of(const Node& node)
{
	switch (node.kind)
	{
	case TypeKind::Function:
		// Two function types with one return type are told apart by their
		// parameters, which are interned as a list.
		return node.parameters;

	case TypeKind::MemberPointer:
		// 8.3.3p1: a pointer to member names a member of one class, which is
		// as much a part of the type as what it points to.
		return node.user;

	case TypeKind::Pack:
		// 14.5.3p1: a bound run is what its elements are, which is a list; an
		// expansion holds none and is told apart by the pattern `operand_of`
		// already reads.
		return node.parameters;

	default:
		return is_user_kind(node.kind) ? kUserTypeKeyExtra : 0;
	}
}

// The key a node is interned under, which every builder and every rebuild of a
// type with its qualifiers changed asks for in the same words.
TypeTable::Key TypeTable::key_of(const Node& node)
{
	Key key;
	key.shape = shape_of(node);
	key.operand = operand_of(node);
	key.extra = extra_of(node);
	key.bound = node.bound;
	return key;
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

// A node is empty until a builder says what it is, so every field a category
// does not use reads as the nothing it means.  Each builder below starts from
// `nodes_[0]` and writes only what its category adds, which is what keeps a
// field added later - 8.3.5p1's ref-qualifier is one - from arriving unset in
// the builders that have no opinion about it.
TypeTable::Node::Node()
	: kind(TypeKind::Fundamental)
	, cv(0)
	, ref_qualifier(static_cast<unsigned char>(RefQualifier::None))
	, bounded(false)
	, variadic(false)
	, fundamental(FT_VOID)
	, target(kNoType)
	, bound(0)
	, parameters(0)
	, user(0)
{}

TypeId TypeTable::fundamental(EFundamentalType type)
{
	Node node;
	node.fundamental = type;
	return intern(key_of(node), node);
}

TypeId TypeTable::pointer_to(TypeId type)
{
	Node node = nodes_[0];
	node.kind = TypeKind::Pointer;
	node.target = type;
	return intern(key_of(node), node);
}

// 8.3.3p3: a pointer to member names a member of one class, and 8.3.3p1 refuses
// one to a reference or to `void`, neither of which a class has a member of.
TypeId TypeTable::member_pointer_to(TypeId object_class, TypeId member)
{
	if (is_reference(member) || is_void(member))
	{
		throw std::runtime_error("a declarator declares a pointer to a member "
		                         "of reference or void type");
	}
	Node node = nodes_[0];
	node.kind = TypeKind::MemberPointer;
	node.target = member;
	node.user = object_class;
	return intern(key_of(node), node);
}

TypeId TypeTable::qualified_function(TypeId function, unsigned add)
{
	const unsigned merged = cv(function) | add;
	if (kind(function) != TypeKind::Function || merged == cv(function))
	{
		return function;
	}
	Node node = nodes_[function];
	node.cv = static_cast<unsigned char>(merged);
	return intern(key_of(node), node);
}

// 8.3.5p1.  The ref-qualifier replaces rather than accumulates: a declarator
// writes at most one, and the question 13.1 asks of an overload set is which of
// the three a declaration wrote - so asking for `None` is what takes one off a
// type in order to ask whether the set already holds the other spelling.
TypeId TypeTable::ref_qualified_function(TypeId function, RefQualifier ref)
{
	const unsigned char wanted = static_cast<unsigned char>(ref);
	if (kind(function) != TypeKind::Function ||
	    nodes_[function].ref_qualifier == wanted)
	{
		return function;
	}
	Node node = nodes_[function];
	node.ref_qualifier = wanted;
	return intern(key_of(node), node);
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
	return intern(key_of(node), node);
}

TypeId TypeTable::array_of(TypeId element, bool bounded, unsigned long long size)
{
	Node node = nodes_[0];
	node.kind = TypeKind::Array;
	node.bounded = bounded;
	node.target = element;
	node.bound = bounded ? size : 0;
	return intern(key_of(node), node);
}

// 8.3.4p1: the type one element of an array is, however many dimensions it has,
// which is the type whose construction the array's own asks for.
TypeId TypeTable::element_of(TypeId type)
{
	TypeId at = strip_cv(type);
	while (kind(at) == TypeKind::Array)
	{
		at = strip_cv(target(at));
	}
	return at;
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
	return intern(key_of(node), node);
}

TypeId TypeTable::user_type(TypeKind category, std::uint32_t entity,
                            const UserType& record)
{
	// The type is named by the entity that declared it rather than by what it
	// is made of, so the entity is what finds it again.  What it is interned
	// under is the record it points at, which every cv-qualified form of the
	// type shares, so that adding and removing a qualifier comes back to the
	// same type rather than to a second one that spells the same.
	const std::uint64_t named =
		(static_cast<std::uint64_t>(category) << 32) | entity;
	const std::unordered_map<std::uint64_t, TypeId>::const_iterator declared =
		user_ids_.find(named);
	if (declared != user_ids_.end())
	{
		return declared->second;
	}

	Node node = nodes_[0];
	node.kind = category;
	node.user = static_cast<std::uint32_t>(user_types_.size());
	user_types_.push_back(record);
	const TypeId id = intern(key_of(node), node);
	user_ids_.insert(std::make_pair(named, id));
	return id;
}

TypeId TypeTable::class_type(std::uint32_t entity, ClassTag tag,
                             const std::string& name,
                             const std::string& qualified)
{
	UserType record;
	record.name = name;
	record.qualified = qualified;
	record.tag = tag;
	record.scoped = false;
	record.complete = false;
	record.size = 0;
	record.align = 1;
	record.empty = false;
	record.trivially_copied = true;
	record.copy_deleted = false;
	return user_type(TypeKind::Class, entity, record);
}

TypeId TypeTable::enum_type(std::uint32_t entity, bool scoped,
                            const std::string& name,
                            const std::string& qualified, TypeId underlying)
{
	UserType record;
	record.name = name;
	record.qualified = qualified;
	record.tag = ClassTag::Struct;
	record.scoped = scoped;
	record.complete = true;
	record.size = 0;
	record.align = 0;
	record.empty = false;
	record.trivially_copied = true;
	record.copy_deleted = false;
	const TypeId id = user_type(TypeKind::Enum, entity, record);
	nodes_[id].target = underlying;
	return id;
}

TypeId TypeTable::template_parameter_type(std::uint32_t entity, bool is_template,
                                          const std::string& name)
{
	UserType record;
	record.name = name;
	record.qualified = name;
	record.tag = ClassTag::Struct;
	record.scoped = is_template;
	record.complete = false;
	record.size = 0;
	record.align = 1;
	record.empty = false;
	record.trivially_copied = true;
	record.copy_deleted = false;
	return user_type(TypeKind::TemplateParameter, entity, record);
}

TypeId TypeTable::template_name_type(std::uint32_t entity,
                                     const std::string& name,
                                     const std::string& qualified)
{
	UserType record;
	record.name = name;
	record.qualified = qualified;
	record.tag = ClassTag::Struct;
	record.scoped = false;
	record.complete = false;
	record.size = 0;
	record.align = 1;
	record.empty = false;
	record.trivially_copied = true;
	record.copy_deleted = false;
	return user_type(TypeKind::TemplateName, entity, record);
}

// 14.6.2p1: `F<A…>` where `F` is still the place its own head declared.  The
// naming is interned by `entity`, which the caller has already made one of per
// place-and-list, so a spelling written twice in one pattern is one type and the
// substitution below runs once for it.
TypeId TypeTable::dependent_template_id(std::uint32_t entity, TypeId parameter,
                                        const std::vector<TypeId>& arguments,
                                        const std::string& name)
{
	const TypeId made = template_parameter_type(entity, false, name);
	UserType& record = user_types_[nodes_[made].user];
	record.applied_template = parameter;
	record.template_arguments = arguments;
	return made;
}

// A type has two spellings - the one a dump writes and the one PA14's encoder
// reads - and a rename that moved only the first would leave the object file
// naming the type the declaration was called before.  Both are written here.
void TypeTable::rename(TypeId type, const std::string& name,
                       const std::string& qualified)
{
	UserType& record = user_types_[nodes_[type].user];
	record.name = name;
	record.qualified = qualified;
}

void TypeTable::set_template_arguments(TypeId type, const std::string& templated,
                                       const std::vector<TypeId>& arguments,
                                       unsigned pack_at)
{
	UserType& record = user_types_[nodes_[type].user];
	record.template_name = templated;
	record.template_arguments = arguments;
	record.template_pack_place = pack_at;
	// 14.6.2p1 is asked of the arguments, so the answer this type had before it
	// had any is not the answer it has now.  Nothing holds this type yet - it
	// was made a moment ago - so its own entry is the only stale one.
	dependent_.erase(type);
}

void TypeTable::set_template_index(TypeId type, unsigned index)
{
	user_types_[nodes_[type].user].template_index = index;
}

void TypeTable::set_parameter_value_type(TypeId type, TypeId value_type)
{
	user_types_[nodes_[type].user].parameter_value_type = value_type;
}

// 14.3.2p1: an argument at a non-type place, which is the value and the type it
// was converted to.  Nothing about it is a user-defined type, so it is interned
// by what it holds like a pointer or an array and not by a declaration.
TypeId TypeTable::value_type(TypeId type, unsigned long long bits)
{
	Node node = nodes_[0];
	node.kind = TypeKind::Value;
	node.bounded = true;
	node.target = type;
	node.bound = bits;
	return intern(key_of(node), node);
}

// 5.19 with 3.9.1p8: a value entry for a floating constant.  `frexp` is what
// takes the value apart exactly - a significand in [0.5, 1) and a power of two -
// so the key below is the whole of the value and not a rounding of it, and the
// sign is asked for on its own because the significand of a zero carries none.
//
// 3.9.1p8 leaves the values a floating type holds to the implementation, and
// this one holds two `frexp` takes no such pair out of: an infinity, which
// 5p4's overflow arrives at and 2.14.4p1's out-of-range literal is, and the NaN
// an arithmetic on one comes to.  Neither is a power of two times a fraction,
// so neither goes through the cast below - each takes a key of its own beside
// the zeros, whose significand is the one no finite value shares.
TypeId TypeTable::real_type(TypeId type, long double value)
{
	const bool negative = std::signbit(value);
	int exponent = 0;
	long long order = negative ? -1 : 0;
	unsigned long long magnitude = value == value ? 1 : 2;
	if (std::isfinite(value))
	{
		const long double significand = std::frexp(value, &exponent);
		order = negative ? -2LL * exponent - 1 : 2LL * exponent;
		magnitude = static_cast<unsigned long long>(
			std::ldexp(significand < 0 ? -significand : significand, 64));
	}
	const std::pair<long long, unsigned long long> key(order, magnitude);
	const std::map<std::pair<long long, unsigned long long>,
	               unsigned long long>::iterator held = real_ids_.find(key);
	if (held != real_ids_.end())
	{
		return value_type(type, held->second);
	}
	const unsigned long long index = reals_.size();
	reals_.push_back(value);
	real_ids_.insert(std::make_pair(key, index));
	return value_type(type, index);
}

void TypeTable::set_template_pack(TypeId type, bool pack)
{
	user_types_[nodes_[type].user].template_pack = pack;
}

// 14.5.3p1: the run of arguments bound to a pack place, interned by the
// elements it holds the way a function type is interned by its parameters - so
// two places bound to the same run read one entry and a fact keyed by an
// argument list tells `Ts = <int>` from `Ts = <int, long>` apart.
TypeId TypeTable::pack_type(const std::vector<TypeId>& elements)
{
	Node node = nodes_[0];
	node.kind = TypeKind::Pack;
	node.parameters = intern_parameters(elements);
	return intern(key_of(node), node);
}

// 14.5.3p4: `pattern...`, which stands in a list until the run its pattern
// names arrives.  The pattern is what tells two expansions apart, and an empty
// element list is what tells an expansion from a run of no elements.
TypeId TypeTable::pack_expansion(TypeId pattern)
{
	Node node = nodes_[0];
	node.kind = TypeKind::Pack;
	node.target = pattern;
	node.parameters = intern_parameters(std::vector<TypeId>());
	return intern(key_of(node), node);
}

void TypeTable::set_nested_in_dependent(TypeId type)
{
	user_types_[nodes_[type].user].nested_in_dependent = true;
	// The answer this type had before the flag is not the answer it has now.
	// Nothing holds it yet - it was made a moment ago - so its own entry is
	// the only stale one.
	dependent_.erase(type);
}

void TypeTable::set_dependent_member(TypeId type, TypeId owner,
                                     const std::string& member)
{
	UserType& record = user_types_[nodes_[type].user];
	record.dependent_owner = owner;
	record.dependent_member = member;
}

void TypeTable::set_declaration(TypeId type, const SemaEntity* declaration)
{
	user_types_[nodes_[type].user].declaration = declaration;
}

void TypeTable::set_local_name(TypeId type, const SemaEntity* function,
                               unsigned occurrence, bool unnamed)
{
	UserType& record = user_types_[nodes_[type].user];
	record.local_function = function;
	record.local_occurrence = occurrence;
	record.local_unnamed = unnamed;
}

void TypeTable::complete_class(TypeId type, unsigned long long size,
                               unsigned long long align, bool empty,
                               bool trivially_copied, bool zeroed_storage,
                               bool subobject_bytes)
{
	UserType& record = user_types_[nodes_[type].user];
	record.complete = true;
	record.size = size;
	record.align = align;
	record.empty = empty;
	record.trivially_copied = trivially_copied;
	record.zeroed_storage = zeroed_storage;
	record.subobject_bytes = subobject_bytes;
	record.carried_by_bytes = trivially_copied;
}

bool TypeTable::has_zeroed_storage(TypeId type)
{
	TypeId bare = strip_cv(type);
	while (kind(bare) == TypeKind::Array)
	{
		// 8.5p8 over an array is 8.5p8 over each of its elements, so an array
		// of a class that holds nothing holds nothing however long it is.
		bare = strip_cv(target(bare));
	}
	return kind(bare) != TypeKind::Class || user_at(bare).zeroed_storage;
}

void TypeTable::settle_copy_facts(TypeId type, bool trivially_copied,
                                  bool copy_deleted, bool vacuous_destruction,
                                  bool derived)
{
	UserType& record = user_types_[nodes_[type].user];
	record.trivially_copied = trivially_copied;
	record.copy_deleted = copy_deleted;
	record.vacuous_destruction = vacuous_destruction;
	// 10p1: what an object of a derived class is carried by at 5.2.2p4's
	// boundary is what the storage it is laid out over is carried by, so the
	// copy half of that boundary is the subobjects' answer and not this class's
	// own copy constructor.  A class that derives from nothing reads its own.
	record.carried_by_bytes =
		derived ? record.subobject_bytes : trivially_copied;
}

bool TypeTable::is_copy_deleted(TypeId type) const
{
	return kind(type) == TypeKind::Class && user_at(type).copy_deleted;
}

bool TypeTable::is_trivially_copied(TypeId type) const
{
	return kind(type) != TypeKind::Class || user_at(type).trivially_copied;
}

bool TypeTable::returns_indirectly(TypeId type)
{
	const TypeId bare = strip_cv(type);
	if (kind(bare) != TypeKind::Class || is_incomplete(bare))
	{
		return false;
	}
	// The course ABI hands back an object of two words or less as the bytes it
	// occupies, and everything wider through a destination the caller names.
	// A class whose bytes do not stand for the object is handed back through a
	// place and never in registers, because the caller has to be able to name
	// the object a copy or a destruction of it would run on.
	return !bytes_stand_for_object(bare) ||
		object_size(bare) > kDirectReturnBytes;
}

bool TypeTable::passes_indirectly(TypeId type)
{
	const TypeId bare = strip_cv(type);
	if (kind(bare) != TypeKind::Class || is_incomplete(bare))
	{
		return false;
	}
	// 12.8p12 and 12.4p8: the boundary carries an object of the class as its
	// bytes exactly where those bytes stand for the object - where they are the
	// whole of what a copy of it is and where the end of its lifetime comes to
	// nothing.  Either one failing makes the parameter and the argument one
	// object standing in the caller's storage, which the call names by its
	// address.  `carried_by_bytes` is the copy half as this boundary reads it,
	// which for a derived class is 10p1's storage it is laid out over.
	return !user_at(bare).carried_by_bytes ||
		!user_at(bare).vacuous_destruction;
}

bool TypeTable::has_vacuous_destruction(TypeId type)
{
	const TypeId bare = strip_cv(type);
	return kind(bare) != TypeKind::Class || user_at(bare).vacuous_destruction;
}

bool TypeTable::has_declared_destruction(TypeId type)
{
	const TypeId bare = strip_cv(type);
	return kind(bare) == TypeKind::Class && user_at(bare).declared_destruction;
}

void TypeTable::settle_declared_destruction(TypeId type, bool declared)
{
	user_types_[nodes_[type].user].declared_destruction = declared;
}

bool TypeTable::bytes_stand_for_object(TypeId type)
{
	const TypeId bare = strip_cv(type);
	return kind(bare) != TypeKind::Class ||
		(user_at(bare).trivially_copied && user_at(bare).vacuous_destruction);
}

bool TypeTable::is_empty_class(TypeId type) const
{
	return kind(type) == TypeKind::Class && user_at(type).empty;
}

bool TypeTable::is_scoped_enum(TypeId type) const
{
	return kind(type) == TypeKind::Enum && user_at(type).scoped;
}

bool TypeTable::is_arithmetic(TypeId type) const
{
	return kind(type) == TypeKind::Fundamental &&
		fundamental_type_class(fundamental_type(type)) !=
			FundamentalTypeClass::NonArithmetic;
}

bool TypeTable::is_scalar(TypeId type) const
{
	// A cv-qualifier is a fact beside the kind rather than a node around it, so
	// this reads through one without removing it.
	switch (kind(type))
	{
	case TypeKind::Pointer:
	case TypeKind::MemberPointer:
	case TypeKind::Enum:
		return true;

	case TypeKind::Fundamental:
		// 3.9p9: `void` is an incomplete type and holds no value, which leaves
		// `std::nullptr_t` the one non-arithmetic fundamental type that is a
		// scalar.
		return fundamental_type(type) != FT_VOID;

	default:
		break;
	}
	return false;
}

bool TypeTable::is_integral(TypeId type) const
{
	return kind(type) == TypeKind::Enum ||
		(kind(type) == TypeKind::Fundamental &&
		 fundamental_type_is_integral(fundamental_type(type)));
}

bool TypeTable::is_floating(TypeId type) const
{
	return kind(type) == TypeKind::Fundamental &&
		fundamental_type_class(fundamental_type(type)) ==
			FundamentalTypeClass::Floating;
}

bool TypeTable::is_object_pointer(TypeId type) const
{
	return kind(type) == TypeKind::Pointer &&
		kind(target(type)) != TypeKind::Function;
}

// 4.12: every arithmetic, unscoped enumeration and pointer type converts to
// bool; a scoped enumeration does not.  4p1 makes clause 4 a sequence, so 4.2
// and 4.3 stand first: an array and a function are the pointer they convert to
// before anything asks what that pointer is worth as a truth value.
bool TypeTable::contextually_bool(TypeId type) const
{
	if (is_scoped_enum(type))
	{
		return false;
	}
	return is_arithmetic(type) || kind(type) == TypeKind::Enum ||
		kind(type) == TypeKind::Pointer ||
		kind(type) == TypeKind::MemberPointer ||
		kind(type) == TypeKind::Array || kind(type) == TypeKind::Function ||
		(kind(type) == TypeKind::Fundamental &&
		 fundamental_type(type) == FT_NULLPTR_T);
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
	return intern(key_of(node), node);
}

TypeId TypeTable::unqualified(TypeId type)
{
	if (cv(type) == 0)
	{
		return type;
	}
	Node node = nodes_[type];
	node.cv = 0;
	return intern(key_of(node), node);
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

// 8.3.5p5 over the object rather than over the function type: the array and the
// function become pointers here too, because that is what the parameter *is* -
// but the top-level cv-qualifiers stay, because the clause drops them from the
// type of the function and not from the object the body names.  A by-value
// parameter written `const` is a const object, and 13.3 asks that object which
// of an overload set a call on it reaches.
TypeId TypeTable::parameter_object(TypeId type)
{
	const TypeId bare = strip_cv(type);
	if (kind(bare) == TypeKind::Array)
	{
		return pointer_to(target(bare));
	}
	if (kind(bare) == TypeKind::Function)
	{
		return pointer_to(bare);
	}
	return type;
}

TypeId TypeTable::substitute(TypeId type,
                             const std::unordered_map<TypeId, TypeId>& bindings,
                             std::unordered_map<TypeId, TypeId>& memo)
{
	const std::unordered_map<TypeId, TypeId>::const_iterator seen =
		memo.find(type);
	if (seen != memo.end())
	{
		return seen->second;
	}
	const unsigned qualifiers = cv(type);
	TypeId result = type;
	switch (kind(type))
	{
	case TypeKind::TemplateParameter:
	{
		// 14.3p1: an argument is bound to the parameter itself, and the
		// qualifiers written around the parameter stay around what it names.
		const std::unordered_map<TypeId, TypeId>::const_iterator bound =
			bindings.find(unqualified(type));
		if (bound != bindings.end())
		{
			result = qualified(bound->second, qualifiers);
		}
		break;
	}

	case TypeKind::Pointer:
		result = qualified(pointer_to(substitute(target(type), bindings, memo)),
		                   qualifiers);
		break;

	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
		// 8.3.2p6 collapses a reference to a reference, which is exactly the
		// case 14.3p4 leaves to the builder rather than to the substitution.
		result = reference_to(substitute(target(type), bindings, memo),
		                      kind(type) == TypeKind::RValueReference);
		break;

	case TypeKind::Array:
		result = array_of(substitute(target(type), bindings, memo),
		                  bounded(type), bound(type));
		break;

	case TypeKind::MemberPointer:
		result = qualified(
			member_pointer_to(substitute(member_class(type), bindings, memo),
			                  substitute(target(type), bindings, memo)),
			qualifiers);
		break;

	case TypeKind::Function:
	{
		// The list is a key of `parameter_ids_`, so it stays where it is while
		// the substitution of one of its elements interns another list.
		const std::vector<TypeId>& written = parameters(type);
		std::vector<TypeId> built(written.size());
		for (std::size_t index = 0; index < written.size(); ++index)
		{
			built[index] = substitute(written[index], bindings, memo);
		}
		// 8.3.5p1 and 8.3.5p7: a substitution replaces the types a function is
		// written over and none of the qualifiers written after its
		// parameter-clause, so both travel to the type it builds.
		result = ref_qualified_function(
			qualified_function(
				function_of(substitute(target(type), bindings, memo), built,
				            variadic(type)),
				qualifiers),
			function_ref_qualifier(type));
		break;
	}

	case TypeKind::Value:
		// 14.3.2p1: the bits are already settled; only the type they were
		// converted to can still name a parameter, which `template<class T, T v>`
		// leaves standing in an argument list read against another head.
		result = value_type(substitute(target(type), bindings, memo), bound(type));
		break;

	case TypeKind::Pack:
	{
		// 14.5.3p4: an expansion is replaced by the run it stands for, which is
		// a list rather than a type and so is the reading in `sema_pack.cpp`.
		// What this walk answers is the run itself, whose elements may still
		// name a parameter of an outer head.
		if (target(type) != kNoType)
		{
			break;
		}
		// 14.5.3p4: a run may itself be what a substitution names, which is how
		// one element of it is written into the type an expansion is read over.
		const std::unordered_map<TypeId, TypeId>::const_iterator bound =
			bindings.find(unqualified(type));
		if (bound != bindings.end())
		{
			result = qualified(bound->second, qualifiers);
			break;
		}
		const std::vector<TypeId>& run = pack_elements(type);
		std::vector<TypeId> built(run.size());
		for (std::size_t index = 0; index < run.size(); ++index)
		{
			built[index] = substitute(run[index], bindings, memo);
		}
		result = pack_type(built);
		break;
	}

	default:
		break;
	}
	memo.insert(std::make_pair(type, result));
	return result;
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

bool TypeTable::is_dependent(TypeId type) const
{
	const std::unordered_map<TypeId, bool>::const_iterator held =
		dependent_.find(type);
	if (held != dependent_.end())
	{
		return held->second;
	}
	// The answer is recorded before the walk, because a type reached twice
	// from one nest is otherwise walked twice - and a nest whose arguments
	// double at every level would be walked 2^n times.  A specialization holds
	// only types made before it, so nothing here reaches this type again.
	const bool answer = dependent_walk(type);
	dependent_[type] = answer;
	return answer;
}

bool TypeTable::dependent_walk(TypeId type) const
{
	switch (kind(type))
	{
	case TypeKind::TemplateParameter:
		return true;

	case TypeKind::Pointer:
	case TypeKind::LValueReference:
	case TypeKind::RValueReference:
	case TypeKind::Array:
		return is_dependent(target(type));

	case TypeKind::MemberPointer:
		return is_dependent(member_class(type)) || is_dependent(target(type));

	case TypeKind::Function:
	{
		if (is_dependent(target(type)))
		{
			return true;
		}
		const std::vector<TypeId>& given = parameters(type);
		for (std::size_t index = 0; index < given.size(); ++index)
		{
			if (is_dependent(given[index]))
			{
				return true;
			}
		}
		return false;
	}

	case TypeKind::Class:
	{
		// 14.6.2.1p9: a nested class of the current instantiation is dependent
		// whatever its own declaration wrote, because the class around it is.
		if (user_at(type).nested_in_dependent)
		{
			return true;
		}
		const std::vector<TypeId>& arguments = user_at(type).template_arguments;
		for (std::size_t index = 0; index < arguments.size(); ++index)
		{
			if (is_dependent(arguments[index]))
			{
				return true;
			}
		}
		return false;
	}

	case TypeKind::Enum:
		// 14.6.2.1p9 again: a nested enumeration of the current instantiation
		// is dependent for the same reason a nested class is.
		return user_at(type).nested_in_dependent;

	case TypeKind::Value:
		// 14.3.2p1: a value argument is the bits it holds, which no argument
		// list can change; only the type it was converted to can still depend
		// on one, which `template<class T, T v>` writes.
		return is_dependent(target(type));

	case TypeKind::Pack:
	{
		// 14.5.3p4: an expansion is what its pattern is and is never settled
		// where it stands - the run it stands for is what replaces it - so it
		// answers dependent for as long as it exists.  A bound run is dependent
		// exactly when one of the arguments in it is.
		if (target(type) != kNoType)
		{
			return true;
		}
		const std::vector<TypeId>& run = pack_elements(type);
		for (std::size_t index = 0; index < run.size(); ++index)
		{
			if (is_dependent(run[index]))
			{
				return true;
			}
		}
		return false;
	}

	default:
		return false;
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
		// 8.3.5p7 writes the cv-qualifier-seq of a function type after its
		// parameters rather than before it, which is where it was written.
		if (node.kind != TypeKind::Function)
		{
			if ((node.cv & kCvConst) != 0)
			{
				out += "const ";
			}
			if ((node.cv & kCvVolatile) != 0)
			{
				out += "volatile ";
			}
		}

		switch (node.kind)
		{
		case TypeKind::Fundamental:
			out += fundamental_type_name(node.fundamental);
			return;

		case TypeKind::Pointer:
			out += "pointer to ";
			break;

		case TypeKind::MemberPointer:
			out += "member-pointer of ";
			append_description(node.user, out);
			out += " to ";
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
			out += ")";
			if ((node.cv & kCvConst) != 0)
			{
				out += " const";
			}
			if ((node.cv & kCvVolatile) != 0)
			{
				out += " volatile";
			}
			// 8.3.5p1: the ref-qualifier is written after the cv-qualifier-seq,
			// which is where it is spelled back.
			if (node.ref_qualifier ==
			    static_cast<unsigned char>(RefQualifier::LValue))
			{
				out += " &";
			}
			else if (node.ref_qualifier ==
			         static_cast<unsigned char>(RefQualifier::RValue))
			{
				out += " &&";
			}
			out += " returning ";
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

		case TypeKind::Value:
			// 14.3.2p1: a value argument is described as what it is worth and
			// what it was converted to, which is what a diagnostic about a
			// specialization has to name it by.
			out += "value ";
			out += decimal(node.bound);
			out += " of ";
			break;

		case TypeKind::Pack:
			// 14.5.3: a run is described by what it holds and an expansion by
			// the pattern it stands for, which is what a diagnostic about a
			// pack that no argument list has settled has to name it by.
			if (node.target == kNoType)
			{
				out += "pack of (";
				append_parameters(type, out);
				out += ")";
				return;
			}
			out += "pack expansion of ";
			break;

		case TypeKind::TemplateName:
			// 14.3.3p1: the argument names a template, so what describes it is
			// the template's own name and not any type it can be applied to.
			out += "template ";
			out += user_at(type).qualified;
			return;
		}
		type = node.target;
	}
}
