// VALIDATION: compile-pass
// 14.5.3p4 says an expansion of a function parameter pack declares one place
// per element of the run, and 8.3.5p10 names them after the pack - the first
// keeping the pack's own name.  A trailing-return-type stands *past* that
// clause, so `decltype(f(a...))` is read in the region the clause left and the
// run is a fact of the declaration `a` reaches, not of the type it holds: once
// the arguments settle the run, each place holds one element's plain type and
// nothing in it says a pack was ever written.
//
// So every door that declares those places has to carry how long the run is,
// including the two that read the clause again for a specialization.  A run of
// none is the pack declared over no place at all, which `sizeof...` reads as
// zero and an expansion of comes to no arguments.

int nothing()
{
  return 3;
}

int one(int a)
{
  return a;
}

int two(int a, int b)
{
  return a + b;
}

template<class F, class... A>
auto call(F f, A... a) -> decltype(f(a...))
{
  return f(a...);
}

template<class... A>
auto width(A... a) -> decltype(sizeof...(a))
{
  return sizeof...(a);
}

int main()
{
  return (call(nothing) == 3 && call(one, 3) == 3 && call(two, 1, 2) == 3 &&
          width() == 0 && width(1, 2) == 2)
    ? 0 : 1;
}
