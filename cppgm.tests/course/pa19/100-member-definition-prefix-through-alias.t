// N3485 focus: 3.4.3p1 [basic.lookup.qual] and 14.5.1.3p1 [temp.mem.func] the
// class template an out-of-class member definition belongs to is found by
// walking the nested-name-specifier component by component - so a leading `::`
// names the global namespace and a typedef-name names the class it is an alias
// for, exactly as they do for every other prefix.

namespace vessel
{
  struct real
  {
    template<class T>
    struct held
    {
      T slot;
      int twice();
      int thrice();
    };
  };

  typedef real alias;
}

template<class T>
int ::vessel::real::held<T>::twice()
{
  return slot + slot;
}

template<class T>
int vessel::alias::held<T>::thrice()
{
  return slot + slot + slot;
}

int main()
{
  vessel::real::held<int> one;
  one.slot = 6;
  return one.twice() + one.thrice() - 30;
}
