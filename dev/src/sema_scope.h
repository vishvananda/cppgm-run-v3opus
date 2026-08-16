#pragma once

#include <cstdint>
#include <deque>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sema_facts.h"
#include "type_model.h"

// The declarations, scopes and bindings of one translation unit.
//
// This is the first semantic layer: what a declaration introduced, which
// region it introduced it into, and the two lookups of 3.4 over the result.
// It knows nothing of syntax, so a later assignment that needs a fact about a
// declaration asks the model rather than the tree it was read from.

// What a name denotes (3.3, 3.4, 7.1.3, 7.2, 7.3).
// 11p1: the access specifier a member was declared under.
const unsigned char kPublicAccess = 0;
const unsigned char kProtectedAccess = 1;
const unsigned char kPrivateAccess = 2;

// 12.1 and 12.4: which special member function a declaration declares.  A
// constructor and a destructor have no name of their own that a lookup finds,
// they are reached from the class rather than through a binding, and the object
// file names them by what they are, so what they are is a fact the declaration
// carries.
const unsigned char kOrdinaryFunction = 0;
const unsigned char kConstructorFunction = 1;
const unsigned char kDestructorFunction = 2;

// 12.8: which of the four special members that carry the value of one object of
// a class into another this declaration declares.  p7/p9/p18/p20 say which of
// them a class that declared none has, p11/p23 which are deleted, p12/p25 which
// are trivial, and p15/p28 what the definition of one comes to - and every one
// of those questions is asked of a declaration rather than of the syntax that
// wrote it, so which of the four it is, is a fact the declaration carries.
const unsigned char kNotTransfer = 0;
const unsigned char kCopyConstructorTransfer = 1;
const unsigned char kMoveConstructorTransfer = 2;
const unsigned char kCopyAssignmentTransfer = 3;
const unsigned char kMoveAssignmentTransfer = 4;
const unsigned char kTransferKinds = 4;


// 1.4p8 and 17.6.4.3.2p1: the names an implementation reserves for functions
// of its own, which a program declares nothing of and every use of names the
// one function the implementation provides.  Which one it is, is what says
// what the object file calls it and what a call of it may be assumed to do -
// so it is a fact the reserved declaration carries rather than one a backend
// works out from the spelling again.
const unsigned char kNotBuiltin = 0;
const unsigned char kBuiltinMemcpy = 1;
const unsigned char kBuiltinMemmove = 2;
const unsigned char kBuiltinStrlen = 3;
const unsigned char kBuiltinUnreachable = 4;
// 3.7.4.1p2 and 3.7.4.2p2: the four allocation and deallocation functions every
// translation unit declares whether or not it wrote them.  A program may
// replace any of them, and a definition of one is a definition of the same
// function this declaration made - so the object file names them by what the
// implementation calls them and not by 3.7.4p2's encoding of the declaration.
const unsigned char kBuiltinOperatorNew = 5;
const unsigned char kBuiltinOperatorNewArray = 6;
const unsigned char kBuiltinOperatorDelete = 7;
const unsigned char kBuiltinOperatorDeleteArray = 8;

enum class SemaKind
{
	Namespace,
	NamespaceAlias,
	Class,
	Enum,
	Typedef,
	Variable,
	Function,
	Parameter,
	Enumerator,
	TemplateType,
	// 14.1p4: a template parameter that names a value rather than a type.  It
	// is a declaration of its own kind because 14.3.2p1 lets the argument bound
	// to it be read wherever 5.19 asks for a constant and nowhere a type-name
	// is asked for.
	TemplateValue
};

// The declarative regions of 3.3, which are also the scopes the dump names.
enum class ScopeKind
{
	Namespace,
	TemplateParameters,
	Class,
	Enum,
	Function,
	Block,
	// 3.3.7p1: the places a function declarator wrote, which stand from the
	// declarator-id that named each of them to the end of that declarator - so
	// a later parameter's own type-id and 8.3.5p2's trailing-return-type name
	// them and nothing outside the declarator does.  It is a region of its own
	// rather than the function's, because 8.3.5p10 leaves a parameter's name
	// out of the function's type and a declaration that is not a definition
	// opens no region at all.
	Prototype
};

// Which declarations a lookup accepts.  3.4 gives contexts that see only part
// of what a name is bound to: an elaborated-type-specifier sees a class that
// an ordinary declaration hides (3.4.4p2), and a namespace-name sees only a
// namespace or an alias of one (7.3.1p3).
enum class LookupKind
{
	Any,
	Type,
	Space,
	// A name written before `::`, which 3.4.3p1 lets name a namespace, a
	// class, an enumeration, or an alias of one.  Asking for all four at once
	// is what keeps a nested-name-specifier one lookup rather than one per
	// kind it might be.
	Region
};

class Scope;

struct SemaEntity;
// 14p1: the pattern a template-declaration parameterises, which the semantic
// walk owns because it is syntax and this layer knows none.
struct TemplateInfo;
// 7.1.5p3: the function-body a constexpr function was defined with, which the
// fold of a call re-reads and this layer knows no syntax of.
struct AstNode;

// The slot index a virtual function has in every vtable that holds it, or
// `kNoVtableIndex` for a declaration that is not virtual.  The index a base
// assigns is the one every class below it keeps, so it is a fact of the
// declaration rather than of one class's table.
const unsigned kNoVtableIndex = static_cast<unsigned>(-1);

// The ABI: the size and the alignment of the vpointer a polymorphic class puts
// at the start of every object of it.
const unsigned long long kVpointerBytes = 8;

// The ABI: the two words a virtual function table holds in front of its slots -
// 10p1's offset from the vpointer back to the start of the complete object,
// which single inheritance leaves at zero, and 5.2.8's record naming the type.
// A vpointer addresses the first slot, so it is the table's address plus these.
const unsigned long long kVtablePrefixBytes = 16;

// 10.3p10 and the ABI: one entry of a class's virtual function table.
//
// The entry names the final overrider that class has for the slot, which is
// the function a call through an object of it runs.  A slot whose overrider is
// pure carries no function at all in the emitted table; the fact is on the
// declaration, so the slot does not repeat it.
struct VirtualSlot
{
	SemaEntity* overrider;
	// The ABI gives a virtual destructor two consecutive slots: the first runs
	// the destructor on the object, and this one runs it and then frees the
	// storage.  Both name the same declaration, so which of the two an entry
	// is, is the one thing the entry itself has to say.
	bool deleting;
};

// 10p1 and 9.2p13: one base-specifier of a class, as the three facts every
// later question reads it by - which class the object holds a subobject of,
// where in the object that subobject begins, and the access 11.2p1 gave the
// link.  A base-specifier-list of n entries leaves n of these in the order it
// wrote them, which is the order 12.6.2p10 constructs them in.
struct BaseClass
{
	SemaEntity* entity;
	unsigned long long offset;
	unsigned char access;
};

