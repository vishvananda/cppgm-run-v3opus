#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "token_model.h"

// A type, as the small integer that names it in a `TypeTable`.
typedef std::uint32_t TypeId;

// The identifier no type has.
const TypeId kNoType = 0;

// The cv-qualifiers of 7.1.6.1, as a bit set.
enum CvQualifiers
{
	kCvNone = 0,
	kCvConst = 1u << 0,
	kCvVolatile = 1u << 1
};

// The type categories 8.3 can build out of another type, plus the fundamental
// types of 3.9.1 that every type bottoms out in and the categories a
// declaration introduces a name for: a class (9), an enumeration (7.2) and a
// template parameter (14.1).
enum class TypeKind
{
	Fundamental,
	Pointer,
	MemberPointer,
	LValueReference,
	RValueReference,
	Array,
	Function,
	Class,
	Enum,
	TemplateParameter
};

// The class-key of 9p1, which is also how the dump spells the type.  Two
// declarations of one class agree exactly when neither or both wrote `union`.
enum class ClassTag
{
	Struct,
	Class,
	Union
};

// The simple-type-specifiers of 7.1.6.2, counted rather than remembered in
// order, because Table 10 names a type by which of them appear and `long` is
// the only one that may appear twice.
enum SimpleTypeSpecifier
{
	kSpecChar,
	kSpecChar16,
	kSpecChar32,
	kSpecWchar,
	kSpecBool,
	kSpecShort,
	kSpecInt,
	kSpecLong,
	kSpecSigned,
	kSpecUnsigned,
	kSpecFloat,
	kSpecDouble,
	kSpecVoid,
	kSimpleTypeSpecifierCount
};

// Which counter a token increments, or -1 when it is not one of them.
int builtin_specifier(unsigned token);

// Table 10 pairs a set of simple-type-specifiers with a type, and a set the
// table does not list names nothing.
bool table_10_names_a_type(const unsigned* counted);
EFundamentalType table_10_type(const unsigned* counted);

// Every distinct type of a run, stored once.
//
// A type is a value: `const int` is the same type wherever it is written, and
// two declarations of one entity agree exactly when their types do.  Storing
// each distinct type once makes that an integer compare rather than a
// structural walk, and it makes the type a declarator builds cost nothing when
// the same type has been built before - which it usually has, because a
// translation unit names few distinct types however many declarations it has.
//
// The type builders are not plain constructors: 8.3.2p6 collapses a reference
// to a reference, 8.3.4p1 pushes cv-qualification through an array, and
// 8.3.5p5 adjusts a parameter type.  Doing that here rather than in the parser
// keeps one answer to "what type is this" for every caller.
class TypeTable
{
public:
	TypeTable();

	TypeId fundamental(EFundamentalType type);

	// `type` with `cv` added, per 7.1.6.1 and 8.3.4p1: an array is qualified
	// by qualifying its elements, and a reference or function type ignores it.
	TypeId qualified(TypeId type, unsigned cv);

	TypeId pointer_to(TypeId type);

	// 8.3.3p1: a pointer to a member of `object_class`, which is not a pointer
	// and holds no address: it says which member of a class it names, and needs
	// an object of that class to name one.
	TypeId member_pointer_to(TypeId object_class, TypeId member);
	// The class a pointer to member names a member of.
	TypeId member_class(TypeId type) const { return nodes_[type].user; }

	// 8.3.5p7: the cv-qualifier-seq written after a parameter-clause, which
	// qualifies the function type itself.  It is not `qualified`, which 8.3.5p7
	// makes ignore a qualifier a typedef brought to a function type.
	TypeId qualified_function(TypeId function, unsigned cv);

	// 8.3.2p6: a reference to a reference collapses, and only two rvalue
	// references make an rvalue reference.
	TypeId reference_to(TypeId type, bool rvalue);

	TypeId array_of(TypeId element, bool bounded, unsigned long long bound);
	TypeId function_of(TypeId result, const std::vector<TypeId>& parameters,
	                   bool variadic);

	// The three categories a declaration introduces a name for.  Each is the
	// type of exactly one entity, so the entity identifies it: two classes of
	// the same name in two scopes are two types, and a class named after its
	// definition is closed is still the same type it was inside it.
	TypeId class_type(std::uint32_t entity, ClassTag tag, const std::string& name,
	                  const std::string& qualified);
	TypeId enum_type(std::uint32_t entity, bool scoped, const std::string& name,
	                 const std::string& qualified, TypeId underlying);
	TypeId template_parameter_type(std::uint32_t entity, bool is_template,
	                               const std::string& name);

