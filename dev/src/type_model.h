#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "token_model.h"

// A type, as the small integer that names it in a `TypeTable`.
typedef std::uint32_t TypeId;

// 9.8p1: a type a function's body declares is named after that function in the
// object file, so a class or enumeration record carries the declaration of the
// function it is local to.  Nothing here reads the declaration; it is the key
// the name is encoded from where a name is asked for.
struct SemaEntity;

// The identifier no type has.
const TypeId kNoType = 0;

// 6.6.3p2: the widest object of class type the course ABI hands back as the
// bytes it occupies, which is the two machine words a return carries.
const unsigned long long kDirectReturnBytes = 16;

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
	TemplateParameter,
	// 14.3.2p1: a template argument written at a non-type place, which is a
	// converted constant value rather than a type.  It is a type-table entry
	// because 14.4p1 makes it part of what tells two specializations of one
	// template apart, and every fact keyed by an argument list - the
	// specialization, the substitution, the object-file name - reads that list
	// as types.  Nothing declares an object of one.
	Value,
	// 14.5.3p1: a template parameter pack, in the two forms one is ever in.
	//
	// A pack that an argument list has bound is the *run* of arguments it took,
	// which is a list and not a type; a pack an argument list has yet to settle
	// is written into a declaration as 14.5.3p4's pack-expansion `P...`, which
	// is the pattern `P` standing for however many arguments the run holds.
	// Both are type-table entries because every list a template tier is keyed
	// by - the argument list, a function's parameter list - reads as types, and
	// an expansion has to survive in one until the run arrives.  Nothing
	// declares an object of either.
	Pack,
	// 14.3.3p1: a template named at a template-template place, which is neither
	// a type nor a value - the argument *is* the template, and what tells two
	// argument lists apart is which template each of them named.  It is a
	// type-table entry for the reason `Value` is: every list a template tier is
	// keyed by reads as types.  Nothing declares an object of one, and nothing
	// depends on a parameter, because the entry names one declaration outright.
	TemplateName
};

// 8.3.5p1: the ref-qualifier written after a member function's
// parameter-clause.  Like 8.3.5p7's cv-qualifier-seq beside it, it qualifies
// the function type itself rather than any parameter a declarator wrote, and
// 13.3.1p4 reads it as the value category the implicit object parameter binds
// its argument by.
enum class RefQualifier
{
	None,
	LValue,
	RValue
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

	// 8.3.5p1: `function` with `ref` as its ref-qualifier, replacing whatever
	// one it had.  It stands beside 8.3.5p7's cv-qualifier-seq as the second
	// thing a member function's declarator writes after its parameter-clause,
	// so it is part of the function type for the same reason: a declaration and
	// the definition written outside its class agree on one type, and 13.1 has
	// `f() &` and `f() &&` to tell apart.
	TypeId ref_qualified_function(TypeId function, RefQualifier ref);

	// That ref-qualifier.  `RefQualifier::None` for every type that is not a
	// function, and for a function whose declarator wrote none.
	RefQualifier function_ref_qualifier(TypeId type) const
	{
		return kind(type) == TypeKind::Function
			? static_cast<RefQualifier>(nodes_[type].ref_qualifier)
			: RefQualifier::None;
	}

	// 8.3.2p6: a reference to a reference collapses, and only two rvalue
	// references make an rvalue reference.
	TypeId reference_to(TypeId type, bool rvalue);