// One declared entity.
//
// A name is bound to an entity, and several names can be bound to the same
// one: a using-declaration and a namespace alias add a binding without
// declaring anything new, so the entity stays where it was declared.
struct SemaEntity
{
	SemaKind kind;
	std::string name;
	TypeId type;
	// Namespace, NamespaceAlias, Class and Enum: the region the declaration
	// introduced, which is what a qualified name looks into.
	Scope* scope;
	// A class with a definition, a function with a body, an enumeration whose
	// enumerators have been read.
	bool defined;
	// 5.19: a value the translation knows, which an enumerator always has and
	// a const object of integral type has when its initializer is constant.
	bool constant;
	unsigned long long value;
	// 13.1: the other declarations of this name in this region, in declaration
	// order.  A name is bound to the first of them and the rest are reached
	// from it, so collecting the candidates of a call costs one walk of the
	// ones there are.
	SemaEntity* next;
	// The last of that chain, held on the first, so a declaration joins it
	// without walking what is already there.
	SemaEntity* tail;
	// The region this declaration was made in, which is what tells a member of
	// a class from a variable of a block: 9.2p1 makes the first reachable only
	// through an object, and the region it was declared in is the one fact
	// about it that says so.  It is the region of the declaration rather than
	// of a binding, so a using-declaration or 9.5p1 injection leaves it alone.
	Scope* region;
	// 9.5p1: the object an anonymous union's member is a member of.  The union
	// has no name, so its members are reached through the object the union
	// declared rather than through one the program named.
	SemaEntity* storage;
	// Class: 12.1, the constructors of the class, as the chain a declaration of
	// an object chooses from by 13.3.1.3.  It is held on the class because a
	// constructor has no name a lookup reaches, and the chain is in declaration
	// order, with 12.1p5's implicit one the only member when no declaration
	// wrote any.
	SemaEntity* constructor;
	// Class: 12.4p1, the destructor of the class, held here for the same
	// reason: the end of an object's lifetime asks the class for it.
	SemaEntity* destructor;
	// Class: 8.5.1p2 and 13.3.1.7, the constructor an object of this aggregate
	// class is built by where it is an object of its own rather than a
	// subobject the clauses reach into - an element of an array of it, a
	// temporary, an argument.  It takes the non-static data members of the
	// class, in the order 9.2p13 laid them out and under the names they were
	// declared with, and initializes each with the one of the same name.  No
	// lookup reaches it and 13.3.1.3 does not walk it, so it is held apart from
	// the chain `constructor` heads; null until the first use asks for one.
	SemaEntity* member_constructor;
	// Function: whether this is that constructor, which is what says its
	// definition initializes each member from the parameter of the same name.
	bool member_entry;
	// Class: 10p1, the direct base classes, which every object of this class
	// holds one subobject of each of.  They are the one fact the base-clause
	// establishes, in the order it wrote them, and layout, 10.2 lookup, 11.2
	// access, 12.6.2 construction, 12.4 destruction and 4.10p3 conversion each
	// read them rather than the syntax.  Empty for a class no base-clause was
	// written for, which allocates nothing.
	std::vector<BaseClass> bases;
	// Class: whether an object of this class holds nothing - no non-static data
	// member, and a base that is itself empty.  1.8p5 still gives it a size of
	// one byte on its own, and the ABI puts such a base subobject at offset
	// zero without giving the derived class any storage for it, so this is what
	// 9.2p13 asks about a base before it lays the members out after it.
	bool empty_class;
	// Class and the ABI: where every empty class subobject of an object of this
	// class stands, as the class it is of and the byte it begins at.  The ABI
	// gives an empty base subobject offset zero and then forbids a second
	// subobject of that same class from standing there too, so 9.2p13 has to
	// know more than "the base holds nothing": it has to know which class holds
	// nothing where.  The list is what says it, built once where the class is
	// completed from the base's list and each member's, and empty for a class
	// with no empty subobject at all - which is nearly every class.
	std::vector<std::pair<TypeId, unsigned long long> > empty_subobjects;
	// 10.3p1: whether this member function is virtual, which a declaration
	// written `virtual` says and which 10.3p2 also gives one that overrides a
	// virtual function of a base - so the fact is settled where 9.2p2 completes
	// the class and not where the declarator was read.
	bool virtual_function;
	// 10.4p2: whether the declaration wrote a pure-specifier, which leaves the
	// class abstract unless something below it overrides the function.  The
	// emitted slot of one is the ABI's `__cxa_pure_virtual` rather than a body.
	bool pure_virtual;
	// 10.3p4: whether the declaration wrote `final`, which no class below may
	// override.
	bool final_virtual;
	// 10.3p5: whether the declaration wrote `override`, which asks the class to
	// refuse it where it overrides nothing.  It is read at class completion and
	// says nothing afterwards.
	bool override_written;
	// 10.3p2: the virtual function of a base class this one overrides, or null
	// where it overrides none.  It is what 10.3p4's `final` and 10.3p7's
	// covariant return are asked about, and what tells a declaration that
	// merely reuses a name from one that takes a slot the base already has.
	SemaEntity* overridden;
	// The slot this virtual function has in the vtable of its own class and of
	// every class below it, or `kNoVtableIndex` where it has none.  A
	// destructor's two entries are this index and the one after it.
	unsigned vtable_index;
	// Class: 10.3, the final overriders of this class in ABI slot order - the
	// derived class's table is the base's with each overridden entry replaced
	// in place, followed by the slots this class introduces in the declaration
	// order of the members that introduce them.  Empty for every class that
	// dispatches nothing, which is nearly all of them.
	std::vector<VirtualSlot> vtable;
	// Class and the ABI: the first virtual member function this class declares
	// that is neither pure nor inline, which is what says *which* translation
	// unit owes the program this class's table.  A class that has one holds
	// its table in the unit that defines that function alone; a class that has
	// none - every virtual member of it defined in its own body - holds a
	// table in every unit that needs one.  Null for both of those.
	SemaEntity* key_function;
	// Destructor: 5.3.5p9 and 12.5p4's deallocation function, resolved where
	// 9.2p2 completes the class because that is where the destructor becomes
	// virtual and the ABI's deleting entry starts owing a call of it.  The
	// entry is a definition the translation writes with no expression under
	// it, so the lookup cannot be left to the place the call is written.
	SemaEntity* deleting_release;
	// Class: 10.3p1, whether an object of this class carries a vpointer -
	// because the class declares a virtual function or because a base does.
	bool polymorphic;
	// Class: whether this class is the one that *adds* the vpointer, which is a
	// polymorphic class whose base carries none.  The ABI gives it the first
	// eight bytes of the object and lays the base subobject and the members out
	// after them, so 9.2p13 asks this before it places anything.
	bool introduces_vptr;
	// Class: 10.4p2, whether any final overrider of this class is pure - which
	// 10.4p3 forbids as a parameter type, as a return type and as the type of
	// an object.
	bool abstract;
	// 12.1 and 12.4: which special member function this declaration declares,
	// as one of the `kOrdinaryFunction` constants.
	unsigned char special;
	// 12.8: which of the four value-transfer special members this declaration
	// declares, as one of the `kNotTransfer` constants.  It is settled where
	// 9.2p2 completes the class, from the parameter list the declaration wrote,
	// so no later reader has to match a parameter type against a class again.
	unsigned char transfer;
	// Class: 12.8p7/p9/p18/p20's four value-transfer special members of this
	// class, indexed by `transfer - 1`.  A copy constructor and a move
	// constructor have no name a lookup reaches and an assignment operator is
	// one declaration of `operator=` among however many the class wrote, so the
	// one place each of the four can be asked for is the class itself.  Null
	// where the class has none - which 12.8p9 and p20 leave a class that
	// declared a copy member or a destructor of its own with.
	SemaEntity* transfers[kTransferKinds];
	// 1.4p8: which reserved function of the implementation this declaration
	// declares, as one of the `kNotBuiltin` constants.  `kNotBuiltin` for every
	// declaration a program wrote, which is all but the few the analysis makes
	// when a use of a reserved name reaches nothing the program declared.
	unsigned char builtin;
	// 15.4p1: whether the exception-specification this function was declared
	// with says it throws nothing.  C++11 leaves it out of the function type,
	// so it is a fact of the declaration; 5.3.4p15 is the one question this
	// milestone asks of it, and the answer decides whether a new-expression
	// tests the address before it initializes an object there.
	bool nonthrowing;
	// 15.4p14 and 12.4p3: whether any declaration of this function wrote an
	// exception-specification at all.  Where none did, a special member - and a
	// destructor however the program wrote it - has the one an implicit
	// declaration would, which is what the members its definition invokes
	// allow; where one did, what it wrote is what the function says.
	bool wrote_exception_specification;
	// 12.3.1p2: a constructor declared `explicit`, which only
	// direct-initialization may choose.  12.3.2p2 gives a conversion function
	// declared `explicit` the same fact, and 13.3.1.5 the same reading of it.
	bool explicit_function;
	// 12.3.2p1: whether this member function is a conversion function, whose
	// name is a type rather than an identifier.  The type it converts to is its
	// return type, which the declaration wrote as the conversion-type-id - so
	// the one thing that is not already a fact of the type is that the function
	// is one of these, which 13.3.1.5's candidate set and the ABI's `cv`
	// terminal each read here rather than from the spelling of the name.
	bool conversion_function;
	// Class: 12.3.2p1 and 13.3.1.5, the conversion functions *this* class
	// declares, settled where 9.2p2 completes it, so a conversion asks a class
	// what it converts to in one walk rather than a lookup per candidate type.
	// 13.3.1.5p1 also offers the ones a base declares that this class does not
	// hide, and those stay on the base: copying them down would give a
	// hierarchy n deep n^2 entries for the n conversions it declares.
	std::vector<SemaEntity*> conversions;
	// Class: the nearest classes at or above this one whose `conversions` is not
	// empty, and empty where nothing above it declares one.  It is what turns
	// 13.3.1.5p1's candidate set into one walk of the few classes that declare a
	// conversion instead of one walk of every base.  A class that declares one
	// itself is the only entry, so single inheritance leaves at most one here
	// however deep the derivation goes.
	std::vector<SemaEntity*> conversions_above;
	// 12.1/12.4 and the ABI: the ABI gives a constructor and a destructor an
	// entry point for a complete object and one for a base class subobject, and
	// this says whether anything in this translation unit ever ran it on a
	// complete object - which is every object but a base subobject, a member
	// subobject among them.  A special member only a base subobject asked for
	// is the base-object entry alone; one a complete object asked for is the
	// complete-object entry, with the base-object name given to the same body.
	bool complete_object_entry;
	// The same question the other way round: whether a base class subobject
	// ever asked for this special member in this translation unit.  One both
	// asked for stands under both entries, and this milestone has no virtual
	// base to make the two bodies differ - so the base-object entry is the same
	// body emitted again under its own name, which is what a base subobject's
	// own action calls.
	bool base_object_entry;
	// 14.7.1p1: whether that base subobject stood in a body the program wrote
	// out rather than in one an instantiation made.  A use the program wrote is
	// what asks this unit for the whole of an instantiated definition, so the
	// object file holds both of the ABI's entry points for it; a use inside
	// another instantiation asks for the entry it names, because the
	// instantiation that wrote it is what owes the rest.
	bool source_base_entry;
	// 12.1p5, 12.4p4 and 12.8p8: whether this member is one the standard
	// declared for the class rather than one a declaration of the program's
	// did.  14.7.1p1 is what asks: instantiating a class instantiates the
	// declarations the *pattern* wrote, and a member the standard gives a
	// specialization is given by the specialization's own class-specifier
	// exactly as it is given to a class the program wrote out - so it is no
	// part of what an instantiation made.
	bool implicit_declaration;
	// 8.4.2p2 and 9.3p2: whether the definition this unit holds of this special
	// member was written outside its class.  3.2p4 makes such a definition one
	// this unit writes whether or not a call of it stands here - 12.8p12's copy
	// of the bytes leaves none - and the ABI's two entry points are both names
	// the program gave a definition it wrote out.
	bool out_of_class_definition;
	// 14.7.1p1 and 3.2p3: whether a body an instantiation was reading named
	// this function.  Naming it there is what made its definition, and a
	// definition an instantiation made is one this unit holds however the use
	// that named it is carried out - 12.8p12's copy of an object's bytes leaves
	// no call behind and the definition still stands.  A use written outside
	// every instantiation asks for nothing of the sort: there was no definition
	// to make, so a copy carried as bytes there names no function at all.
	bool instantiated_use;
	// 7.3.3p1: the declaration a using-declaration in a class named, which this
	// member of that class stands for.  The class declares it, so 11p1's access
	// and 7.3.3p14's hiding are asked of this declaration, while every use of
	// it - the function a call runs, the offset a member access reads, the name
	// the object file gives it - is a fact about the one it names.  Null for
	// every declaration the program wrote in the region that declares it.
	SemaEntity* shadowed;
	// 12.9p1: the constructor of a direct base class this constructor was
	// declared from, whose parameters it takes and whose call on the base
	// subobject is what its definition does.  Null for a constructor a
	// declaration wrote and for 12.1p5's implicit one.
	SemaEntity* inherited;
	// 12.6.2p6: the constructor of this constructor's own class that its
	// ctor-initializer delegates to, chosen by 13.3 from the arguments the one
	// mem-initializer wrote.  A constructor that delegates initializes no base
	// and no member of its own: the target does all of that and its own body
	// runs after.  Null for every constructor whose ctor-initializer named a
	// base or a member, which is all but the few that delegate - so the chain
	// p6 forbids a cycle in is walked over those alone.
	SemaEntity* delegates_to;
	// 13.3.1.1.2p2: the conversion function this declaration is the surrogate
	// call function of.  An object whose class converts to a pointer to
	// function is callable, and what 13.3 chooses among is a set holding one
	// such declaration per conversion beside the class's own `operator()`s - so
	// the surrogate is a declaration like any other and this is the one thing
	// about it that is not: which conversion the call runs on the object before
	// it calls what the pointer points to.  Null for every declaration a
	// program wrote.
	SemaEntity* surrogate_for;
	// 11.3p5: whether this unit's definition of the function was written in a
	// friend declaration inside a class body, which defines a member of the
	// enclosing namespace where no ordinary lookup finds its name.  The class
	// that wrote it is the only place this unit reads it, so 3.2p2's uses
	// written in it are read there; every other definition no one unit owns is
	// read where a use of it reaches it.
	bool friend_definition;
	// 8.5.1p1 and 12.1p4: whether the program itself wrote what this function
	// does, which `= default` and `= delete` do not.  It is what stops a class
	// with a constructor from being an aggregate.
	bool user_provided;
	// 8.4.3p1: a function definition written `= delete`, which every use of is
	// ill formed.
	bool deleted;
	// 8.4.2p1: whether the definition of this function is the one the standard
	// gives it rather than one the program wrote, which is true of an implicitly
	// declared special member and of one written `= default`.  It is what says
	// the definition is this translation unit's to generate on demand.
	bool defaulted;
	// 11p1: which of the three access specifiers this member was declared
	// under, which says who may name it.  A declaration that is not a member
	// keeps `kPublicAccess`, which every context may name.
	unsigned char access;
	// 8.5.1p1 and 12.6.2p8: whether the member declaration wrote a
	// brace-or-equal-initializer, which is what a constructor that does not
	// mention the member initializes it with - and what stops the class from
	// being an aggregate.
	bool default_initializer;
	// 8.5.1p1: whether an object of this class is initialized from a
	// braced-init-list by initializing its members from the clauses.
	bool aggregate;
	// 9.2p13: where in its class an object of this non-static data member
	// begins, in bytes.  Layout is settled once, where 9.2p2 completes the
	// class, so every later use of the member is one read rather than a walk
	// of the members declared before it.  9.6p2 gives a bit-field no address of
	// its own, so for one this is where the storage unit holding it begins.
	unsigned long long offset;
	// 7.6.2p1: the alignment an alignment-specifier on this declaration asked
	// for, or zero where it wrote none.  9.2p13 allocates the member at the
	// next address the stricter of this and its type's own alignment allows,
	// and 7.6.2p5 makes the class's alignment at least as strict.
	unsigned long long requested_align;
	// 9.6p1: whether the declaration wrote a width, which makes the member a
	// run of bits inside a storage unit rather than an object with an address.
	// The three facts below say nothing about a member no width was written
	// for, which every other data member is.
	bool bit_field;
	// 9.6p1: the width in bits the declaration wrote.  Zero is the unnamed
	// separator, which allocates nothing and only moves the cursor on.
	unsigned bit_width;
	// 9.6p2: where the field's bits begin inside the storage unit `offset`
	// names.  Layout guarantees the field does not leave that unit, so a read
	// or a write is one load, one shift and one mask.
	unsigned bit_offset;
	// 4.5p3: the type a value read out of the field is promoted to, which is
	// `int` wherever `int` represents every value the width allows.  It is the
	// type the storage unit is loaded and masked with, and it is settled here
	// because only the declaration knows the width.
	TypeId bit_access;
	// 7.1.2p2: whether the definition of this function may appear in more than
	// one translation unit, which 9.3p2 also gives a member function defined in
	// its class body and 12.1p5 an implicitly declared constructor.  It is what
	// says the definition binds weakly and is emitted only where it is used.
	bool inline_function;
	// 2.2p1: whether the definition this translation unit holds of this
	// function was written in the unit's own source file rather than in a file
	// it included.  A definition read from an included file is one every unit
	// including that file also holds, so the object file owes only the entry
	// points this unit's own code named; one written here is this unit's
	// contribution and owes what the ABI gives the declaration.  False for a
	// declaration this unit defines nowhere.
	bool own_source_definition;
	// 12.4p8: whether the definition this declaration was given in this
	// translation unit writes no statement at all.  Every definition is read
	// before any body is, so the fact stands wherever a use of the function is
	// later read - which is what lets 3.8p1 ask whether the end of an object's
	// lifetime comes to anything before the destructor's own body has been
	// analysed.  False for a declaration this unit defines nowhere.
	bool empty_body;
	// 12.1p5: whether this constructor does nothing, so that an object it
	// initializes needs no call at all.  A constructor the program wrote, or one
	// with a member to initialize, is not one of these.
	bool trivial;
	// Whether this declaration is reached through an object of the class that
	// declares it: 9.2p1 for a data member, and 9.3.1p3 for a member function,
	// whose type carries that object as the first parameter its declarator did
	// not write.  9.4p1 makes a member declared `static` a member of the class
	// and not of an object, so the region alone does not say.
	bool object_member;
	// 7.1.1p10: whether the declaration wrote `mutable`, which nullifies the
	// const an object of the class carries into this member.  A member function
	// declared `const` may write one, and 5.3.1p3 hands back a pointer to
	// non-const, so it is a fact about the member and not about the object it
	// is reached through.
	bool mutable_member;
	// 14.1p1: the region this declaration's template parameters were declared
	// in, which is what an instantiation of it substitutes arguments for.  Null
	// for a declaration no template-declaration parameterises, which every
	// declaration of the PA12 subset but a template is.
	Scope* template_parameters;
	// 14p1: what this declaration is a template *of*, when a
	// template-declaration parameterises it - the syntax the pattern was
	// written from, the region it stands in, and the parameters its head
	// declared.  14.7.1p1 makes an instantiation a reading of that pattern
	// against arguments bound to those parameters, so the declaration is the
	// natural owner of both and the walk that instantiates one asks nothing of
	// the tree it did not record here.  Null for every declaration no
	// template-declaration parameterises.
	TemplateInfo* templated;
	// 14.7p1: the template this declaration is a specialization of, and whether
	// the output has already written the declaration it stands for.  A
	// specialization is made by naming it rather than by a declaration the
	// program wrote, so it is bound to no name and is reached only from the
	// template-id or the call that asked for it.
	SemaEntity* primary;
	bool instantiated;
	// 14.7.1p1: whether this specialization was named where no complete type
	// was required - 7.1.3p1's typedef-name, 8.3.5p6's function declaration -
	// so that the declaration was made and the definition left to the first
	// use that requires one.  False again the moment that use arrives.
	bool declared_only = false;
	// 14.8.1p2: the template this declaration is `partial_of` with a leading
	// part of its argument list already written.  A template-id may leave the
	// trailing arguments out where a use deduces them, so what such a name
	// stands for is neither the template - the written arguments are part of
	// what a deduction starts from - nor a specialization, because no argument
	// list is complete yet.  It is a candidate like any other template and
	// makes a specialization of `partial_of` over the whole list once the use
	// has deduced the rest; `template_arguments` holds the part that was
	// written.  Null for every declaration a program or an instantiation made.
	SemaEntity* partial_of;
	// 14.7.2p8: whether an explicit instantiation definition asked this unit
	// for the definition of this function.  3.2p3 leaves an instantiated
	// definition to the use that requires it, and an explicit instantiation is
	// the one thing that requires it without writing a use - the definition is
	// still the program's rather than this unit's, so what this changes is when
	// the object file writes it and not how it binds.
	bool explicitly_instantiated;
	// 3.2p3 and 14.6.4.1p1: whether anything in this unit has asked this unit
	// for the definition of this function.  A definition 14.7.1p1 put aside
	// waits for that ask, and 14.6.4.1p1 gives a specialization a second point
	// of instantiation at the end of the unit - so a definition the program
	// writes *below* a use that already asked is one this unit owes, and it
	// joins what the end of the unit writes rather than waiting for an ask that
	// has already been made.
	bool definition_required;
	// 14.7.1p1: whether the definition this declaration makes was read by an
	// instantiation rather than written out by the program.  9.4.2p2's
	// definition of a static data member is the one that lays out its storage,
	// and one 14.7.3p1's `template<>` wrote for these arguments is this unit's
	// however little it reaches - while one a pattern was read again for
	// belongs to the program where the program reaches it.
	bool instantiated_definition;
	// 3.2p3 with 14.7.1p1: whether an object of this class has already asked
	// its static data members for the storage they stand in, which is one visit
	// per class however many objects of it a unit lays out.
	bool storage_demanded;
	// 10.1p3: the walk of a derivation that last reached this class.  Such a
	// walk is one visit per class and is made once per class completed, so a
	// derivation adding a base at every level makes one per level - and a
	// number on the class is what keeps each of those from building and
	// throwing away a table of its own.  Zero for a class no walk has reached.
	unsigned long long reached_at;
	// 14.2 and the ABI's `<template-args>`: the arguments that made this
	// specialization, as the interned list `TypeTable::type_list` keys a fact
	// about one by.  Zero - the empty list - for every declaration no
	// template-id and no deduction made.
	std::uint32_t template_arguments;
	// This entity among the run's, which is how a fact about it is keyed - the
	// same use `Scope::id` is put to for a region.
	std::uint32_t id;
	// 4.5p3: the type an unscoped enumeration is promoted to, which 7.2p5
	// makes the first of `int`, `unsigned int`, `long` and `unsigned long`
	// that represents every value the enumeration has.  It is known only once
	// the enumerators have been read, so it is held on the declaration rather
	// than in the type the declaration made.
	TypeId promotion;
	// 3.1p2: whether a declaration of an object also defines it.  An `extern`
	// declaration with no initializer declares the object without defining it,
	// which is the one thing that tells a use of a name in another translation
	// unit from the storage this one lays out.
	bool object_definition;
	// 7.5p1: whether the declaration was written inside a `"C"` linkage
	// specification, which is a fact about the declaration and not about its
	// type.  3.5p9 makes the name of an entity with C language linkage and
	// external linkage the name the object file carries, so only the analysis
	// can say what a backend must spell it as.
	bool c_linkage;
	// 3.5p3: whether this namespace-scope name has internal linkage, which a
	// `static` declaration gives it and which a `const` object has unless it
	// was declared `extern`.  It is what tells a symbol another translation
	// unit may reach from one that belongs to this one alone, and only the
	// declaration's own specifiers say it.
	bool internal_linkage;
	// 3.7.2p1: whether this variable has thread storage duration, which any
	// declaration of it written `thread_local` gives it.  It is one object per
	// thread rather than one per program, so 3.7.2p2 gives it storage of its
	// own kind, a wrapper the ABI names, and - where its initializer is not one
	// 3.6.2p2 settles - an initialization that runs once per thread rather than
	// once before `main`.  False for every other variable.
	bool thread_storage;
	// The name the PA12 dump spells this entity with: a namespace-scope
	// declaration is written with the named namespaces around it, and
	// everything else with the name it was declared with.  It is built where
	// the declaration is read, so a use of the name costs no walk.
	std::string dump_name;
	// The same name PA14's encoder reads, which is `dump_name` with
	// 7.3.1.1p1's `_GLOBAL__N_1` written where the dump writes nothing.  Empty
	// for every declaration outside an unnamed namespace, which is all but the
	// few a unit writes there, so the second spelling costs nothing where there
	// is nothing to say.
	std::string abi_name;
	// 9.8p1 and the ABI's `<local-name>`: the function whose body declares this
	// entity.  3.5p8 gives a local class no linkage, so the region around it
	// writes no name a program can spell and two functions may each declare one
	// of the same spelling - which is why the object file names it after the
	// function rather than after the regions the dump writes.  A member of a
	// local class carries the same function, because it is named through the
	// class.  Null for every declaration at namespace or class scope, which is
	// nearly all of them.
	SemaEntity* local_function;
	// Which occurrence of that name the function's body declares this is,
	// counted from zero, which is the ABI's discriminator.  A member of a local
	// class carries the class's own number rather than one of its own, because
	// the number belongs to the component the function declared.  For a type the
	// function's body left unnamed it is the ABI's `<unnamed-type-name>` number
	// instead, which is the one sequence the region's unnamed types are counted
	// in whatever their class-key.
	unsigned local_occurrence;
	// 9.8p1 and the ABI's `<unnamed-type-name>`: whether the entity the function
	// declared has no spelling at all, so that the number above is what names it
	// and no spelling of this unit's own may stand in the object file - two units
	// number the classes they declare from their own beginnings.
	bool local_unnamed;
	// 14.5.3p4 and 8.3.5p10: how many places the expansion of a function
	// parameter pack declared, held on the first of them because that is the
	// one the pack's own name is bound to - so `sizeof...(args)` and `args...`
	// read the run off the declaration the name already reaches.  Zero for
	// every declaration that is not one.
	unsigned pack_run;
	// 7.1.5p1: whether a declaration of this function wrote `constexpr`, which
	// makes 5.19p2 fold a call of it whose arguments are themselves constant.
	// The specifier is a fact of the function rather than of one declaration of
	// it, so it accumulates over them exactly as 7.1.2p2's `inline` does.
	bool constexpr_function;
	// 7.1.5p3: the function-definition this unit read for it, and the region
	// its own declarator opened for its parameters.  A fold reads the body in a
	// region of its own opened over that one - binding each parameter to what
	// its argument came to - so both are recorded where the definition is met
	// and nothing about a fold waits for the body to have been walked.  Null
	// for a declaration this unit defines nowhere, which no fold may call.
	const AstNode* constexpr_body;
	Scope* constexpr_region;
	// 14.5.3p4: the pack this binding stands for one element of, while one
	// reading of a pattern stands.  The pattern may name that pack *as a pack*
	// again - a nested expansion and 5.3.3p5's `sizeof...` both do, and neither
	// means this element - so the element binding carries the declaration the
	// run is still read off.  Null for every declaration that is not one.
	SemaEntity* pack_element_of;
};

