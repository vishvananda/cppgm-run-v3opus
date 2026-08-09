// N3485 focus: 14.5.1.3 [temp.mem.func] and 3.4.3 [basic.lookup.qual] each
// component of the nested-name-specifier a member definition writes is looked
// up in the region the one before it reached, so a member of a class template
// declared in a namespace is defined outside its class wherever that namespace
// is named from.

namespace vessel
{
template<class T>
struct box
{
  T held;
  T twice() const;
};
}

template<class T>
T vessel::box<T>::twice() const
{
  return held + held;
}

int main()
{
  vessel::box<int> one;
  one.held = 21;
  return one.twice() - 42;
}
