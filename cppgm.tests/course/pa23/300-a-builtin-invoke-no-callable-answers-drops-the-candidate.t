// 14.8.2p8 with 20.8.2p1: the call `INVOKE` stands for is made in the immediate
// context of the substitution, so a class whose declarations answer no such
// call is a deduction that fails rather than a program that is refused.
template<class T> T&& declval();
template<class...> struct make_void { typedef void type; };
template<class... T> using void_t = typename make_void<T...>::type;

template<class T, class = void>
struct callable_with_int
{
  static const bool value = false;
};

template<class T>
struct callable_with_int<T, void_t<decltype(__builtin_invoke(declval<T&>(),
                                                             1))> >
{
  static const bool value = true;
};

struct answers { int operator()(int) const; };
struct wrong_arity { int operator()(int, int) const; };
struct not_callable { int member; };

static_assert(callable_with_int<answers>::value, "");
static_assert(!callable_with_int<wrong_arity>::value, "");
static_assert(!callable_with_int<not_callable>::value, "");
static_assert(!callable_with_int<int>::value, "");

int main()
{
  return 0;
}