	// 8.3.4p1's array, whose bound is two facts rather than one: how many
	// elements it has, and - where an argument list has yet to say - the place
	// the constant-expression named.  `place` is `kNoType` for every bound a
	// reading settled, so a settled array is interned by its number exactly as
	// it was before there was a place to write; a dependent one carries the
	// place in the key, which is what keeps `T[N]` and `T[M]` two types where
	// the number both stand in with is one.
	TypeId array_of(TypeId element, bool bounded, unsigned long long bound,
	                TypeId place = kNoType);
	// 8.3.4p1: the place a dependent bound named, `kNoType` for a settled one.
	TypeId bound_place(TypeId type) const { return nodes_[type].bound_place; }
	// The same array over another element type, with the bound exactly as it
	// was written.  Every rebuild of an array - cv-qualification travelling to
	// the element, 13.3's bare type, a substitution of the element alone - is
	// this one reading, so no caller has to know the bound is two facts.
	TypeId rebuilt_array(TypeId array, TypeId element)
	{
		return array_of(element, bounded(array), bound(array),
		                bound_place(array));
	}
	// 8.3.4p1 under 14.3p1: the same array with `element` for its element type
	// and whatever the substitution made of its bound place for its bound.  A
	// place the arguments settled leaves a number and no place - `T[N]` with
	// `N` bound to 5 is `int[5]` - and one they did not is carried as it was.
	TypeId substituted_array(TypeId array, TypeId element, TypeId settled);

	// 8.3.2p5 and 8.3.4p1: the same two types, derived the way a *declarator*
	// derives them - which is the one place the standard says what may not be
	// derived at all.  Three readers do it and no other: the tree walk over a
	// ptr-operator and a declarator suffix, the reader that walks a flattened
	// spelling, and 14.3p1's substitution of an argument into a pattern.  All
	// three go through these doors so that 14.8.2p8 gets one answer from each -
	// a candidate that drops rather than a program that ends.
	//
	// The three entries above stay what they are, because the table interns
	// synthetic types no declarator wrote: 13.1's key for a member function
	// spells its ref-qualifier as a reference to `cv void`, and 14.8.2.1p3
	// derives a reference to stand a deduction's argument up as an lvalue.
	// Those are keys and stand-ins, not types a program may declare an object
	// of, so the refusal belongs at the door and not at the table.
	TypeId derived_reference(TypeId target, bool rvalue);
	TypeId derived_array(TypeId element, bool bounded, unsigned long long bound,
	                     TypeId place = kNoType);
	TypeId derived_substituted_array(TypeId array, TypeId element,
	                                 TypeId settled);
	// 8.3.4p1's own half of that, for the two doors that both ask it.
	void require_derivable_element(TypeId element);
	// 8.3.4p1: the type of one element of an array, however many dimensions it
	// has, and the type itself for anything else.  It is what 12.6p1's
	// construction, 12.4p8's destruction and 5.3.4's allocation of an array all
	// ask the array about, so it is one reading of the type and not one per
	// reader.
	TypeId element_of(TypeId type);
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

	// 14.1p2: whether the place a template parameter declared binds a template
	// rather than a type or a value.  It is a fact of the type the place
	// declared, so a naming written inside the pattern - `F<int>` - reads it
	// without the head that declared `F`.
	bool is_parameter_template(TypeId type) const
	{
		return kind(type) == TypeKind::TemplateParameter && user_at(type).scoped;
	}

	// 14.3.3p1: the entry standing for the template `entity` declares, which is
	// what a template-template place binds.  One per template, so two argument
	// lists that named the same template are one list.
	TypeId template_name_type(std::uint32_t entity, const std::string& name,
	                          const std::string& qualified);
	bool is_template_name(TypeId type) const
	{
		return kind(type) == TypeKind::TemplateName;
	}

	// 14.6.2p1: a template-id whose template is a place no argument list has
	// settled - `F<T>` inside the pattern that declared `F`.  It stands for
	// whatever the argument bound to `F` makes of the list, so it is interned by
	// the place and the list and answers dependent until a substitution reaches
	// it.  `kNoType` for every type that is no such naming.
	TypeId dependent_template_id(std::uint32_t entity, TypeId parameter,
	                             const std::vector<TypeId>& arguments,
	                             const std::string& name);
	TypeId applied_template(TypeId type) const
	{
		return kind(type) == TypeKind::TemplateParameter
			? user_at(type).applied_template
			: kNoType;
	}

