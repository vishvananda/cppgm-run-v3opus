// N3485 focus: 9.5p1 [class.union] a member of an anonymous union declared in a
// base class is reached through the object 9.5p1 declared *and* through 10.2's
// base class subobject, so the access holds both steps.
struct Base
{
  int lead;

  union
  {
    int value;
    long wide;
  };

  Base()
      : lead(1)
  {
  }

  int held() const
  {
    return value;
  }
};

struct Derived : Base
{
  Derived()
  {
  }

  int reached() const
  {
    return value;
  }
};

int main()
{
  Derived derived;
  derived.value = 4;
  return derived.held() + derived.reached() + derived.value - 12;
}
