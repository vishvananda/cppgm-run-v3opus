// 14.5.6.1p5 with 14.4p1: two declarations of one template write the value a
// dependent qualified-id names in spellings of their own, and what the name
// reaches is the same member either way - so the declaration and the
// definition meet, and the call has one declaration to name.

template<bool B, class T = void>
struct enable_if
{
};

template<class T>
struct enable_if<true, T>
{
  typedef T type;
};

template<bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<class T>
struct trait
{
  static const int value = 3;
  static const bool ok = true;
};

// A member template declared with the class's own typedef and defined with the
// parameter that typedef names.
template<class T>
struct box
{
  typedef T value_type;

  template<class U, enable_if_t<trait<value_type>::ok, int> = 0>
  int assign(U);

  int call()
  {
    return assign(0);
  }
};

template<class T>
template<class U, enable_if_t<trait<T>::ok, int> >
int box<T>::assign(U)
{
  return trait<T>::value;
}

// A free function template declared through an alias template whose type-id
// writes the value over places the naming discarded, and defined with that
// type-id written out.
template<class... Cond>
using require = enable_if_t<trait<Cond...>::ok>;

template<class T>
require<T> settle(T &, T &);

template<class T>
typename enable_if<trait<T>::ok>::type settle(T & left, T & right)
{
  T held = left;
  left = right;
  right = held;
}

// Two heads that spelled one place differently.
template<class T>
int carried(T);

template<class S>
int carried(S)
{
  return trait<S>::value;
}

int main()
{
  box<int> held;
  int left = 1;
  int right = 2;
  settle(left, right);
  return held.call() + carried(0) - left - right - 3;
}
