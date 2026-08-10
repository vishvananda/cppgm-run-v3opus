// 14.1p4 and 14.5.1.3p1: a definition written outside its class is read against
// 14.6.1p1's current instantiation, which puts at each place the place itself -
// so a place that binds a *value* has to be bound as one there, exactly as the
// class's own body binds it.  Bound as a type instead, 5.1.1p8 refuses every use
// the definition makes of the name, which is every out-of-class definition of a
// member of a class template with a non-type parameter.
//
// 14.1p2 leaves the definition free to spell the places as it likes, so the name
// this head wrote is what has to reach the place the class's head declared.

template<class T, int N>
struct holder
{
  static T slot;
  static int count();
  int scaled() const;
  T store[N];
};

// A static data member's initializer, written over the value place.
template<class T, int N>
T holder<T, N>::slot = N * 2;

// A member function's body, over a place this head spells differently.
template<class U, int M>
int holder<U, M>::count()
{
  U held[M];
  held[M - 1] = M;
  return held[M - 1] + M;
}

template<class T, int N>
int holder<T, N>::scaled() const
{
  return N + (int)sizeof(store) / (int)sizeof(T);
}

// 14.5.3p1: a run is a place of its own, and the same head spells it too.
template<int... Ns>
struct counted
{
  static int total();
};

template<int... Ms>
int counted<Ms...>::total()
{
  return (int)sizeof...(Ms);
}

int main()
{
  holder<int, 3> h;
  return holder<int, 3>::slot == 6 && holder<int, 3>::count() == 6 &&
         h.scaled() == 6 && counted<1, 2, 3, 4>::total() == 4
    ? 0
    : 1;
}
