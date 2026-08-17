// N3485 focus: 14.5.1.3 [temp.mem.class] with 14.1p2 [temp.param] - a member
// template defined outside its class writes one head per class it is nested in,
// and 14.1p2 lets each of those heads spell the enclosing class template's
// places with names of its own.  The names the *definition* wrote are what the
// declarator and the body are read against, at every tier such a definition can
// be written at: a constructor template, two member function templates that
// rename the same two places differently, and a member of a partial
// specialization.

template<class T>
struct holder {
  int v;
  template<class U> holder(U u);
};

template<class Tp>
template<class Up>
holder<Tp>::holder(Up u)
  : v(static_cast<int>(u) + static_cast<int>(sizeof(Tp)))
{}

template<class A, class B>
struct two {
  int f();
  int g();
};

template<class X, class Y>
int two<X, Y>::f() { return static_cast<int>(sizeof(X)); }

template<class Y, class X>
int two<Y, X>::g() { return static_cast<int>(sizeof(X)); }

template<class T, class U>
struct sel {
  int f();
};

template<class T>
struct sel<T, T> {
  template<class V> int f();
};

template<class Tq>
template<class Vq>
int sel<Tq, Tq>::f()
{
  return static_cast<int>(sizeof(Tq)) + static_cast<int>(sizeof(Vq));
}

int main()
{
  holder<char> made(2);
  if (made.v != 3) {
    return 1;
  }
  two<char, int> *pair = 0;
  if (pair->f() != 1) {
    return 2;
  }
  if (pair->g() != 4) {
    return 3;
  }
  sel<int, int> *same = 0;
  return same->f<char>() == 5 ? 0 : 4;
}
