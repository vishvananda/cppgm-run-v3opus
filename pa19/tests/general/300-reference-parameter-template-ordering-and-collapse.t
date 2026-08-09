// VALIDATION: compile-pass
// N3485 focus: 14.5.6.2 [temp.func.order], 14.8.2.1 [temp.deduct.call], 5.3.3 [expr.sizeof]
//
// 14.5.6.2p5 and p7 take the reference and the top-level qualifiers off both
// types before the ordering, so `f(T &)` and `f(const T &)` deduce each other
// and the ordering as far as p7 says neither is more specialized.  p9 is what
// the two clauses took: a place both templates wrote a reference at is ordered
// by which reference is the more qualified, and an lvalue reference stands
// ahead of what is not one.
//
// 14.8.2.1p3 is the other half of the same reading: an rvalue reference over a
// parameter nothing wrote a qualifier on deduces "lvalue reference to A" from
// an lvalue, so 8.3.2p6 collapses the two and the parameter binds it - which
// makes the argument the template's parameter names a reference type, and
// 5.3.3p2 measures one of those as the type it refers to.

template<class T>
int qualified(T &)
{
  return 1;
}

template<class T>
int qualified(const T &)
{
  return 2;
}

template<class T>
int forwarded(T &&)
{
  return 4;
}

template<class T>
int forwarded(const T &)
{
  return 8;
}

template<class T>
int measured(T &&)
{
  return (int)sizeof(T) + (int)alignof(T);
}

int main()
{
  int mutable_object = 0;

  const int frozen = 0;

  // 13.3.3.2p3 settles the first on the qualifiers the reference bound with;
  // the second ties there and 14.5.6.2p9 settles it.
  const int ordered = qualified(mutable_object) + qualified(frozen) * 10;

  // An lvalue deduces `T` as a reference and the rvalue reference collapses to
  // one; a prvalue leaves the rvalue reference standing.
  const int bound = forwarded(mutable_object) + forwarded(1) * 10;

  return ordered + bound * 100 + measured(mutable_object) * 10000;
}
