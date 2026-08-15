// VALIDATION: compile-pass
// 5.3.3p5 inside 14.2's argument spelling: a pattern read once per element of
// a run still counts the run itself, because the name a reading left standing
// for one element carries the pack it came from.

template<int N>
int value()
{
  return N;
}

int pair(int a, int b)
{
  return a * 10 + b;
}

template<int... I>
int expand()
{
  return pair(value<I + sizeof...(I)>()...);
}

template<int N>
struct place
{
  static const int value = N;
};

template<int... I>
struct counted
{
  typedef place<sizeof...(I)> type;
};

template<class... T>
struct counted_types
{
  typedef place<sizeof...(T) + sizeof(int)> type;
};

template<class... T>
int arity(T... args)
{
  return place<sizeof...(args)>::value;
}

int main()
{
  return expand<0, 1>() == 23 && counted<1, 2, 3>::type::value == 3 &&
         counted<>::type::value == 0 &&
         counted_types<int, char>::type::value == 6 && arity(1, 2, 3) == 3
    ? 0 : 1;
}
