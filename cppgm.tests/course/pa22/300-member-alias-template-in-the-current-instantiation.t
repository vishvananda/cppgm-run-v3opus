// N3485 focus: 14.5.7 [temp.alias] with 14.6p8 [temp.res] - 7.1.3p2 makes the
// name a member alias template declares a template-name, so `A<T>` written
// beside it in the same class template body is a template-id whose `A` the
// reading of the pattern has to look up.  A reading that declares a
// typedef-name of the type-id read on the spot declares neither the template
// nor a type any argument list settles.

template<class T>
struct traits {
  typedef T value;

  template<class U> struct box { U slot; };

  template<class U> using rebound = U;
  template<class U> using boxed = box<U>;
  template<class U> using owner = rebound<value>;
};

template<class T, class U>
struct sel {
  int f();
};

template<class T>
struct sel<T, T> {
  template<class V> using same = V;
  int f();
};

template<class T>
int sel<T, T>::f()
{
  typename sel<T, T>::template same<int> seven = 7;
  return seven;
}

int main()
{
  traits<int>::owner<char> owned = 3;
  if (owned != 3) {
    return 1;
  }
  traits<int>::boxed<char> held;
  held.slot = 5;
  if (held.slot != 5) {
    return 2;
  }
  sel<int, int> *same = 0;
  return same->f() == 7 ? 0 : 3;
}