// The name the object file encodes this declaration from, which is the dump's
// wherever 7.3.1.1p1 gave the region around it a name to write.
const std::string& abi_qualified_name(const SemaEntity& entity);


// One node of the PA12 semantic dump.
//
// PA11 writes a tree of scopes whose lines are all declarations, so a scope
// holds its lines and its child scopes apart.  PA12 writes resolved statements
// and expressions, which are ordered against each other and nest to any depth,
// so its node is the general one: a line, and the nodes written under it.
struct DumpNode
{
	std::string text;
	std::vector<DumpNode*> children;
	// What the line stands for, as typed facts rather than as the text it
	// spells them with.  A later assignment that lowers the resolved program
	// reads this and never parses `text`.
	SemaFact fact;
};

// 1.9p12 and 5p11: whether evaluating this expression is something the program
// can observe.  A name, a constant and the operators that only read them are
// not; anything that calls, assigns or constructs is, and so is any expression
// holding one.  5.3.3p1 leaves the operand of `sizeof` and `alignof`
// unevaluated, so what is written there is never observed.  It is one question
// about the resolved tree - the analysis asks it of a statement whose value
// nothing reads, and the lowering asks it of an operand it would otherwise have
// nothing to carry out of - so it is one answer rather than two.
bool observable_expression(const DumpNode& node);

