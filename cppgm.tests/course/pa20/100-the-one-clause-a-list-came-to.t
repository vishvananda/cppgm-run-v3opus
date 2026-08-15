// N3485 focus: 14.5.3 [temp.variadic] p4 with 8.5 [dcl.init] p16, 5.2.3
// [expr.type.conv] p1 and 12.6.2 [class.base.init] p7 - an entry written
// `pattern...` stands for a run of arguments in every list a program writes one
// into, and a list that holds *one* argument is such a list too.  A non-class
// object initialized from parentheses, a functional cast to a non-class type
// and a mem-initializer of a member of non-class type each take the one
// expression the list came to, which is the pattern read in that element's
// region rather than the entry the program wrote - so each of them answers a
// run of one, and a run of none, exactly as its class-typed twin already does.
struct pair2 {
  int x;
  int y;
  pair2() : x(90), y(90) {}
  pair2(int a) : x(a), y(0) {}
  pair2(int a, int b) : x(a), y(b) {}
};

template<class... A>
struct scalar_member {
  int scalar;
  pair2 object;
  scalar_member(A... a) : scalar(a...), object(a...) {}
};

template<class... A>
int declared(A... a)
{
  int scalar(a...);
  pair2 object(a...);
  return scalar * 100 + object.x;
}

template<class... A>
int converted(A... a)
{
  return int(a...) * 100 + pair2(a...).x;
}

template<int... N>
int folded()
{
  // 5.19p3: the one clause the parentheses came to is a constant expression,
  // so the object is a constant - and 8.3.4p1's bound may be written with the
  // cast that came to the same one.
  const int paren(N...);
  const int braced = {int(N...)};
  int room[int(N...)];
  return paren * 100 + braced * 10 +
         static_cast<int>(sizeof(room) / sizeof(int));
}

template<class... A>
int emptied(A... a)
{
  // 14.5.3p4 over a run of no elements: `pair2 p(a...)` is `pair2 p()`'s
  // value-initialization and `pair2(a...)` is 5.2.3p2's `pair2()`.
  pair2 object(a...);
  return object.x + pair2(a...).y;
}

int main()
{
  if (declared(7) != 707) { return 1; }
  if (converted(6) != 606) { return 2; }
  if (folded<4>() != 444) { return 3; }
  if (emptied() != 180) { return 4; }
  scalar_member<int> one(5);
  if (one.scalar != 5 || one.object.x != 5) { return 5; }
  return 0;
}
