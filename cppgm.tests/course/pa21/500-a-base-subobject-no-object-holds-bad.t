// VALIDATION: compile-fail
// N3485 focus: 5.2.9 [expr.static.cast], 5.19 [expr.const], 10 [class.derived]
//
// 5.2.9p11 lets a `static_cast` name a derived class from a reference to its
// base, and says the behaviour is undefined where the object the reference
// designates is not a base class subobject of an object of that class.  5.19p2
// leaves a constant expression no undefined behaviour at all, so the cast is
// one this reading refuses rather than one it takes a step it cannot take.
//
// `alone` is an object of the base class and no subobject of anything, so the
// path a conversion down would walk back up is not there: the entry a
// derivation names is the one an object of `derived_part` holds, and this
// object holds none.  Nothing here reads a member that is missing - the
// refusal is at the binding, before `extra` is named at all.

struct base_part
{
  int tag;

  constexpr base_part(int value) : tag(value)
  {
  }
};

struct derived_part : base_part
{
  int extra;

  constexpr derived_part(int value) : base_part(value), extra(value + 1)
  {
  }
};

constexpr base_part alone(1);

constexpr int down(base_part const &part)
{
  return static_cast<derived_part const &>(part).extra;
}

static_assert(down(alone) == 2, "");

int main()
{
  return 0;
}