// 9.3p2 and 3.2p4: whether a definition the program writes outside `scope` is
// one *this* translation unit holds, rather than one 3.2p3 leaves to the use
// that asks for it.  A member function defined outside its class is written
// here whatever names it here - which is what tells it from a body written in
// the class, one every unit that needs the definition writes for itself.
//
// The reference compiler reads the clause of a class a namespace declares and
// leaves a member class's definitions to the use, and the suite grades what it
// emits, so the question is asked of the region the class stands in.
bool holds_written_definitions(const Scope& scope);

// 12.8p31: whether this expression *creates* the object it is worth, rather
// than selecting one that something else created.  A temporary the program
// wrote and a call whose returned object the caller names each create theirs,
// which is what lets the object being initialized be that object with no copy
// standing between the two.  A conditional creates nothing: 5.16p3 gives it a
// result object each of its operands copy-initializes, so what a copy of it
// would carry is an object that already stands somewhere.  It is one question
// about the resolved tree - the analysis asks it to decide whether the copy is
// written at all, and the lowering asks it to decide where the initializer
// builds - so it is one answer rather than two that can disagree.
// `types` answers 3.10p9's cv-qualification of a class prvalue, which the cast
// carries on its own node and the object standing under it does not.
bool creates_its_object(const DumpNode& node, TypeTable& types);

