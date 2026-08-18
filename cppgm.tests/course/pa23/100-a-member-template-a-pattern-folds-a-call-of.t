// VALIDATION: compile-pass
// 14p1 and 14.7.1p1: there is nothing to instantiate where the argument list
// has settled nothing.  Two readings arrive at such a naming and neither is an
// error the program made.
//
// 14.6p8's reading of a class template's own definition declares nothing into
// the output, so a member template of the class that definition declares has no
// pattern recorded there at all - and a fold written in the same body,
// `pick<int>()` in a static data member's initializer, names a specialization of
// it.  A template *head* read in earnest is the other: it declares places of its
// own, and a default template argument written over them - `enable_if_t<ok<U>()>`
// - names a specialization over a `U` no list has settled while the dialect is a
// lowering.  In both, what the call comes to is the arguments' to say, and the
// naming that follows with them in hand is where 7.1.5p2 is answered.
//
// 7.1.5p2 travels with the declaration: `constexpr` stands on the template's
// declarator, so every specialization of it is a constexpr function whatever
// the argument list - which is the fact the reading that makes no definition
// has to read off the template rather than off a body it never wrote.

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

template<class, class>
struct same
{
  static const bool value = false;
};

template<class T>
struct same<T, T>
{
  static const bool value = true;
};

// 14.6p8: the fold is written in the body being read, and the member template
// it calls is one that reading records no pattern for.
template<class T>
struct holder
{
  template<class U>
  static constexpr int weigh()
  {
    return (int)sizeof(U) + (int)sizeof(T);
  }

  static const int measured = weigh<char>();

  enum { counted = weigh<short>() };

  // A nested class of the current instantiation, whose own member template a
  // default template argument over an unsettled place calls.
  struct check
  {
    template<class U>
    static constexpr bool matches()
    {
      return same<T, U>::value;
    }
  };

  template<class U, enable_if_t<check::template matches<U>(), int> = 0>
  static int only_same(U)
  {
    return 1;
  }

  static int only_same(...)
  {
    return 2;
  }
};

template<class U>
constexpr bool wide()
{
  return sizeof(U) > 1;
}

// The head is read in earnest and its own place is what the default is written
// over, so the call names a specialization no list has settled.
template<class U, enable_if_t<wide<U>(), int> = 0>
int picked(U)
{
  return 4;
}

int picked(...)
{
  return 8;
}

int main()
{
  const int measured = holder<int>::measured;
  const int counted = (int)holder<int>::counted;

  const int matching = holder<int>::only_same(0);
  const int differing = holder<char>::only_same(0);

  const int wide_one = picked((long)0);
  const int narrow_one = picked((char)0);

  return (measured - 5) + (counted - 6) + (matching - 1) + (differing - 2) +
      (wide_one - 4) + (narrow_one - 8);
}
