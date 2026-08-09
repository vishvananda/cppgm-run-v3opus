// N3485 focus: 14.5.2 [temp.mem] and 9.3.1 [class.mfct.non-static] a member
// template declares a member like any other, so 9.3.1p3 gives every non-static
// one the object parameter its declarator did not write - and a definition
// written outside the class declares the template the class declared rather
// than a second one.

struct counter
{
  int base;

  template<class T>
  int raised(T step) const;
};

template<class T>
int counter::raised(T step) const
{
  return base + step;
}

int main()
{
  counter one;
  one.base = 40;
  return one.raised(2) - 42;
}