// One line-oriented scope of the dump.
//
// The output is a tree of scopes whose shape is not the scope tree: a reopened
// namespace continues one node, an enumeration writes one per declaration, and
// an unscoped enumeration writes none at all.  Keeping the dump apart from the
// model lets each be what its own rules say, and lets a line be written as the
// declaration spells it while a use of the same type is written canonically.
struct DumpScope
{
	std::string header;
	std::vector<std::string> lines;
	std::vector<DumpScope*> children;
};

// A binding of one name in one region.
//
// 3.3.10p2 lets a class or enumeration name be hidden by a later declaration of
// the same name in the same region while staying reachable through an
// elaborated-type-specifier, so a binding holds both.
struct Binding
{
	Binding()
		: ordinary(nullptr)
		, tag(nullptr)
	{}

	SemaEntity* ordinary;
	SemaEntity* tag;
};

// One declarative region.
class Scope
{
public:
	Scope(ScopeKind scope_kind, Scope* enclosing, SemaEntity* scope_owner,
	      DumpScope* scope_dump, std::uint32_t scope_id);

	ScopeKind kind;
	Scope* parent;
	SemaEntity* owner;
	// The node a declaration written in this region writes its line to.
	DumpScope* dump;
	std::unordered_map<std::string, Binding> names;
	// The declarations of this region in order, which is what a class layout
	// and an anonymous union's member injection walk.
	std::vector<SemaEntity*> declarations;
	// The regions 7.3.4p2 makes the declarations of this one also appear in: a
	// using-directive's target, and an inline namespace member (7.3.1p8).
	std::vector<Scope*> nominated;
	// The regions that wrote those directives, which is the same relation read
	// the other way.
	std::vector<Scope*> nominated_by;
	// This region among the run's, which is how a fact about a pair of them is
	// keyed.
	std::uint32_t id;
	// 14.6.1p6: the nearest template-parameter region this one stands inside,
	// and the next such region outside that one through its own link.  A
	// declaration made anywhere in a template has to be asked whether it
	// redeclares one of that template's parameters, so the regions that can
	// answer are chained together as they are opened: the question then costs
	// the number of template heads standing over the declaration rather than
	// the block nesting it happens to be written at.
	Scope* template_head;
	// The `N::M::` the PA12 dump writes before a declaration of this region.
	// 7.3.1.1p1 leaves an unnamed namespace with no name to write, so its
	// members are spelled as the namespace around it spells its own.
	std::string prefix;
	// 3.5p4: whether 7.3.1.1p1's unnamed namespace stands around this region,
	// which gives every name declared here internal linkage and gives the
	// object file a region to name that the dump writes nothing for.  A region
	// inherits it from the one it is opened in, so it is one comparison per
	// declaration rather than a walk outwards.
	bool unnamed_region;
	// The same `N::M::` the object file's name is encoded from, which differs
	// from the dump's in one place: 7.3.1.1p1's unnamed namespace is a region
	// of its own that 3.5p4 keeps to this translation unit, and the ABI writes
	// it as the name `_GLOBAL__N_1` rather than as nothing - so two unnamed
	// namespaces in two units name two entities and not one.  Empty where it
	// is the same string as `prefix`, which every region outside an unnamed
	// namespace leaves it.
	std::string abi_prefix;
	// 9.8p1: the function whose body this region stands inside, which is what a
	// declaration made here is named from in the object file.  A region
	// inherits it from the one it was opened in, so settling it for a
	// declaration is one read rather than a walk outwards, and null says the
	// region stands at namespace or class scope.
	SemaEntity* local_function;
	// The occurrence number of the outermost entity between that function and
	// this region - the class a local class's members are named through.  Zero
	// for the function's own body and for every region outside one.
	unsigned local_occurrence;
	// Whether that outermost entity is one the function's body left unnamed, so
	// that the number above is the ABI's `<unnamed-type-name>` rather than its
	// discriminator.  False for every region outside a function.
	bool local_unnamed;
	// Scratch of one walk: the walk that reached this region, so that one with
	// several paths into one namespace holds it once.
	std::uint64_t visit;
	// 10.2p2: the regions searched after this one and before the one around it,
	// which for a class with a base-clause are the regions its bases declare,
	// in the order the base-specifier-list wrote them.  Every other region
	// leaves it empty, so a program with no inheritance pays nothing for the
	// question and allocates nothing.
	std::vector<Scope*> bases;
	// 14.6.2p3: whether any base-specifier of this class named a type that
	// depends on a template parameter.  3.4.1's lookup of a name written inside
	// this class is answered where the class was *defined*, and which class
	// such a base is only an argument list says - so such a base is left off an
	// unqualified lookup written in a member of this class, while 3.4.3's
	// qualified lookup and 3.4.5's class member access, which name the object
	// rather than read the definition, still walk it.  False for every class no
	// template head stands over.
	bool dependent_base;
	// 14.6.2p3 again, per base-specifier: the regions 3.4.1's search does look
	// in, which are the bases whose own specifier named a settled type.  It is
	// read only where `dependent_base` says one specifier did not, so a class
	// outside a template writes it and no search reads it - one pointer per
	// base and no walk.
	std::vector<Scope*> open_bases;
	// 7.3.3p1: the names a using-declaration written in this class brought a
	// member function of a base in under.  7.3.3p14's hiding is a question only
	// those names can answer yes to, and it is asked of the complete class -
	// one pass over the declarations each of them has, where 9.2p2 completes
	// the class - so a class that wrote no using-declaration pays nothing at
	// all and one that did pays the declarations it has rather than their
	// square.
	std::vector<std::string> using_names;
	// 12.9p1: whether a using-declaration written in this class named the
	// constructors of its direct base.  Which of them are inherited is settled
	// where 9.2p2 completes the class, because 12.9p1 leaves out the ones the
	// complete class declares itself - wherever among the members they stand.
	bool inheriting_constructors;
	// 11.3p6: the functions a friend declaration in this class first declared.
	// 7.3.1.2p3 makes each a member of the innermost enclosing namespace whose
	// name no ordinary lookup finds, and 3.4.2p2 makes it visible through the
	// class that declared it - so the class holds them and the namespace binds
	// nothing.  Empty for every region but a class one wrote a friend in.
	std::vector<SemaEntity*> friend_functions;
	// 11.3p6 again, read from the namespace: the head of the chain of
	// friend-declared functions of each name this region declares but does not
	// bind.  It is what a later namespace-scope declaration of the same
	// function finds, so the two declare one entity rather than two.
	std::unordered_map<std::string, SemaEntity*> hidden;

