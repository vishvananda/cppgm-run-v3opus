// N3485 focus: 14.5.3 [temp.variadic] p4 with 8.3.5 [dcl.fct] p3 and p10 - the
// places a parameter-declaration written `pattern... name` declares are that
// pattern read once per element of the run, each reading standing in a region of
// its own, and 8.3.5p10 names them after the pack so a use of the pack's own
// name reaches the first of them.  Every shape a pattern can take is the same
// one reading: a bare place, a ptr-operator over one, a reference, a run of no
// elements at all, and 8.3.5p1's ellipsis standing beside them, which declares
// no place of its own.
template<class... A>
int by_value(A... a)
{
  // 5.3.3p5: `sizeof...` over the places the expansion declared counts the run
  // rather than standing in it.
  return sizeof...(a) * 10 + sizeof...(A);
}

template<class... A>
int by_reference(A&... a)
{
  return sizeof...(a);
}

template<class... A>
int by_const_reference(const A&... a)
{
  return sizeof...(a);
}

template<class... A>
int by_pointer_to_pointer(A**... a)
{
  return sizeof...(a);
}

// 14.5.3p4 with 8.3.5p10: the pack's own name reaches the first place, so the
// run is read off that declaration and forwarded whole to a second call.
template<class... A>
int forwarded(A... a)
{
  return by_value(a...);
}

// A member of a class template whose places the class's own run declares - and
// beside it 8.3.5p1's ellipsis written after a comma, which declares no place
// and is what a clause holding a pack is still allowed to write.
template<class... A>
struct holder {
  static int taken(A... a) { return sizeof...(a); }
  static int none() { return sizeof...(A); }
  static int c_ellipsis(int first, ...) { return first + sizeof...(A); }
};

int main()
{
  int x = 1;
  char y = 'a';
  int* px = &x;
  char* py = &y;

  if (by_value(1, 'a', 2L) != 33) { return 1; }
  if (by_value() != 0) { return 2; }
  if (by_value(1) != 11) { return 3; }
  if (by_reference(x, y) != 2) { return 4; }
  if (by_const_reference(1, 'a') != 2) { return 5; }
  if (by_pointer_to_pointer(&px, &py) != 2) { return 6; }
  if (forwarded(1, 'a', 2L) != 33) { return 8; }
  if (forwarded() != 0) { return 9; }
  if (holder<int, char>::taken(1, 'a') != 2) { return 10; }
  if (holder<int, char>::none() != 2) { return 11; }
  if (holder<>::none() != 0) { return 12; }
  if (holder<int, char>::c_ellipsis(7, 8, 9) != 9) { return 13; }
  return 0;
}
