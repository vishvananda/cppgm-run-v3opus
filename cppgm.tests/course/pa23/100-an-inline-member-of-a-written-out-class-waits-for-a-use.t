// VALIDATION: compile-pass
// 14.7.3p5 says the definition of an explicitly specialized class is unrelated
// to the one an instantiation would have made and its members are defined the
// way a normal class's are.  That answers which definition a member *is* - this
// unit's own, and no reading of a pattern - so `int box<float>::plain()` below
// stands in the output whether or not anything calls it.
//
// It answers nothing about which unit writes an `inline` one.  That is 3.2p4's
// question about the source, and the class a template-id has to name is one the
// program reaches only through its argument list, so an `inline` member of it
// waits for a use exactly as a body written inside the class does - which is
// why no `@box_float___lazy` stands below.  12.1 and 12.4 are the exception a
// constructor written out of class makes: its body stands under both of the
// ABI's entry points, so the unit that wrote it owes both names where it
// stands and has no use to wait for.

template<class T>
struct box
{
  int plain()
  {
    return 1;
  }

  int lazy()
  {
    return 1;
  }
};

template<>
struct box<float>
{
  box(int seed);
  int plain();
  int lazy();

  int held;
};

int box<float>::plain()
{
  return 3;
}

inline int box<float>::lazy()
{
  return 4;
}

inline box<float>::box(int seed)
  : held(seed)
{
}

int main()
{
  box<int> pattern;
  return pattern.plain() + pattern.lazy() == 2 ? 0 : 1;
}
