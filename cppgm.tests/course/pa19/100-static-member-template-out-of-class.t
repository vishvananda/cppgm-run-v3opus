// N3485 focus: 14.5.2 [temp.mem], 9.4p1 [class.static] and 9.4.1p2
// [class.static.mfct] a static member template is called on no object, and the
// definition written outside the class repeats no `static` to say so - so the
// declaration it redeclares is found by 14.5.6.1p5's signature, over each
// head's own parameters, rather than by 13.1's index of the list as written.

struct counter
{
  static int base;

  template<class T>
  static int raised(T step);
};

int counter::base = 40;

template<class T>
int counter::raised(T step)
{
  return counter::base + step;
}

int main()
{
  return counter::raised(2) - 42;
}
