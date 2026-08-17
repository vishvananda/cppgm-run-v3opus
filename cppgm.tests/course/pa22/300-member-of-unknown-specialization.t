// VALIDATION: compile-pass
// N3485 focus: 14.6.2.1 [temp.dep.type], 14.6.2 [temp.dep], 10.2 [class.member.lookup]
// 14.6.2p3 leaves a base-specifier whose type depends on a template parameter
// off the chain 3.4.1 searches inside the definition, because which class that
// base is only an argument list says.  14.6.2.1p6 is the other half: a
// qualified name looked up in a class with such a base and not found there is
// not a name the program failed to declare - it is a member of a class no
// argument list has named yet, and it stands in until the arguments arrive.
// Every component of the prefix asks the same question as the name itself.

template<class T>
struct base
{
  typedef T value_type;
  struct inner { typedef T held; };
  static const int mark = 5;
  static int twice() { return 12; }
};

struct plain
{
  typedef long other;
};

template<class T>
struct derived : base<T>, plain
{
  // The name the current instantiation declares nothing of, found in a base
  // only an argument list settles.
  typedef typename derived::value_type through_current;
  // The same written with the arguments spelled out.
  typedef typename derived<T>::value_type through_arguments;
  // A component of the prefix answered the same way, and the name behind it.
  typedef typename derived::inner::held through_component;
  // A base whose specifier named a settled class still answers here.
  typedef typename derived::other settled;

  through_current a;
  through_arguments b;
  through_component c;
  settled d;

  static int mark_plus() { return derived::mark + 1; }
  static int call_through() { return derived::twice(); }
};

int main()
{
  derived<int> d;
  d.a = 1;
  d.b = 2;
  d.c = 3;
  d.d = 4;
  return d.a + d.b + d.c + d.d == 10 &&
         derived<int>::mark_plus() == 6 &&
         derived<int>::call_through() == 12
    ? 0 : 1;
}
