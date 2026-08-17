// VALIDATION: compile-pass
// N3485 focus: 5.1.1 [expr.prim.general], 5.3.3 [expr.sizeof],
//              9.3.1 [class.mfct.non-static]
//
// 9.3.1p3 makes an id-expression that names a non-static data member with no
// object expression a member of the object `this` points to, which is why such
// a name written where no member function stands has nothing to be a member of.
//
// 5.1.1p13 admits it in three places, and only two of them have an object.  A
// class member access brings one of its own; 5.3.1p3's `&` forms a pointer to
// member, which names the member and not an object; and the third bullet is an
// *unevaluated operand*, where the id-expression denotes the member itself and
// there is no object at all.  5.3.3p1 leaves `sizeof`'s operand unevaluated, so
// `sizeof(S::keys)` is the size of the type `keys` was declared with, asked
// wherever the program cares to ask it - at namespace scope, in the
// brace-or-equal-initializer of a static data member of the very class, and
// inside a template whose own arguments say nothing about the member.
//
// The rule is about the operand and not about the member: a use of the same
// name that *is* evaluated still has no object, and stays the error 5.1.1p13
// makes it.

struct scalars
{
  int first;
  double second;
  unsigned int keys[8];
};

// 5.1.1p13's third bullet at namespace scope, where no member function stands.
constexpr unsigned long by_name = sizeof(scalars::keys);
static_assert(by_name == 32, "the member's own declared type");
static_assert(sizeof(scalars::first) == 4, "a scalar member");
static_assert(sizeof(scalars::second) == 8, "of whatever type it was declared");

struct reader
{
  unsigned int keys[8];
  int single;

  // 9.2p2: the initializer of a static data member is a complete-class
  // context and no member function, so `this` is not there either.
  static constexpr unsigned long qualified = sizeof(reader::keys);
  static constexpr unsigned long unqualified = sizeof(keys);
  static constexpr unsigned long scalar = sizeof(single);
};

static_assert(reader::qualified == 32, "written inside the class, qualified");
static_assert(reader::unqualified == 32, "and unqualified");
static_assert(reader::scalar == 4, "over a member of scalar type");

struct outer
{
  struct inner
  {
    double held;
  };
};

static_assert(sizeof(outer::inner::held) == 8, "a member of a nested class");

// 14.7.1p1: the same sentence inside a class template, whose own arguments say
// nothing about the member being measured.
template<unsigned long Rounds>
struct rounds
{
  unsigned int keysetup_[8];
  static constexpr unsigned long state_size = sizeof(rounds::keysetup_);
  static constexpr unsigned long rounds_taken = Rounds;
};

static_assert(rounds<20>::state_size == sizeof(unsigned int[8]), "the size");
static_assert(rounds<12>::state_size == 32, "for any argument list");
static_assert(rounds<20>::rounds_taken == 20, "and the argument is unchanged");

// 9.3.1p3 where there *is* an object: the same member, read off `this`, and
// read off an object expression written out.
struct held
{
  int value;
  constexpr held() : value(7) {}
  constexpr int twice() const { return value * 2; }
};

constexpr held one;
static_assert(one.twice() == 14, "a member read through `this`");
static_assert(one.value == 7, "and through an object expression");
static_assert(sizeof(held::value) == 4, "beside the unevaluated reading");

int main()
{
  return by_name + reader::qualified + rounds<20>::state_size == 96 ? 0 : 1;
}