	// 14.3.2p1: the value a template-argument-list bound to a non-type place,
	// as the type it was converted to and the bits it holds.  Two arguments are
	// the same argument exactly when both agree, which is what 14.4p1 makes one
	// specialization of two namings.
	TypeId value_type(TypeId type, unsigned long long bits);
	bool is_value(TypeId type) const { return kind(type) == TypeKind::Value; }
	unsigned long long value_bits(TypeId type) const { return nodes_[type].bound; }

	// The same entry for a value of one of 3.9.1p8's floating types, which no
	// `unsigned long long` holds: the x86-64 `long double` has a 64 bit
	// significand beside its sign and exponent.  So the entry is interned by an
	// index into the pool of the distinct floating values this translation has
	// met, and the pool answers what the index stands for.  A value met twice
	// is one index, which is what keeps an entry a key two folds agree on.
	TypeId real_type(TypeId type, long double value);
	long double value_real(TypeId type) const { return reals_[nodes_[type].bound]; }

	// 14.5.3p1: the run of arguments a template-argument-list bound to a pack
	// place, and 14.5.3p4's expansion of a pattern that names one.  A run is
	// interned by its elements, so `Ts` bound to `<int, long>` is one entry
	// however many places read it; an expansion is interned by its pattern.
	TypeId pack_type(const std::vector<TypeId>& elements);
	TypeId pack_expansion(TypeId pattern);
	bool is_pack(TypeId type) const { return kind(type) == TypeKind::Pack; }
	// True for `P...` and false for a bound run: an expansion is the one of the
	// two that still has a pattern under it.
	bool is_pack_expansion(TypeId type) const
	{
		return kind(type) == TypeKind::Pack && target(type) != kNoType;
	}
	// The other of the two: a run an argument list has settled, which is what a
	// pack's name stands for and what no place is ever declared of.
	bool is_settled_run(TypeId type) const
	{
		return kind(type) == TypeKind::Pack && target(type) == kNoType;
	}
	const std::vector<TypeId>& pack_elements(TypeId type) const
	{
		return *parameter_lists_[nodes_[type].parameters];
	}

	// 14.5.3p1: whether a template parameter is a pack, which says that the
	// argument bound to it is a run rather than one argument and that a name
	// written for it shall be expanded.  It is a fact of the type the place
	// declared, so a substitution reaches it without the head.
	void set_template_pack(TypeId type, bool pack);
	bool is_template_pack(TypeId type) const
	{
		return kind(type) == TypeKind::TemplateParameter &&
			user_at(type).template_pack;
	}

	// 14.1p4: the type a non-type template parameter names a value of, kept on
	// the parameter's own type so that a region binding it - and a deduction
	// walking it - can tell a value place from a type place without the head
	// that declared it.  `kNoType` for a type parameter.
	void set_parameter_value_type(TypeId type, TypeId value_type);
	TypeId parameter_value_type(TypeId type) const
	{
		return kind(type) == TypeKind::TemplateParameter
			? user_at(type).parameter_value_type
			: kNoType;
	}

	// The name the dump spells a user-defined type with.  7.1.3p2 lets a
	// declaration name an unnamed class after it has been read, so the name can
	// arrive after the type.
	void rename(TypeId type, const std::string& name,
	            const std::string& qualified);
	// That name, which 9.1p2 makes what a member of the type is written after.
	const std::string& user_name(TypeId type) const { return user_at(type).name; }
	// 3.4.3: the same type named from outside every region that encloses its
	// declaration, which is what an object-file name for it is built from.  The
	// dump spells a type as the declaration wrote it, so the two differ.
	const std::string& user_qualified_name(TypeId type) const
	{
		return user_at(type).qualified;
	}

