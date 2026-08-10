// N3485 focus: 14.2 [temp.names] p3 and 5.9 [expr.rel] p1 - only a `>` ends a
// template-argument-list, so a `<` written inside one is the relational
// operator and a `<<` is 5.8 [expr.shift]'s.  A name that carries such an
// argument is still one name, so its `::` and the `,` between two arguments
// are found outside the argument that wrote them.
template<int N>
struct box {
  static const int value = N;
};

template<int A, int B>
struct pair_box {
  static const int value = A + B;
};

template<class T>
struct wrap {
  static const int value = T::value;
};

int main()
{
  if (box<(1 < 2)>::value != 1) { return 1; }
  if (box<(1 << 3)>::value != 8) { return 2; }
  if (box<(1 <= 1)>::value != 1) { return 3; }
  if (box<(1 << 2 << 3)>::value != 32) { return 4; }
  if (pair_box<(1 < 2), 3>::value != 4) { return 5; }
  if (wrap<box<(1 < 2)> >::value != 1) { return 6; }
  if (box<0 < 1>::value != 1) { return 7; }
  return 0;
}