	// The name the dump spells a user-defined type with.  7.1.3p2 lets a
	// declaration name an unnamed class after it has been read, so the name can
	// arrive after the type.
	void rename(TypeId type, const std::string& name);
	// That name, which 9.1p2 makes what a member of the type is written after.
	const std::string& user_name(TypeId type) const { return user_at(type).name; }
	// 3.4.3: the same type named from outside every region that encloses its
	// declaration, which is what an object-file name for it is built from.  The
	// dump spells a type as the declaration wrote it, so the two differ.
	const std::string& user_qualified_name(TypeId type) const
	{
		return user_at(type).qualified;
	}

	// 9.2p2: the class becomes complete at the end of its member specification,
	// which is where its size and alignment are first known.
	void complete_class(TypeId type, unsigned long long size,
	                    unsigned long long align, bool empty,
	                    bool trivially_copied);

	// 9p6: whether an object of the class holds nothing, which is what says a
	// copy of one moves no bytes.  False for every type that is not a class.
	bool is_empty_class(TypeId type) const;

	// 12.8p25: whether a copy of an object of the class is the copy of its
	// bytes, which it is until the program writes a copy constructor of its own
	// or holds a subobject whose copy is not.  True for every type that is not
	// a class.
	bool is_trivially_copied(TypeId type) const;

	bool is_class(TypeId type) const { return kind(type) == TypeKind::Class; }
	bool is_enum(TypeId type) const { return kind(type) == TypeKind::Enum; }
	// 7.2p2: an enumeration written `enum class` or `enum struct`, whose
	// enumerators are reached only through its own scope.
	bool is_scoped_enum(TypeId type) const;

	// 3.9.1: what a type is made of, which is what every layer above asks about
	// an operand before it says what an operator does with one.  Each is a fact
	// about the type alone, so the table that holds types answers it.
	bool is_arithmetic(TypeId type) const;
	// An enumeration counts, because 3.9.1p7 makes its values integral.
	bool is_integral(TypeId type) const;
	bool is_floating(TypeId type) const;
	// 3.9.2: a pointer to an object type rather than to a function.
	bool is_object_pointer(TypeId type) const;
	// 4.12: whether a prvalue of `type` converts to bool, which every condition
	// and logical operand needs.
	bool contextually_bool(TypeId type) const;
	ClassTag class_tag(TypeId type) const;

	// 8.3.5p5: the type a parameter declared with `type` contributes to the
	// function type.  An array becomes a pointer to its element, a function
	// becomes a pointer to itself, and top level cv-qualification is dropped.
	TypeId adjust_parameter(TypeId type);

	TypeKind kind(TypeId type) const { return nodes_[type].kind; }
	unsigned cv(TypeId type) const { return nodes_[type].cv; }
	EFundamentalType fundamental_type(TypeId type) const
	{
		return nodes_[type].fundamental;
	}
	// The pointee, referent, element or return type.
	TypeId target(TypeId type) const { return nodes_[type].target; }
	bool bounded(TypeId type) const { return nodes_[type].bounded; }
	unsigned long long bound(TypeId type) const { return nodes_[type].bound; }
	const std::vector<TypeId>& parameters(TypeId type) const
	{
		return *parameter_lists_[nodes_[type].parameters];
	}
	bool variadic(TypeId type) const { return nodes_[type].variadic; }

	// True for `void` itself, which 8.3.5p4 gives its own meaning as the only
	// parameter of a function.  A cv-qualified `void` is not it.
	bool is_plain_void(TypeId type) const;

	// True for `void` however it is qualified, which 3.9p5 makes the one
	// fundamental type an object can never have.
	bool is_void(TypeId type) const;

	bool is_reference(TypeId type) const
	{
		return kind(type) == TypeKind::LValueReference ||
			kind(type) == TypeKind::RValueReference;
	}

	// 3.9p6: a type an object cannot be made of, because its size is not
	// known: `void` and an array whose bound is missing at any dimension.
	bool is_incomplete(TypeId type) const;

	// `type` with its top level cv-qualifiers removed, which is the type 4p1
	// gives a prvalue of it.
	TypeId strip_cv(TypeId type);

	// 3.9.3p5: the cv-qualifiers an object of `type` has, which for an array
	// are its elements', because an array is as cv-qualified as they are.
	unsigned object_cv(TypeId type) const;

	// The course ABI size and alignment of an object of `type`, in bytes.  A
	// reference is an 8 byte pointer and a function is the 4 byte mock stub of
	// the assignment.  Zero for an incomplete type, which has neither.
	unsigned long long object_size(TypeId type) const;
	unsigned long long object_align(TypeId type) const;

	// What makes two function declarations declare the same function under
	// 3.5 and 13.1: their parameter type lists, already adjusted by 8.3.5p5.
	std::uint32_t signature(TypeId type) const
	{
		return (nodes_[type].parameters << 1) | (nodes_[type].variadic ? 1u : 0u);
	}