	// Every region a lookup written in which reaches this one's declarations,
	// which is the closure of 7.3.4p2 read from the region that declares rather
	// than from the region that asks.
	//
	// That way round is what makes it worth gathering: a name is declared in
	// one region and looked up from many, so one gathering answers every
	// lookup of it, while a closure per asking region would gather the same
	// namespaces again for each.  It is gathered only for a lookup a
	// using-directive has to answer, and it grows with the directives rather
	// than being gathered again: `searchers` holds the regions in the order the
	// walk reached them, `searcher_at` gives each its place, `expanded` says how
	// many of each one's directives the walk has followed, and `searchers_at`
	// how many of the run's it has seen.  A directive written afterwards
	// therefore costs the walk the edges it added and nothing for the rest.
	std::vector<Scope*> searchers;
	std::vector<std::uint32_t> expanded;
	std::unordered_map<Scope*, std::uint32_t> searcher_at;
	std::uint64_t searchers_at;

private:
	Scope(const Scope&);
	Scope& operator=(const Scope&);
};

// 3.4.3.1 and 3.4.3.2: a declaration of a namespace or of a class is named from
// outside it by the regions it is written in.  Both spellings of that name are
// settled together, so no caller can write one of them and leave the other
// saying what the region was called before an unnamed namespace was opened
// around it - and 3.5p4's linkage is settled with them.
void name_in_region(SemaEntity& entity, const Scope& scope,
                    const std::string& name);

// The entities, scopes and dump nodes of one translation unit.
class SemaModel
{
public:
	SemaModel();

	Scope& global() { return *global_; }
	const DumpScope& root() const { return *root_; }
	// The `translation-unit` node the PA12 dump is rooted at.
	DumpNode& unit() { return *unit_; }
	const DumpNode& unit() const { return *unit_; }

