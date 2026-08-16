// VALIDATION: compile-pass
// 14.5.3p5 and 5.3.3p5: a name already expanded where it stands says nothing
// about how long *this* run is.  The pattern of an inner `...` is read once per
// element of its own run, and `sizeof...` counts a run rather than standing in
// one - so neither names a pack the enclosing expansion is over.  A tree tells
// the two apart by node kind and 14.2's argument list by the text it wrote them
// in, and both readings have to answer alike.

template<class... T>
struct holder
{
  char bytes[sizeof...(T) + 1];
};

template<class T>
struct first_type;

template<class Head, class... Rest>
struct first_type<holder<Head, Rest...> >
{
  typedef Head type;
};

template<int...>
struct nums
{
};

template<class T>
struct first_num;

template<int Head, int... Rest>
struct first_num<nums<Head, Rest...> >
{
  static const int value = Head;
};

template<class... Args>
struct outer
{
  // The outer `...` is over `Args` alone: `Rest` is expanded by the inner one,
  // so every element of the run holds the whole of `Rest`.
  template<class... Rest>
  struct inner
  {
    typedef holder<holder<Args, Rest...>...> made;
  };

  // 5.3.3p5: `sizeof...(Args)` is a value, so this expansion is over `Which`
  // however long `Args` turns out to be.
  template<int... Which>
  struct counted
  {
    typedef nums<(Which + (int)sizeof...(Args))...> made;
  };
};

int add(int left, int right)
{
  return left + right;
}

int one(int value)
{
  return value;
}

template<class... Args>
struct written
{
  // The same two questions asked of the tree a call's argument list holds.
  template<class... Rest>
  static int counting(Rest... rest)
  {
    return add(one((int)sizeof(Rest) + (int)sizeof...(Args))...);
  }

  template<class... Rest>
  static int nesting(Rest... rest)
  {
    return add(one((int)sizeof(Args) + (int)sizeof...(rest))...);
  }
};

int main()
{
  typedef outer<char, short>::inner<int, long, float>::made made;
  typedef outer<char, short, int>::counted<1, 2>::made counted;
  return sizeof(made) == 3 && sizeof(first_type<made>::type) == 5 &&
         first_num<counted>::value == 4 &&
         written<char, short, int>::counting((char)1, (short)2) == 9 &&
         written<char, short>::nesting((char)1, (short)2, (int)3) == 9
    ? 0 : 1;
}