	// The identifier of a sequence of types, which is what a fact about one is
	// keyed by: the parameter list 13.1 tells two function declarations apart
	// by, and the template-argument list 14.7.1 tells two specializations of
	// one template apart by.
	std::uint32_t type_list(const std::vector<TypeId>& types)
	{
		return intern_parameters(types);
	}

	// 14.3 and 14.8.2: `type` with every template parameter `bindings` names
	// replaced by the type bound to it, keeping the qualifiers written around
	// each.  `memo` holds the answers of one substitution, so a type reached
	// twice is rebuilt once; a type that holds no template parameter is itself
	// and interns nothing.
	TypeId substitute(TypeId type,
	                  const std::unordered_map<TypeId, TypeId>& bindings,
	                  std::unordered_map<TypeId, TypeId>& memo);

	// The type in the form PA2 and PA7 print it in.
	std::string description(TypeId type) const;

private:
	// One type.  Every category fits the same record, so a type is one index
	// and the table is one flat array.
	struct Node
	{
		TypeKind kind;
		unsigned char cv;
		bool bounded;
		bool variadic;
		EFundamentalType fundamental;
		TypeId target;
		unsigned long long bound;
		std::uint32_t parameters;
		// Class, Enum and TemplateParameter: the record in `user_types_` that
		// holds what the declaration said, shared by every cv-qualified form
		// of the type so that completing or naming one names them all.
		std::uint32_t user;
	};

	// What a user-defined type carries beyond its category: how it is spelled,
	// and, for a class, whether it is complete and what an object of it costs.
	struct UserType
	{
		std::string name;
		std::string qualified;
		ClassTag tag;
		bool scoped;
		bool complete;
		unsigned long long size;
		unsigned long long align;
		// 9p6 and 12.8p15: whether an object of the class holds nothing - no
		// base subobject with storage and no non-static data member.  1.8p5
		// still gives it a byte, so its size does not say so, and a memberwise
		// copy of it moves nothing at all.
		bool empty;
		// 12.8p25: whether 12.8p15's memberwise copy of an object of the class
		// is the copy of its bytes, which the program writing a copy
		// constructor of its own - here or in a subobject - makes it not.
		bool trivially_copied;
	};

	// What makes two types the same type.
	struct Key
	{
		std::uint32_t shape;  // kind, cv and the two flags
		std::uint32_t operand;
		std::uint32_t extra;
		unsigned long long bound;

		bool operator==(const Key& other) const
		{
			return shape == other.shape && operand == other.operand &&
				extra == other.extra && bound == other.bound;
		}
	};

	struct KeyHash
	{
		std::size_t operator()(const Key& key) const;
	};

	struct ListHash
	{
		std::size_t operator()(const std::vector<TypeId>& list) const;
	};

	TypeId intern(const Key& key, const Node& node);
	std::uint32_t intern_parameters(const std::vector<TypeId>& parameters);
	// `type` with its top level cv-qualifiers removed.
	TypeId unqualified(TypeId type);
	// What tells two types of one category and one cv-qualification apart.
	static std::uint32_t operand_of(const Node& node);
	TypeId user_type(TypeKind kind, std::uint32_t entity, const UserType& record);
	// What tells two types apart, as the three words of a key: the category
	// with its flags, the type it is built over, and the second operand a
	// function and a pointer to member have.  Every builder and every rebuild
	// of a type with its qualifiers changed goes through these, so a type is
	// interned under one key however it is reached.
	static std::uint32_t shape_of(const Node& node);
	static std::uint32_t extra_of(const Node& node);
	static Key key_of(const Node& node);
	const UserType& user_at(TypeId type) const
	{
		return user_types_[nodes_[type].user];
	}
	// The bytes one object of `type` occupies, which for an array is one
	// element and for a class is what its definition laid out.
	unsigned long long element_size(TypeId type) const;
	void append_description(TypeId type, std::string& out) const;
	void append_parameters(TypeId type, std::string& out) const;

	std::vector<Node> nodes_;
	std::vector<UserType> user_types_;
	// The array types `qualified` is between, innermost last.
	std::vector<TypeId> dimensions_;
	std::unordered_map<Key, TypeId, KeyHash> ids_;
	// The unqualified type each declaration of a class, enumeration or template
	// parameter introduced, keyed by the entity that declared it.
	std::unordered_map<std::uint64_t, TypeId> user_ids_;
	std::unordered_map<std::vector<TypeId>, std::uint32_t, ListHash> parameter_ids_;
	std::vector<const std::vector<TypeId>*> parameter_lists_;
};