	// A region enclosed by `parent`, writing its lines to `dump`.
	Scope& open(ScopeKind kind, Scope& parent, SemaEntity* owner, DumpScope* dump);
	DumpScope& open_dump(DumpScope& parent, const std::string& header);
	// 14.6p8: a dump node under no parent, for a reading whose lines nothing
	// writes out and which outlives the call that started it.
	DumpScope& detached_dump();
	DumpNode& open_node(DumpNode& parent, const std::string& text);
	// Puts what `node` holds under a new line of its own, in place: `node` keeps
	// the place it already has among the lines its parent wrote, and what it
	// said moves into the one node it now holds.  That is what a conversion
	// written around an operand needs, and it costs neither a search of the
	// parent nor a second ordering of what it holds.
	DumpNode& wrap_node(DumpNode& node, const std::string& text);
	SemaEntity& create(SemaKind kind, const std::string& name, TypeId type);

	// 13.1: the declaration of one function name in one region whose parameter
	// type list is `signature`, or nothing when that region has none.  `head` is
	// the entity the name is bound to, so a redeclaration is found rather than
	// searched for and declaring the nth overload of a name costs what declaring
	// the first does.
	SemaEntity* overload_of(const SemaEntity& head, std::uint32_t signature) const;
	void hold_overload(const SemaEntity& head, std::uint32_t signature,
	                   SemaEntity& entity);
	// Forgets that pairing, which 7.3.1.2p3 asks for when a declaration leaves
	// the chain a friend declaration put it in for the one its name binds.
	void drop_overload(const SemaEntity& head, std::uint32_t signature);

	// 14.7.1p1: the specialization of `primary` for the template-argument list
	// `arguments`, or nothing when it has not been made yet.  Naming the same
	// specialization twice names one declaration, so the second naming is a
	// probe rather than a second substitution.
	SemaEntity* specialization_of(const SemaEntity& primary,
	                              std::uint32_t arguments) const;
	void hold_specialization(const SemaEntity& primary, std::uint32_t arguments,
	                         SemaEntity& entity);

	// 5.19p2 with 7.1.5p2: what a call of the constexpr function `callee` came
	// to, as the `TypeKind::Value` entry the fold interned, or `kNoType` where
	// no fold of it has been made.  It is keyed by the declaration and the
	// interned list holding what its object and its arguments came to - the
	// same pair 13.1 tells two declarations of one name apart by and 14.7.1p1
	// tells two specializations apart by - because the answer is a fact of that
	// pair and of nothing else.  A chain n deep therefore costs n readings of a
	// body however many times each link of it is written.
	TypeId folded_call(const SemaEntity& callee, std::uint32_t arguments) const;
	void hold_folded_call(const SemaEntity& callee, std::uint32_t arguments,
	                      TypeId value);
	// 7.1.5p5: how many folds of a body stand over the one being read, which is
	// what bounds a constexpr function that calls itself - or a chain of them
	// longer than the machine stack - to a refusal rather than a crash.
	unsigned& folding_depth() { return folding_depth_; }
	// 10.1p3: a number no earlier walk of a derivation used, which is what
	// marks the classes this one reaches on `SemaEntity::reached_at`.
	unsigned long long next_reach() { return ++reach_; }

	// The identifier a user-defined type is interned under, which is the
	// entity that declared it.
	std::uint32_t type_entity_id() { return ++type_entities_; }

	// The binding of `name` in `where` alone.
	SemaEntity* find(const Scope& where, const std::string& name,
	                 LookupKind filter) const;
	// Binds `name` in `where` to `entity`, which a using-declaration does to an
	// entity declared elsewhere.
	void bind(Scope& where, const std::string& name, SemaEntity& entity);
	// Records `entity` as declared in `where`, in declaration order.
	void declare_in(Scope& where, SemaEntity& entity);

	// 3.4.1: the innermost declaration of `name` visible from `where`.
	//
	// 3.4p2 lets a lookup that finds a function name associate more than one
	// declaration with it, and 7.3.4p3 lets it reach the declarations of several
	// namespaces at once.  `found`, when given, takes the chain each region that
	// contributed heads, in the order the search reached them; the declaration
	// returned is the first of them.  A lookup that asks for no set is one whose
	// caller needs a single declaration, and two regions are the error 3.4p1
	// makes them for it.
	SemaEntity* lookup(Scope& from, const std::string& name, LookupKind filter,
	                   std::vector<SemaEntity*>* found = nullptr);
	// 3.4.3: the declaration of `name` that `in` and everything its
	// declarations appear in have.
	SemaEntity* lookup_in(Scope& in, const std::string& name, LookupKind filter,
	                      std::vector<SemaEntity*>* found = nullptr);
	// A set of declarations for one use of a name to hold.  It belongs to the
	// lookup that found it rather than to any region, because a declaration's own
	// chain is a fact about the region that declared it and no lookup may relink
	// it.
	std::vector<SemaEntity*>& open_overloads();

	// A using-directive in `where` naming `space`, which an inline namespace
	// and an unnamed one also write for themselves.
	void nominate(Scope& where, Scope& space);

	// The class or enumeration a type is: the entity whose declaration made
	// it, which is how a type reached through a typedef-name finds the region
	// its members are declared in.
	void own_type(TypeId type, SemaEntity& entity);
	SemaEntity* type_owner(TypeId type) const;

	// The region a name written before `::` looks into, following a
	// typedef-name or a namespace alias to what it names.
	Scope* region_of(const SemaEntity& entity) const;

	// 11.3p1: `granting` gave `friendly` - a function or a class - the reach
	// its own members have.  The relation is held here rather than on either
	// entity because a program with no friend declaration should pay nothing
	// for it: `has_friends` is what every access check asks first.
	void befriend(const SemaEntity& granting, const SemaEntity& friendly);
	bool befriended(const SemaEntity& granting,
	                const SemaEntity& friendly) const;
	bool has_friends() const { return !friendships_.empty(); }

private:
	// The regions that bind `name`, or null when no region does.
	const std::vector<Scope*>* declarers(const std::string& name) const;
	// 3.4: the one declaration of `name` there is, when the search reaches the
	// region that made it.  The search runs from `from` out to the region
	// before `stop`, which is `from` alone for a qualified lookup.
	SemaEntity* lookup_unique(Scope& from, const Scope* stop,
	                          const std::string& name, LookupKind filter,
	                          Scope& declarer);
	// 10.2p2: whether `declarer` is one of the regions a class derives from, or
	// one of theirs in turn, and the declaration of `name` such a search finds.
	bool declares_below(const std::vector<Scope*>& bases, const Scope& declarer);
	SemaEntity* find_inherited(const std::vector<Scope*>& bases,
	                           const std::string& name, LookupKind filter);
	// 7.3.4p2: whether the declarations of `declaring` appear in `in`, which
	// they do when it is `in` itself or the using-directives written in `in`
	// reach it.
	bool reaches(Scope& in, Scope& declaring);
	// The regions whose declarations appear in `in`, gathered into `reached_`
	// when there are at most `budget` of them.  False when there are more,
	// which is when asking each region that declares the name is the cheaper
	// question.
	bool walk_reached(Scope& in, std::size_t budget);
	// The declaration of `name` in every region `in`'s declarations appear in,
	// where `regions` - what `declarers` answered - holds more than one, so
	// that 3.4p1 has to ask about each of them.
	SemaEntity* search_declarers(Scope& in, const std::string& name,
	                             LookupKind filter,
	                             const std::vector<Scope*>& regions,
	                             std::vector<SemaEntity*>* found);
	// Gathers `declaring.searchers`, following only the using-directives
	// written since the last gathering.
	void gather_searchers(Scope& declaring);
	// 7.3.4p2: hold `declaring` until the walk outward reaches the level its
	// declarations appear at, unless it is already being held.
	void take_pending(Scope& declaring);
	// 3.4p1 and 3.4p2: the one entity a lookup that reached two declarations
	// found, the set it found when both of them are functions, or the error that
	// they are two entities.
	static SemaEntity* merge_found(SemaEntity* found, SemaEntity* again,
	                               std::vector<SemaEntity*>* set);

