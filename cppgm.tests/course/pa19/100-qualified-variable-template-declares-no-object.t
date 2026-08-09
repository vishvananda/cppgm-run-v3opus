// N3485 focus: 14p1 [temp] and 9.4.2 [class.static.data] - what tells a
// template-declaration's pattern from 14.5.1.3p1's static data member is the
// region the declaration belongs to and not the spelling that reached it.  A
// qualified declarator-id naming a namespace member declares the same pattern an
// unqualified one there does, so this unit lays out no storage for it; the one
// naming a class member is the definition of that member and still does.

namespace outer
{
  template<class T>
  T pattern;

  template<class T>
  struct holder
  {
    static T member;
  };
}

template<class T>
T outer::pattern;

template<class T>
T outer::holder<T>::member;

int main()
{
  outer::holder<int>::member = 0;
  return outer::holder<int>::member;
}