	// 14.7.1p1 and the ABI's `<template-args>`: the template this class is a
	// specialization of, named from outside every region around it, and the
	// types its template-argument list bound to the parameters.  A name
	// encoded from the type writes the two apart - the template's name and
	// then its arguments - which the one spelling `qualified` carries cannot
	// be split back into, so the specialization records them as the facts they
	// are.  Empty for every class no template made.
	//
	// 14.5.3p1: the arguments a pack place took are one argument of the list
	// the object file writes, however many the run holds - so `pack_at` is the
	// place the run begins at, and `kNoPackPlace` where the head declared none.
	void set_template_arguments(TypeId type, const std::string& templated,
	                            const std::vector<TypeId>& arguments,
	                            unsigned pack_at);
	static const unsigned kNoPackPlace = 0xffffffffu;
	unsigned template_pack_place(TypeId type) const
	{
		return user_at(type).template_pack_place;
	}
	bool is_specialization(TypeId type) const
	{
		return !user_at(type).template_name.empty();
	}
	const std::string& template_name(TypeId type) const
	{
		return user_at(type).template_name;
	}
	const std::vector<TypeId>& template_arguments(TypeId type) const
	{
		return user_at(type).template_arguments;
	}

	// 14.1p2 and the ABI's `<template-param>`: which of its template's
	// parameters this one is, counted from zero.  A specialization's own name
	// is encoded from the *template's* signature, where a parameter stands for
	// itself, and the ABI writes it by its place rather than by its spelling -
	// so the place is a fact of the type the parameter declared.
	void set_template_index(TypeId type, unsigned index);
	unsigned template_index(TypeId type) const
	{
		return user_at(type).template_index;
	}

	// 14.6.2p1 and 14.2: the prefix a name written through a dependent one
	// stands after, and that name.  A type made this way is a member of a class
	// only an argument list names, and the ABI writes the two apart - the
	// prefix and then the name - which the one spelling it is diagnosed by
	// cannot be split back into.  `kNoType` for every type no dependent prefix
	// made.
	void set_dependent_member(TypeId type, TypeId owner,
	                          const std::string& member);
	TypeId dependent_owner(TypeId type) const
	{
		// A stand-in for a member of a class no argument list has named is a
		// parameter-kind entry, so every other kind - which holds no user
		// record to read - stands after no prefix at all.
		return kind(type) == TypeKind::TemplateParameter
			? user_at(type).dependent_owner
			: kNoType;
	}
	const std::string& dependent_member(TypeId type) const
	{
		return user_at(type).dependent_member;
	}

	// 14.6.2.1p9: a class or enumeration nested in the current instantiation is
	// a dependent type, however plainly its own declaration is written - what
	// an object of it holds and what its members are is what the enclosing
	// argument list says.  A nest of them is answered by the one flag, because
	// the level above was asked the same question when it was made.
	void set_nested_in_dependent(TypeId type);
	// 9.1p2 and 14.2: the declaration this class or enumeration was made by.
	// The spelling `user_qualified_name` carries names every region around it
	// in one string, and a component of it that a template made cannot be
	// split back out - so a name the object file encodes reads the regions the
	// declaration itself stands in.  Null for a type no declaration owns.
	void set_declaration(TypeId type, const SemaEntity* declaration);
	const SemaEntity* declaration(TypeId type) const
	{
		return user_at(type).declaration;
	}

	// 14.3.2p1: the object or function a value at one of 14.1p4's address
	// places designates, asked by the entry number that value holds.
	//
	// Those bits are an entry of the constant-address table, which is this
	// translation unit's own numbering in the order the unit reached each
	// address - so an object-file name built from a *use* of the type would
	// name one entity differently in every unit that wrote it.  What the name
	// has to write is the declaration, and the type is the whole of what such a
	// use holds, so the table carries the answer beside it.  Null for the null
	// pointer value and for every entry no argument named.
	void set_address_object(unsigned long long bits, const SemaEntity* object);
	const SemaEntity* address_object(unsigned long long bits) const;

