// 14.1p9 and 14.3p1: a default argument at a value place is a constant
// expression, and 5.19 is evaluated where the expression stands - so a naming
// that leaves an earlier place dependent has nothing to evaluate it over and
// the reading itself is the argument, made again where the arguments settle.

template<class T>
struct trait
{
  static const bool value = true;
  enum { width = 2 };
};

template<class T>
struct proxy
{
};

template<class T>
struct trait<proxy<T> >
{
  static const bool value = false;
  enum { width = 3 };
};

// The default names the place before it, which the naming below leaves
// dependent until the call deduces it.
template<class T, bool B = trait<T>::value>
struct gate
{
  typedef int type;
  static const int answer = 1;
};

template<class T>
struct gate<T, false>
{
  typedef long type;
  static const int answer = 2;
};

// 14.8.2p5: the result is read where the declaration is, over a place, and
// again over the argument the call deduces.
template<class A>
typename gate<A>::type first(A)
{
  return 1;
}

// 14.1p9 at a place whose own type an earlier argument settles, and a second
// default reading the first.
template<class T, int W = trait<T>::width, int X = W + 1>
struct sized
{
  static const int answer = X;
};

template<class A>
int second(A)
{
  return sized<A>::answer;
}

// Two namings of one argument list are one specialization, so the default is
// read once and both namings agree on what it came to.
template<class A>
int third(A)
{
  return gate<A>::answer + gate<A>::answer;
}

int main()
{
  int settled = 0;
  settled += static_cast<int>(sizeof(first(0)) == sizeof(int)) ? 0 : 1;
  settled += static_cast<int>(sizeof(first(proxy<int>())) == sizeof(long))
                 ? 0
                 : 1;
  settled += second(0) == 3 ? 0 : 1;
  settled += second(proxy<char>()) == 4 ? 0 : 1;
  settled += third(0) == 2 ? 0 : 1;
  settled += third(proxy<long>()) == 4 ? 0 : 1;
  return settled;
}
