#pragma once

#include <utility>
#include <vector>

#include "sema_declaration.h"
#include "type_model.h"

class SemaAnalyzer;
struct BaseClass;
struct Scope;
struct SemaEntity;

// 9.2p13 and the course ABI: the storage one class definition is laid out over,
// as the walk that gives each of its subobjects a place in it.
//
// The ABI allocates them in one order - the vpointer 10.3p1 asks for, the base
// class subobjects 10.1p4 does not share in the order the base-specifier-list
// wrote them, the non-static data members in declaration order, and last the
// shared base subobjects, because where one of those stands is a fact of the
// complete object and not of the class that named it.  Each placement asks the
// same three questions of the storage so far - how far it reaches, how strictly
// it must be aligned, and where the empty class subobjects already in it stand -
// and leaves the same four answers about carrying an object of the class by
// value.  That running state is what this holds, so a placement is a step of one
// walk rather than a parameter list handed between free functions, and the walk
// is made once per class where 9.2p2 completes it.
class ClassLayout
{
public:
	ClassLayout(SemaAnalyzer& analyzer, SemaEntity& entity, Scope& scope,
	            bool is_union, unsigned long long packed);

	// 9.2p2 and the course ABI: the whole walk, with `requested` the alignment
	// 7.6.2 asked for or zero for none.  It ends by completing the class, so
	// every later use of a member, a base or an object of it is a read.
	void run(unsigned long long requested);

private:
	// 10p1: one base class subobject given its place in this storage.
	void place_base(BaseClass& link);
	// 9.2p13 and 9.6p2: one non-static data member given its place, or a share
	// of the storage unit the bit-fields before it are filling.
	void place_member(SemaEntity& member);
	// 9.6p2: the share of a storage unit one bit-field takes, from the byte the
	// walk has reached and the unit it is filling.
	void place_bit_field(SemaEntity& member);
	// 9.2p2 and 1.8p5: the class completed with what the walk came to.
	void finish(unsigned long long requested);
	// The ABI: where the empty class subobjects of an object of `type` standing
	// at `at` are, appended to `holes`, and whether putting one there would put
	// a subobject of some class where a subobject of that class already stands.
	// The ABI gives an empty base subobject offset zero and then forbids a
	// second subobject of its class from standing there, and an empty subobject
	// takes no storage to push the next one along - so the offsets alone do not
	// say it and the classes standing at them have to be carried.
	void place_empty_subobjects(
		TypeId type, unsigned long long at,
		std::vector<std::pair<TypeId, unsigned long long> >& holes);
	bool collides_with_empty(TypeId type, unsigned long long at);
	// The ABI again, asked of each subobject as it is placed: what carrying an
	// object of this class by value comes to, which is what every subobject of
	// it comes to.
	void note_carried(TypeId copied, TypeId storage);

	ClassLayout(const ClassLayout&);
	ClassLayout& operator=(const ClassLayout&);

	SemaAnalyzer& analyzer_;
	SemaEntity& entity_;
	Scope& scope_;
	bool is_union_;
	// 16.6's cap in force where the definition of the class ends, which is where
	// 9.2p2 completes it and where this settles the layout - so a directive
	// written between two of its members reaches every one of them.
	unsigned long long packed_;
	// 9.2p13: how far into the object the walk has reached, and the strictest
	// alignment anything placed so far needs.
	unsigned long long size_;
	unsigned long long align_;
	// 9p6: whether an object of this class holds nothing at all.
	bool empty_;
	// 12.8p25, 12.8p12, 8.5p8 and 5.2.2p4: the four facts about the storage the
	// ABI's boundary reads, each of them the same answer over every subobject.
	bool trivially_copied_;
	bool subobject_bytes_;
	bool zeroed_storage_;
	bool floating_storage_;
	// 9.5p2: how many members of this union wrote a brace-or-equal-initializer,
	// which is at most one.  Zero for every class that is not a union.
	unsigned default_initializers_;
	// 9.6p2: the storage unit the bit-fields declared so far are filling.  A
	// class with no bit-field never opens one.
	BitFieldUnit unit_;
	// The ABI: where the empty class subobjects of an object of this class
	// stand, filled in as they are placed.  It is the class's own list, because
	// the classes that go on to hold an object of this one read it from there.
	std::vector<std::pair<TypeId, unsigned long long> >& holes_;
};