	// 9.8p1: the function whose body declared this class or enumeration, and
	// its place among the types of that name the function declares.  Settled
	// where the declaration is read, because the region it was written in is
	// the only thing that says either.
	void set_local_name(TypeId type, const SemaEntity* function,
	                    unsigned occurrence, bool unnamed);
	const SemaEntity* local_function(TypeId type) const
	{
		return user_at(type).local_function;
	}
	unsigned local_occurrence(TypeId type) const
	{
		return user_at(type).local_occurrence;
	}
	// The ABI's `<unnamed-type-name>`: whether the region gave the type no name
	// at all, so that the number above is what names it there.
	bool local_unnamed(TypeId type) const
	{
		return user_at(type).local_unnamed;
	}

	// 9.2p2: the class becomes complete at the end of its member specification,
	// which is where its size and alignment are first known.
	// `zeroed_storage` is 8.5p8's answer for the same class: whether any byte
	// of an object of it is written by zero-initialization, which is what its
	// bases and members hold rather than what its size comes to.
	// `subobject_bytes` is 12.8p12 over the storage the layout just walked -
	// whether the base subobject and every member is carried by its own bytes -
	// which 5.2.2p4's boundary reads for a class that derives from something.
	void complete_class(TypeId type, unsigned long long size,
	                    unsigned long long align, bool empty,
	                    bool trivially_copied, bool zeroed_storage,
	                    bool subobject_bytes);

	// 8.5p8: whether zero-initializing an object of `type` writes anything at
	// all.  It is over the storage the bases and the non-static data members
	// hold, so a subobject that holds nothing holds none of it and a class every
	// subobject of which holds nothing is zeroed by writing no byte.  1.8p5
	// still gives such an object a size, which is why the size cannot answer
	// this.  True for every type that is not a class, and for an array of one
	// whichever way its element answers.
	bool has_zeroed_storage(TypeId type);

	// 9p6: whether an object of the class holds nothing, which is what says a
	// copy of one moves no bytes.  False for every type that is not a class.
	bool is_empty_class(TypeId type) const;

	// 12.8p11, p12 and 12.4p8: what the class's copy constructor is - whether a
	// copy of an object of it is the copy of its bytes, and whether 8.4.3p2
	// leaves the program no copy of one at all - and what the end of an object
	// of it comes to.  The layout writes a first answer to the first of them
	// from the declarations the class holds; all three are settled where the
	// class's copy constructor is, which is one step later than the layout and
	// is the answer every reader wants.  `derived` is 10p1's question of whether
	// the class has a base class subobject, which is what says the boundary
	// reads the storage the class is laid out over rather than the class's own
	// copy constructor.
	void settle_copy_facts(TypeId type, bool trivially_copied,
	                       bool copy_deleted, bool vacuous_destruction,
	                       bool derived);

	// 8.4.3p2 and 12.8p11: whether the copy constructor of the class is one no
	// program may name, which is what says a transfer of an object of it is the
	// member the program declared for it rather than its bytes.  False for
	// every type that is not a class.
	bool is_copy_deleted(TypeId type) const;

	// 12.8p12: whether a copy of an object of the class is the copy of its
	// bytes, which it is until the program writes a copy constructor of its own
	// or holds a subobject whose copy is not.  True for every type that is not
	// a class.
	bool is_trivially_copied(TypeId type) const;

	// 12.4p8: whether the end of the lifetime of an object of the class comes
	// to nothing at all - which is broader than 12.4p5's triviality by exactly
	// the clause that says so, because a destructor whose body writes no
	// statement runs nothing however the program declared it.  True for every
	// type that is not a class.
	bool has_vacuous_destruction(TypeId type);

	// 12.4p3: whether the program declared a destructor anywhere in the
	// subobject tree of the class.  It is the other of the two questions an end
	// of a lifetime asks, and which of them is asked is which kind of object is
	// ending: an object a declaration named reads 12.4p8's vacuity, because the
	// block that declared it is where the program can watch its end; an object
	// the *translation* made - 12.2p1's temporary, 5.2.2p4's parameter object,
	// the object a delete-expression ends - is reached through an address, and
	// the call is the one mark its end has, so it is written wherever the
	// program named a destructor at all.  False for every type that is not a
	// class.
	bool has_declared_destruction(TypeId type);
	// Settled where the class completes, from whether the program wrote a
	// destructor of its own and from the same subobjects 12.4p8 walks.
	void settle_declared_destruction(TypeId type, bool declared);

