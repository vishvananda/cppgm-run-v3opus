#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "type_model.h"

struct SemaEntity;

// 5.19p2: the object an address constant expression designates.
//
// 3.9.2p1's pointer holds the address of an object, and what a constant
// expression knows about one is never a number: it is *which* object, and which
// subobject of it.  So an address here is the declaration whose storage it is -
// or 2.14.5p8's string literal, which no declaration named - together with the
// path of subobject indices down to the object designated.  One index per
// level, 9.2p13's members and 8.3.4p6's elements alike, which is exactly the
// order the interned list a `SemaConstant` of class or array type holds its
// subobjects in: the path and that list are read down together.
//
// 5.7p4 gives a pointer past the last element of an array a value of its own,
// so the last index of a path may equal the bound of the array it indexes, and
// `past_end` says the same thing about an object that is no array element -
// that clause treats such an object as an array of one.
//
// `automatic` is 3.7.3's storage duration read as 5.19p2 asks about it: an
// address constant expression designates an object with static storage
// duration, and one this evaluation itself gave storage to - a place a call
// filled, an object a body declared, 12.2p1's temporary a reference bound to -
// is not one.  It travels with the address rather than being asked of the base,
// because a subobject of such an object is one too.
struct ConstantAddress
{
	ConstantAddress()
		: object(nullptr)
		, temporary(kNoType)
		, automatic(false)
		, past_end(false)
	{}

	// The declaration whose storage this address is into, null for a literal
	// and for a temporary.
	SemaEntity* object;
	// 2.14.5p8's string-literal object, as the spelling phase 6 left.
	std::string literal;
	// 12.2p1's temporary, which a reference bound to a prvalue is bound to.  It
	// is identified by the interned entry of the value it holds, because that is
	// the whole of what 5.19 knows about such an object - and because a fold
	// keyed on what its arguments came to would otherwise never find the answer
	// it already has, one temporary being made per call.
	TypeId temporary;
	// Which subobject of it, one index per level down.
	std::vector<unsigned long long> path;
	bool automatic;
	bool past_end;
};

// The pool the identifiers a `SemaConstant` of pointer type carries index into.
//
// A constant is interned by its type and its bits, so an address has to be one
// number for two namings of it to be one constant: `&x` written twice is one
// identifier, 5.10p1's `==` over two of them is a comparison of identifiers and
// no walk at all, and a fold keyed on what its arguments came to tells a call
// on one object from a call on another.  Identifier zero is 4.10p1's null
// pointer value, which designates no object.
class AddressTable
{
public:
	AddressTable();

	std::uint32_t intern(const ConstantAddress& address);
	const ConstantAddress& at(std::uint32_t id) const;

private:
	AddressTable(const AddressTable&);
	AddressTable& operator=(const AddressTable&);

	// The whole of an address, in the order two of them are ordered by.
	typedef std::pair<std::pair<const SemaEntity*, std::string>,
	                  std::pair<std::vector<unsigned long long>,
	                            std::pair<TypeId, std::pair<bool, bool> > > > Key;
	static Key key_of(const ConstantAddress& address);

	std::vector<ConstantAddress> held_;
	std::map<Key, std::uint32_t> ids_;
};
