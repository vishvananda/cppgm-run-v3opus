// VALIDATION: compile-pass
// N3485 focus: 14.5.4 [temp.friend], 11.3 [class.friend], 3.4.2 [basic.lookup.argdep]
// 11.3p6 declares a friend function template into the namespace around the
// class and binds its name nowhere, so 3.4.2p2 is the only lookup that reaches
// it.  14.7.1p1's instantiation reads the same syntax again in a region that
// binds the arguments and stands under no class at all, which is why the class
// that granted the declaration is a fact of the template rather than of the
// regions the reading stands in.

struct counter
{
  int n;
};

struct step
{
  int by;

  template<class T>
  friend int advance(T& target, const step& s)
  {
    target.n = target.n + s.by;
    return target.n;
  }
};

int main()
{
  counter c;
  step s;
  c.n = 4;
  s.by = 5;
  return advance(c, s) == 9 && c.n == 9 ? 0 : 1;
}
