// VALIDATION: compile-fail
// 5.3.3p5: `sizeof...` counts a run rather than standing in one, so a pattern
// whose only pack it names is written over nothing - the same answer 14.5.3p5
// gives a pattern whose only pack an inner `...` already expanded.

template<int...>
struct nums
{
};

template<class... Args>
struct outer
{
  template<int... Which>
  struct inner
  {
    typedef nums<((int)sizeof...(Args))...> made;
  };
};

int main()
{
  outer<char, short>::inner<1, 2>::made made;
  (void)made;
  return 0;
}
