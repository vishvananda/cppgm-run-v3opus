// VALIDATION: run-pass
// 14.2p3's ending delimiter, read backwards.  The pattern an inner `...`
// expands is found by walking back from the `...` to the `<` the template-id
// before it opened, and not every `>` written in between closes a list: a `>=`,
// a `->` and every angle inside 5.1.1p6's parentheses close nothing.  A walk
// that took them all for delimiters ran past the pattern's own `<` and took the
// names written before it along, which left the *enclosing* expansion naming no
// parameter pack at all.

template<bool V>
struct bool_constant
{
  static const int value = V ? 1 : 0;
};

template<class... T>
struct list
{
  static const int length = sizeof...(T);
};

struct reads
{
  int held;
};

template<class... Outer>
struct outer
{
  template<class... Inner>
  struct parenthesized
  {
    typedef list<list<Outer, bool_constant<(sizeof(Inner) > 1)>...>...> type;
  };

  template<class... Inner>
  struct relational
  {
    typedef list<list<Outer, bool_constant<sizeof(Inner) >= 2>...>...> type;
  };

  template<class... Inner>
  struct arrowed
  {
    typedef list<list<Outer, list<decltype(((Inner *)0)->held)>...>...> type;
  };
};

int main()
{
  const int one = outer<int, char>::parenthesized<long, short>::type::length;
  const int two = outer<int, char>::relational<long, short>::type::length;
  const int three = outer<int, char>::arrowed<reads, reads>::type::length;
  return one + two + three == 6 ? 0 : 1;
}