	// 12.8p12 and 12.4p8 as one question: whether the bytes of an object of the
	// class stand for the object.  They do exactly where a copy of one is the
	// copy of its bytes *and* nothing runs when one ends - an object something
	// runs at the end of is one the program can watch, so a second object made
	// out of its bytes is a second thing to run at the end of and not a copy.
	// This is what an initialization, an argument, a returned object, a
	// conditional's arm and 12.8p31's elision all ask.  True for every type
	// that is not a class.
	bool bytes_stand_for_object(TypeId type);

	// 6.6.3p2 and 5.2.2p4: whether an object of this type is returned in
	// storage the caller gives the function rather than as a value the call
	// hands back.  A class the bytes are not the copy of cannot be handed back
	// as bytes at all - the copy is a call, and a call needs an object to make
	// it into - and a class whose object is wider than two words is one the
	// boundary carries by its address whether or not its copy is trivial.  It
	// is one fact of the type, settled where 12.8p12's is, so the signature the
	// declaration writes, the definition's return and every call of it read the
	// same answer rather than each working one out.
	bool returns_indirectly(TypeId type);

	// 5.2.2p4: whether a parameter of this type is passed as the address of the
	// object the caller built rather than as the bytes that object holds.  A
	// class whose bytes do not stand for the object is one the boundary cannot
	// carry as a value: the caller has to be able to name the object it built
	// and the callee has to be able to name the one it was given, so the two
	// name one object.  It is the other half of `returns_indirectly` and read at
	// the same three places - the declaration, the definition and the call -
	// except that width says nothing here, because an argument the caller
	// already laid out costs nothing to point at however narrow it is, and that
	// the copy half is `carried_by_bytes` rather than the class's own copy.
	bool passes_indirectly(TypeId type);

	// 3.9.1p8: whether any scalar subobject of `type` is of a floating type,
	// which is what the two boundaries above ask of a class narrow enough to
	// have been carried in registers.  An array answers for its element and a
	// floating type for itself, so the walk that lays a class out settles it
	// with one read of each base and member.
	bool holds_floating_storage(TypeId type);
	void settle_floating_storage(TypeId type, bool holds);

	// 10.4p2: whether a final overrider of the class is a declaration the
	// program left pure, which is what says no object of it can be created.
	// The class walk settles it once at the closing brace and 8.3.4p1 reads it
	// here, because forming an array of the class is forming its elements and
	// the table that forms types is where every writer of one meets.
	bool is_abstract_class(TypeId type);
	void settle_abstract_class(TypeId type, bool abstract);

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
	// 3.9p10: an arithmetic type, an enumeration, a pointer, a pointer to
	// member or `std::nullptr_t`, each possibly cv-qualified - the types an
	// object of which holds one value rather than subobjects.
	bool is_scalar(TypeId type) const;
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
	// 8.3.5p5 over the object a parameter is rather than over the function
	// type: the array and the function become pointers, and the top-level
	// cv-qualifiers the clause drops from the type stay on the object.
	TypeId parameter_object(TypeId type);

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
	// 5.3.3p2 and 5.3.6p3: the type `sizeof` and `alignof` measure when a
	// type-id names a reference, which is the type referred to and not the
	// storage a reference occupies.  It stands beside the two answers it is
	// asked before, because 5.19's tree reading, its spelling reading and the
	// expression layer each write the operator and one of them writing this
	// clause and not the others is how `sizeof(char (&)[2])` came to be 8.
	TypeId measured_type(TypeId type) const;

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

	// The types that list holds, which is what a fact keyed by a list reads
	// back: the template-argument list a specialization was made from is one.
	const std::vector<TypeId>& type_list_at(std::uint32_t list) const
	{
		return *parameter_lists_[list];
	}