	// 9.8p1: the function a declaration recorded in `where` is named from, and
	// its place among what that function declares.
	void settle_local_name(Scope& where, SemaEntity& entity);

public:
	// 9.8p1 and the ABI's `<unnamed-type-name>`: the same two facts for a class
	// or enumeration a function's body declared under no name at all.  It is
	// settled where the declaration is read rather than in `declare_in`, because
	// a declaration with no name is bound in no region.
	void settle_unnamed_local_name(Scope& where, SemaEntity& entity);

private:

	std::deque<Scope> scopes_;
	std::deque<SemaEntity> entities_;
	std::deque<DumpScope> dumps_;
	std::deque<DumpNode> nodes_;
	std::deque<std::vector<SemaEntity*> > overload_sets_;
	Scope* global_;
	DumpScope* root_;
	DumpNode* unit_;
	std::uint32_t type_entities_;
	std::unordered_map<TypeId, SemaEntity*> type_owners_;
	// The declarations of each overloaded name, keyed by the entity the name is
	// bound to and the parameter type list 13.1 tells two declarations apart by.
	std::unordered_map<std::uint64_t, SemaEntity*> overloads_;
	// The specializations made so far, keyed by the template and the
	// template-argument list they were made from.
	std::unordered_map<std::uint64_t, SemaEntity*> specializations_;
	// The calls of a constexpr function folded so far, keyed the same way: the
	// declaration and the interned list of what its object and its arguments
	// came to.
	std::unordered_map<std::uint64_t, TypeId> folded_calls_;
	unsigned folding_depth_;
	unsigned long long reach_;
	// 11.3p1: the friendships granted so far, as the pair of entity
	// identifiers in one word, so asking whether one class befriended one
	// declaration is a probe.
	std::unordered_set<std::uint64_t> friendships_;
	// The regions that bind each name, each once, in the order they first bound
	// it.
	//
	// 3.4p1 makes a lookup that reaches two declarations of a name in one
	// declarative region an error, so a lookup through using-directives has to
	// know where else the name is declared.  Answering that from the name
	// rather than by walking every namespace the directives reach is what keeps
	// a unit with many of them from costing one walk per lookup: a name
	// declared nowhere is answered without a walk, a name one region declares
	// is answered by asking whether the lookup reaches that region, and a name
	// several declare is answered from whichever is smaller, the regions that
	// declare it or the regions the lookup reaches.
	std::unordered_map<std::string, std::vector<Scope*> > declarers_;
	// 9.8p1 and the ABI's discriminator: how many types of each name each
	// function's body has already declared, keyed by the function and the name.
	// The count is what tells the second `struct L` a function writes from the
	// first, and it is kept here rather than on the function because nearly
	// every function declares no type at all - the map grows only for the ones
	// that do.
	std::unordered_map<std::string, unsigned> local_occurrences_;
	// The ABI's `<unnamed-type-name>` number: how many types each function's
	// body has already declared under no name, keyed by the function.  The
	// region counts them in one sequence whatever their class-key, so a class
	// and an enumeration share it.
	std::unordered_map<std::uint32_t, unsigned> local_unnamed_;
	// The using-directives written so far, as the pair of region identifiers in
	// one word, so writing the same one twice costs a probe rather than a scan.
	std::unordered_set<std::uint64_t> nominations_;
	// The namespace each of those directives named, in order, which is how a
	// gathered set of searchers learns which of its regions were nominated
	// since it last looked.
	std::vector<Scope*> nominees_;
	// Scratch of one gathering: the places left to expand.
	std::vector<std::uint32_t> pending_;
	// Scratch of one bounded walk, kept between walks so that a lookup in a
	// region with no using-directive allocates nothing.
	std::vector<Scope*> reached_;
	// Scratch of one unqualified lookup: the regions a using-directive already
	// passed reaches and which the walk outward has not yet arrived at the
	// level 7.3.4p2 puts them at - the nearest region enclosing both the
	// directive and the region declaring.  A level that wrote no directive and
	// holds nothing pending leaves this empty, so a lookup answered where it
	// stands allocates nothing and walks nothing.
	std::vector<Scope*> placed_;
	std::uint64_t visit_;
};

// 9.2p13: whether this declaration gives the class `scope` declares an object
// of its own, which is what the layout, the initialization of a subobject and
// the end of one's lifetime each walk the declarations for.  9.4p2 makes a
// static data member a variable rather than part of an object; 9.5p1 records an
// anonymous union's members in the region around it as well as in its own; and
// 7.3.3p1's using-declaration declares a member of a base class, which that
// class already laid out.  None of the three is storage this class gives.
bool declares_subobject(const SemaEntity& member, const Scope& scope);

// Whether `outer` is `inner` or a region `inner` is written in, which is what
// 7.3.4p2's "nearest enclosing namespace" is measured with.
bool encloses(const Scope& outer, const Scope& inner);
// 10.2p2 and 14.6.2p3: the regions 3.4.1's search looks in after `scope`, which
// are its base classes' except where a base-specifier named a dependent type.
const std::vector<Scope*>& unqualified_bases(const Scope& scope);
// True when `entity` names a type: a class, an enumeration, a typedef-name or
// a template type parameter (3.4.4p2, 7.1.3p1, 14.1p3).
bool names_a_type(const SemaEntity& entity);
// True when `entity` names a namespace, which 7.3.1p3 and 7.3.2 ask for.
bool names_a_space(const SemaEntity& entity);

// 14.1p1 and 3.3.2p6: the region a declaration written in `scope` belongs to.
// The region a template's parameters are declared in encloses only the
// declaration they parameterise, and 3.3.7p1's holds the places one declarator
// wrote, so a name declared in either belongs to the region around it - which
// is where a use of the template looks for it and where a class an
// elaborated-type-specifier in a parameter-clause first declares stands.
Scope& declaring_region(Scope& scope);

// 14.1p1 and 3.4.1p8: where a template head stands while the declaration it
// parameterises is read through a qualified declarator-id.
//
// The declaration belongs to the region its name reaches, so that is the region
// the head's own is opened inside for as long as the declarator and the body
// are read: a name they write is looked up in the head first - 14.1p2 made
// these parameters this declaration's own - and in the region the name reaches
// after it, exactly as it would be for a declaration written there.  Nothing
// the head binds outlives the declaration, so it goes back where it was
// written afterwards.  A null head stands nowhere, which is every declaration
// no template-declaration parameterises.
class StandingIn
{
public:
	StandingIn(Scope* head, Scope& region)
		: head_(head)
		, parent_(head == nullptr ? nullptr : head->parent)
	{
		if (head_ != nullptr)
		{
			head_->parent = &region;
		}
	}

	~StandingIn()
	{
		if (head_ != nullptr)
		{
			head_->parent = parent_;
		}
	}

private:
	StandingIn(const StandingIn&);
	StandingIn& operator=(const StandingIn&);

	Scope* const head_;
	Scope* const parent_;
};

// Writes `scope` and everything under it, indenting two spaces per level.
void write_dump(std::ostream& out, const DumpScope& scope, unsigned depth);
void write_nodes(std::ostream& out, const DumpNode& node, unsigned depth);
