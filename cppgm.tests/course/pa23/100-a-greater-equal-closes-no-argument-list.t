// VALIDATION: compile-pass
// 14.2p3 takes the first non-nested `>` as the ending delimiter and splits
// `>>` and nothing else, so no `>` of a `>=` token ends a list.  PA10 hands
// the name on as the terminals the parse matched, which writes `>=` closed up
// and a `>` that really does close a list apart from a following `=`.

template<bool B>
struct bool_constant
{
  static const bool value = B;
};

template<bool, class T = void>
struct enable_if {};

template<class T>
struct enable_if<true, T>
{
  typedef T type;
};

template<bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<class...>
struct all : bool_constant<true> {};

template<class... T>
struct tuple
{
  template<class Dummy = void,
           enable_if_t<all<bool_constant<sizeof...(T) >= 1> >::value, int> = 0>
  tuple() {}
};

static_assert(all<bool_constant<1 >= 1>, bool_constant<1 <= 1> >::value,
              "a relational token is no ending delimiter");
static_assert(bool_constant<(8 >> 1) == 4>::value, "a shift is not two delimiters");

int main()
{
  tuple<int> one;
  (void)one;
  return 0;
}
