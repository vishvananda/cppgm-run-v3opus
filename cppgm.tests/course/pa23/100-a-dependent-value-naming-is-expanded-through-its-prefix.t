// 14.5.3p5 with 14.4p1: which packs a value a dependent qualified-id names
// stands over is a fact of the prefix it was written after and of nothing
// else, because the name that follows the prefix can name no pack of its own.
// So an expansion of such a naming is read per element wherever the entry came
// from - one a declarator wrote, and one a substitution rebuilt over a prefix
// it left dependent, which no reading of a spelling ever stood in.

template<class T>
struct trait
{
  static const int value = 1;
};

template<class T, class U>
struct paired
{
  static const int value = 2;

  struct inner
  {
    static const int value = 4;
  };
};

template<int ... Ns>
struct list
{
  static const int count = sizeof...(Ns);
};

template<int ... Ns>
struct summed;

template<>
struct summed<>
{
  static const int total = 0;
};

template<int First, int ... Rest>
struct summed<First, Rest ...>
{
  static const int total = First + summed<Rest ...>::total;
};

// The prefix names the pack where the declarator stands, so the entry the
// reading interned is the one the expansion walks.
template<class ... Ts>
struct written
{
  typedef list<trait<Ts>::value ...> held;
  typedef summed<trait<Ts>::value ...> added;
};

// The prefix is one an *enclosing* substitution moved: `X` settles and `Ts`
// does not, so the naming stands again over a prefix this substitution built
// and the expansion has to find the run through that prefix.
template<class X>
struct outer
{
  template<class ... Ts>
  struct rebuilt
  {
    typedef list<paired<X, Ts>::value ...> held;
    typedef summed<paired<X, Ts>::inner::value ...> nested;
  };

  template<class ... Ts>
  static int counted(list<paired<X, Ts>::value ...> *)
  {
    return sizeof...(Ts);
  }
};

int main()
{
  const int one = written<int, char, long>::held::count;
  const int two = written<int, char, long>::added::total;
  const int three = outer<int>::rebuilt<char, long>::held::count;
  const int four = outer<int>::rebuilt<char, long>::nested::total;
  list<2, 2> made;
  const int five = outer<int>::counted<char, long>(&made);
  return one + two + three + four + five - 3 - 3 - 2 - 8 - 2;
}
