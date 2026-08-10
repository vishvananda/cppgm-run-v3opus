// N3485 focus: 14.3.2 [temp.arg.nontype] p5 and 4.12 [conv.bool] p1 - an
// argument at a non-type place is a converted constant expression of the type
// the place declared, and a conversion to `bool` is `false` for zero and
// `true` for every other value rather than the low bits of the operand.  So
// 14.4 [temp.type] p1 makes `flag<(bool)3>`, `flag<true>` and `flag<1>` one
// specialization holding 1, and `int spread[(bool)5]` holds one element.
template<bool B>
struct flag {
  static const bool value = B;
  static int get() { return (int)B; }
};

int spread[(bool)5];

int main()
{
  if (sizeof(spread) != sizeof(int)) { return 1; }
  if (flag<(bool)3>::get() != 1) { return 2; }
  if (flag<true>::get() != 1) { return 3; }
  if (flag<1>::get() != 1) { return 4; }
  if (!flag<(bool)3>::value) { return 5; }
  if ((int)(bool)5 != 1) { return 6; }
  return 0;
}
