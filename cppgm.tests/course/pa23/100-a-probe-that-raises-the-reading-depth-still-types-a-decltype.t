// VALIDATION: run-pass
// 14.6.2.2p1 and 7.1.6.2p4: a decltype-specifier names a type an instantiation
// alone can name only where its operand depends on a template parameter, which
// is a fact of the region the operand stands in.  14.6p8's ambient depth is no
// answer to it: a *probe* raises that depth to keep a naming from demanding a
// definition, and is still reading an operand every name of which is settled.
// So `sizeof(decltype(0))` written as a template argument is a number, where a
// stand-in there would be an argument no list could ever settle.

template<bool B, class T = void>
struct enable_if {};

template<class T>
struct enable_if<true, T> { typedef T type; };

template<class T>
T && declval();

template<unsigned long N>
struct box { static const unsigned long size = N; };

struct wrapped { char const * value; };

template<class T>
typename enable_if<sizeof(T) == sizeof(decltype(declval<T const &>().value)),
                   int>::type
probe(T const &)
{
  return 7;
}

int main()
{
  wrapped value = {"value"};
  box<sizeof(decltype(0))> plain;
  box<sizeof(decltype(declval<wrapped>().value))> reached;
  return probe(value) == 7 && plain.size == sizeof(int) &&
      reached.size == sizeof(char *) ? 0 : 1;
}
