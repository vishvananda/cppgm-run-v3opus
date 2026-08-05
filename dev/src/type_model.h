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
// types of 3.9.1 that every type bottoms out in.
enum class TypeKind
{
	Fundamental,
	Pointer,
	LValueReference,
	RValueReference,
	Array,
	Function
};

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

	// 8.3.2p6: a reference to a reference collapses, and only two rvalue
	// references make an rvalue reference.
	TypeId reference_to(TypeId type, bool rvalue);

	TypeId array_of(TypeId element, bool bounded, unsigned long long bound);
	TypeId function_of(TypeId result, const std::vector<TypeId>& parameters,
	                   bool variadic);

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
	void append_description(TypeId type, std::string& out) const;
	void append_parameters(TypeId type, std::string& out) const;

	std::vector<Node> nodes_;
	// The array types `qualified` is between, innermost last.
	std::vector<TypeId> dimensions_;
	std::unordered_map<Key, TypeId, KeyHash> ids_;
	std::unordered_map<std::vector<TypeId>, std::uint32_t, ListHash> parameter_ids_;
	std::vector<const std::vector<TypeId>*> parameter_lists_;
};
