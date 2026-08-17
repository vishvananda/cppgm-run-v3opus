// VALIDATION: compile-pass
// N3485 focus: 5.18 [expr.comma], 5.2.9 [expr.static.cast], 14.3.2 [temp.arg.nontype]
// A template-argument-list reaches the semantic layer as text, so 5.19's whole
// operator set has to be read back out of one spelling.  5.18p1's comma is the
// one operator a constant expression may write only inside 5.1.1p6's
// parentheses - outside them a comma separates one argument from the next - and
// 5.2.9p4's cast to cv void is the one operand whose value is discarded, which
// is what its left operand may be.

template<int N>
struct S
{
  static const int value = N;
};

template<bool B>
struct flag
{
  static const bool value = B;
};

template<class A, class B>
struct same
{
  static const bool value = false;
};

template<class A>
struct same<A, A>
{
  static const bool value = true;
};

template<bool...>
struct row;

template<bool... Bs>
struct all : same<row<Bs...>, row<((void)Bs, true)...> >
{
};

int main()
{
  int total = 0;
  total += S<(1, 2)>::value;
  total += S<(1, 2, 3)>::value;
  total += S<((void)1, 5)>::value;
  total += S<(static_cast<void>(7), 4)>::value;
  total += S<(sizeof(int), 7)>::value;
  total += S<(1, 2) + 3>::value;
  total += flag<all<true, true>::value>::value ? 100 : 0;
  total += flag<all<false>::value>::value ? 0 : 200;
  return total == 326 ? 0 : 1;
}
