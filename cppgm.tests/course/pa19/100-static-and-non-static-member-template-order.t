// N3485 focus: 14.5.6.2p2 [temp.func.order] and 13.3.1p4 [over.match.funcs]
// 9.3.1p3 puts a non-static member's object parameter in its type, and 13.3.1p4
// gives a static member of the same class an implicit one that matches any
// object - so the two are ordered by the places their declarators wrote and the
// one written over `T *` wins the call an ordinary `T` also deduces.

struct counter
{
  int base;

  template<class T>
  static int reach(T step)
  {
    return static_cast<int>(step);
  }

  template<class T>
  int reach(T *step)
  {
    return base + static_cast<int>(*step);
  }
};

int main()
{
  counter one;
  one.base = 38;
  int two = 2;
  return one.reach(&two) + counter::reach(2) - 42;
}
