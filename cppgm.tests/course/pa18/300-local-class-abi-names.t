// 9.8p1 and the ABI's <local-name>: a class a function body declares is named
// after that function, so two of one spelling in one unit are two entities and
// two vtables.  The shapes below are the ones the encoding has to tell apart:
// the same name twice in one function, a member function as the context with
// its cv-qualifier, `main`, and a function whose own name registers a
// substitution the class's members then reuse.
struct base
{
  virtual int value() const { return 0; }
};

int twice()
{
  int total = 0;
  {
    struct same : base { int value() const { return 1; } } object;
    base & polymorphic = object;
    total += polymorphic.value();
  }
  {
    struct same : base { int value() const { return 2; } } object;
    base & polymorphic = object;
    total += polymorphic.value();
  }
  return total;
}

struct holder
{
  int member() const
  {
    struct same : base { int value() const { return 8; } } object;
    base & polymorphic = object;
    return polymorphic.value();
  }
};

namespace space {

struct thing
{
  int held;
};

int with_argument(thing given)
{
  struct same : base
  {
    same(thing from) : kept(from) {}
    int value() const { return kept.held; }
    thing kept;
  } object(given);
  base & polymorphic = object;
  return polymorphic.value();
}

}

int main()
{
  struct same : base { int value() const { return 16; } } object;
  base & polymorphic = object;
  space::thing given;
  given.held = 32;
  return twice() == 3 && holder().member() == 8 &&
         space::with_argument(given) == 32 && polymorphic.value() == 16
    ? 0
    : 1;
}
