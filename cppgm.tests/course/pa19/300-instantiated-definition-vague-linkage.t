// VALIDATION: compile-pass
// N3485 focus: 14.7.1 [temp.inst], 3.2 [basic.def.odr]
//
// None of these definitions is one this translation unit owns: 14.7.1p1 makes
// each of them a reading of a template every unit that names the
// specialization repeats, so two units that both name it hold one definition
// between them.  A member defined in its class is already inline; the ones
// here are the members, the static data member and the function template that
// are not.

template<class T>
struct box
{
  T v;

  box(T x);

  T twice();

  static T tally;

  struct tag
  {
    int n;
  };

  tag mark();
};

template<class T>
box<T>::box(T x) : v(x)
{
}

template<class T>
T box<T>::twice()
{
  return v + v;
}

template<class T>
T box<T>::tally = T();

template<class T>
typename box<T>::tag box<T>::mark()
{
  tag t;
  t.n = v;
  return t;
}

template<class T>
T pick(T a, T b)
{
  return a < b ? b : a;
}

int main()
{
  box<int> b(3);
  box<int>::tally = b.twice();
  return b.mark().n - 3 + pick<int>(1, 2) - 2 + box<int>::tally - 6;
}
