// VALIDATION: compile-pass
// 14.5.3p5 and 5.3.3p5 where the inner `...` is written *inside* a spelling
// PA10 flattened rather than as a node beside it.  A template-id is one
// terminal of the tree it stands in - one callee, one decl-specifier - so
// `list<A, B...>` holds its own expansion in its text, and the reading that
// asks which packs the enclosing pattern is over has to answer of that text the
// way it answers of an argument list: the operand each inner `...` was written
// after is left out, and `sizeof...` is left out with the name it counts.
//
// Nothing here observes the elements themselves - each call is counted rather
// than run - so what the program pins is the run each expansion came to.

template<class... E>
struct list
{
  static const int n = sizeof...(E);
};

template<class T, int N>
struct pair2
{
  static const int n = N;
};

template<class... T>
int count(T... t)
{
  return sizeof...(t);
}

template<class... B>
struct outer
{
  template<class... A>
  struct inner
  {
    // The outer `...` is over `A` alone: `B` is expanded by the inner one, so
    // every element of the run holds the whole of `B` however long it is.
    static int nested()
    {
      return count(list<A, B...>()...);
    }

    // 5.3.3p5: `sizeof...(B)` is a value rather than a use of `B`, so it says
    // nothing about how long this run is either.
    static int counted()
    {
      return count(pair2<A, sizeof...(B)>()...);
    }
  };
};

int main()
{
  // Two packs of different lengths, and the run is the outer one's.
  int a = outer<int, char, long>::inner<short, double>::nested();
  int b = outer<int, char, long>::inner<short, double>::counted();
  // The same where the inner run holds nothing at all.
  int c = outer<>::inner<short, double>::nested();
  // And where the two happen to be equally long, which is the shape a reading
  // that counted both would still have got right.
  int d = outer<int, char>::inner<short, double>::nested();

  return a == 2 && b == 2 && c == 2 && d == 2 ? 0 : 1;
}