	// 14.3 and 14.8.2: `type` with every template parameter `bindings` names
	// replaced by the type bound to it, keeping the qualifiers written around
	// each.  `memo` holds the answers of one substitution, so a type reached
	// twice is rebuilt once; a type that holds no template parameter is itself
	// and interns nothing.
	TypeId substitute(TypeId type,
	                  const std::unordered_map<TypeId, TypeId>& bindings,
	                  std::unordered_map<TypeId, TypeId>& memo);

	// 14.6.2p1: whether the type is written in terms of a template parameter,
	// which is what makes a name that mentions it dependent.  A specialization
	// is dependent when one of the arguments that made it is, so the walk asks
	// the arguments the type records rather than the class's members.
	//
	// The answer is a fact of the type and never changes, so it is kept: the
	// arguments of one specialization are a graph and not a tree - `P<t,t>`
	// reaches `t` twice - and a walk that asks again at every edge is
	// exponential in the depth of a nest a few lines of source can write.
	bool is_dependent(TypeId type) const;

	// The type in the form PA2 and PA7 print it in.
	std::string description(TypeId type) const;

private:
	// One type.  Every category fits the same record, so a type is one index
	// and the table is one flat array.
	struct Node
	{
		Node();

		TypeKind kind;
		unsigned char cv;
		// Function: 8.3.5p1's ref-qualifier, as a `RefQualifier`.  It is stored
		// beside `cv` because it is written where the cv-qualifier-seq is
		// written and tells two function types apart the same way.
		unsigned char ref_qualifier;
		bool bounded;
		bool variadic;
		EFundamentalType fundamental;
		TypeId target;
		unsigned long long bound;
		// Array: 8.3.4p1's bound where the constant-expression named a place
		// an argument list has yet to settle.  `bound` stands one element in
		// for it so that every reader that asks how large the array is answers
		// as it did before; this is what says the number is a stand-in and
		// which place a substitution replaces it from.
		TypeId bound_place;
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
		// 12.8p12 over the storage the class is laid out over: whether the base
		// class subobject and every non-static data member is carried by its
		// own bytes, which is the same walk with the class's *own* declaration
		// of a copy constructor left out of it.
		bool subobject_bytes = true;
		// 12.8p12 as 5.2.2p4's boundary reads it: whether the boundary carries
		// an object of the class as its bytes.  It is the class's own copy fact
		// where the class derives from nothing, and 10p1's storage it is laid
		// out over where it derives from something - a derived class is carried
		// the way the subobjects it is made of are.
		bool carried_by_bytes = true;
		// 12.4p8: whether the end of the lifetime of an object of the class
		// comes to nothing, which the ABI and every copy read beside the copy
		// fact: bytes stand for an object only while nothing runs at its end.
		bool vacuous_destruction = true;
		// 12.4p3: whether the program itself declared a destructor anywhere in
		// the subobject tree of the class, which is what says an end of a
		// lifetime the *translation* writes is a call.
		bool declared_destruction = false;
		// 3.9.1p8 through the storage the class is laid out over: whether any
		// scalar subobject of it is of a floating type.  5.2.2p4's boundary is
		// what asks - the course ABI carries such an object by its address, so
		// that no boundary of this milestone names a floating register - and
		// the walk that lays the class out is the one pass that can answer it,
		// because a base and a member each already carry their own answer.
		bool floating_storage = false;
		// 10.4p2: whether any final overrider of the class is still pure, which
		// the vtable pass settles once and 8.3.4p1 reads of an array's element.
		bool abstract = false;
		// 8.5p8: whether any byte of an object of the class is written by
		// zero-initialization, which is what its bases and members hold.
		bool zeroed_storage = true;
		// 12.8p11: whether the class's copy constructor is one 8.4.3p2 leaves
		// no program able to name, which is what says an object of it is
		// carried by the member the program declared and not by its bytes.
		bool copy_deleted;
		// 9.8p1: the function whose body declared this type, and which
		// occurrence of the name there it is.  3.5p8 gives it no linkage, so
		// the regions the dump writes do not tell it from another function's
		// type of the same spelling - only the function does, and the object
		// file names it after that function.  Null for every type declared at
		// namespace or class scope.
		const SemaEntity* local_function = nullptr;
		unsigned local_occurrence = 0;
		bool local_unnamed = false;
		// 9.1p2: the declaration that made this type, which is what says
		// which regions it is named through.  `qualified` is one string, and a
		// component of it a template made cannot be split back out of it, so a
		// name the object file encodes walks the declaration instead.
		const SemaEntity* declaration = nullptr;
		// 14.7.1p1: the template a specialization was made of and the
		// arguments that made it, empty for every class a template-id did not
		// name.
		std::string template_name;
		std::vector<TypeId> template_arguments;
		// 14.5.3p1: the place among them the pack's run begins at, which the
		// flattened list cannot be split back into.
		unsigned template_pack_place = 0xffffffffu;
		// 14.6.2.1p9: whether the region this class or enumeration was
		// declared in is one an argument list has yet to settle.
		bool nested_in_dependent = false;
		// 14.1p2: which parameter of its template a template parameter is.
		unsigned template_index = 0;
		// 14.5.3p1: whether that parameter is a pack.
		bool template_pack = false;
		// 14.1p4: the type a non-type template parameter names a value of.
		TypeId parameter_value_type = kNoType;
		// 14.6.2p1: the prefix a dependent member name stands after and the
		// name itself, which the ABI writes apart.  `kNoType` for every other
		// type.
		TypeId dependent_owner = kNoType;
		std::string dependent_member;
		// 14.6.2p1 at a template-template place: the place a dependent
		// template-id applied its argument list to, `kNoType` for every type
		// that is not one.  The list itself stands in `template_arguments`,
		// which is the one spelling every other argument list is read as.
		TypeId applied_template = kNoType;
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

