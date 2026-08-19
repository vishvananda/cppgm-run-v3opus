// 12.9p2 says what a constructor's characteristics are - the
// template-parameter-list, the parameter-type-list, absence or presence of
// `explicit` and absence or presence of `constexpr` - and 12.9p3 declares the
// inherited one with the same four.  So a base constructor template the program
// wrote `constexpr` on leaves this class an object 5.19 builds, and what the
// fold reads is 12.9p8: the base subobject built by the base's own constructor
// out of the arguments this one was called with.

struct base
{
  int value;

  template <class U>
  constexpr base(U u)
    : value(int(u) + 1)
  {
  }
};

struct kept : base
{
  using base::base;
};

constexpr kept one(3);
static_assert(one.value == 4, "an inherited constructor is constexpr where the base's is");

// The same declaration reached through a second argument type, where the
// deduction is the derived class's and the definition the standard's.
static_assert(kept('A').value == 66, "and it deduces at the class the using-declaration is written in");

int main()
{
  char sized[kept('A').value - 61];
  return one.value == 4 && sizeof(sized) == 5 ? 0 : 1;
}
