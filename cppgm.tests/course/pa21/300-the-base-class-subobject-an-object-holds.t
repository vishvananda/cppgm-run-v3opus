// VALIDATION: compile-pass
// N3485 focus: 10 [class.derived], 12.6.2 [class.base.init], 5.19 [expr.const]
//
// 10p1 makes a base class subobject part of the object, so a constant of class
// type holds one entry for it beside the entries its members have.  12.6.2p10
// says where: the base class subobjects stand before the members, in
// base-specifier order, which is the order 9.2p13 lays the storage out in - so
// one index reaches a subobject for the member access, for the address, and for
// the image the definition holds.
//
// Every reading that already existed asks that one list.  A mem-initializer
// names a base by its *type*, and 14.5.3p4 may write one as a pattern standing
// for a run of them; 12.6.2p4 default-initializes a base no mem-initializer
// names; 10.2 finds a member in a base and reaches it through the entry the
// base subobject stands at; 4.10p3 converts an object of a derived class to one
// of a base by taking that entry; 12.8p15's implicitly-defined copy constructor
// copies it as it stands; and 8.5.3p4 binds a reference to the subobject rather
// than to a copy of it, which 5.2.9p11's cast then names the whole object from
// again.  9.3.2p1's `this` is what a member function reads all of that through,
// and 9.3.1p3 makes a call written with no object expression one on it.
//
// 3.6.2p2 reads the same list one layer down: the image of an object with
// static storage duration holds each base class subobject at the byte 9.2p13
// laid it out at, and a mem-initializer of a base that names the enclosing
// constructor's own parameter is worth what the call one level up passed.

struct left_part
{
  int left_value;

  constexpr left_part(int value) : left_value(value)
  {
  }
};

struct right_part
{
  int right_value;

  constexpr right_part(int value) : right_value(value)
  {
  }
};

template<class... Parts>
struct joined : Parts...
{
  constexpr joined(Parts... value) : Parts(value)...
  {
  }
};

struct tagged_base
{
  int tag = 7;

  constexpr int twice() const
  {
    return tag + tag;
  }
};

struct tagged : tagged_base
{
  int extra;

  constexpr tagged(int value) : extra(value)
  {
  }

  // 9.3.2p1 and 10.2: `this` names the whole object, and the member the lookup
  // finds in the base is the entry that base subobject holds.
  constexpr int total() const
  {
    return this->tag + extra + twice();
  }

  // 8.5.3p4 with 4.10p3: the reference names the base class subobject of the
  // object this call was written on and no copy of it.
  constexpr tagged_base const &as_base() const
  {
    return *this;
  }
};

constexpr joined<left_part, right_part> both(left_part(3), right_part(5));
constexpr tagged one(11);
constexpr tagged_base sliced = one;

static_assert(both.left_value == 3, "");
static_assert(both.right_value == 5, "");
static_assert(one.tag == 7, "");
static_assert(one.total() == 32, "");
static_assert(one.as_base().twice() == 14, "");
static_assert(sliced.tag == 7, "");
static_assert(static_cast<tagged_base const &>(one).tag == 7, "");

// 5.2.9p11: the cast down names the object the base class subobject is part of,
// which is the one the reference was bound inside.
constexpr int down(tagged_base const &part)
{
  return static_cast<tagged const &>(part).extra;
}

static_assert(down(one) == 11, "");

int main()
{
  return one.total() - 32;
}