	// 14.6.2p1 asked of the type itself, which is what the memo above holds.
	bool dependent_walk(TypeId type) const;

	std::vector<Node> nodes_;
	// A record already made never moves: `user_name`, `template_arguments` and
	// the rest are read as references, and asking a class for its definition
	// makes types - so a vector would leave every one of those readings holding
	// a record the next `class_type` had moved out from under it.
	std::deque<UserType> user_types_;
	// The array types `qualified` is between, innermost last.
	std::vector<TypeId> dimensions_;
	// 4.8: the distinct floating values a value entry has been made for, and
	// the index each is held at.  The key is the decomposition 3.9.1p8's
	// representation gives one - sign, exponent and significand - which tells
	// `0.0` from `-0.0` and tells two `long double`s apart that agree to
	// `double` precision, neither of which a comparison of `double`s does.
	std::vector<long double> reals_;
	std::map<std::pair<long long, unsigned long long>, unsigned long long>
		real_ids_;
	std::unordered_map<Key, TypeId, KeyHash> ids_;
	// The unqualified type each declaration of a class, enumeration or template
	// parameter introduced, keyed by the entity that declared it.
	std::unordered_map<std::uint64_t, TypeId> user_ids_;
	std::unordered_map<std::vector<TypeId>, std::uint32_t, ListHash> parameter_ids_;
	std::vector<const std::vector<TypeId>*> parameter_lists_;
	// 14.3.2p1: which declaration each entry of the constant-address table
	// stands for, for the one reader that has a value and needs a name.
	std::unordered_map<unsigned long long, const SemaEntity*> address_objects_;
	// 14.6.2p1's answer for each type it has been asked of.  A specialization's
	// arguments are a graph, so the walk below reaches one type by as many
	// paths as the nest above it has; the answer is a fact of the type, so it
	// is read once and kept.
	mutable std::unordered_map<TypeId, bool> dependent_;
};
