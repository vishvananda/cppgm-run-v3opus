// 20.8.2p1 [func.require]: `INVOKE(f, t1, ..., tN)` over a callable that is not
// a pointer to member is the call `f(t1, ..., tN)`, so what it names is
// whichever declaration 13.3 chooses for that call - a member `operator()` of
// the object's class where the callable is an object, and the function a
// pointer points to where it is one.
template<class T> T&& declval();
template<class A, class B> struct same { static const bool value = false; };
template<class A> struct same<A, A> { static const bool value = true; };

struct forwarder
{
  template<class T> T&& operator()(T&&) const;
};

struct ranked
{
  int operator()(int) const;
  char operator()(char) const;
};

typedef int (*taking_int)(int);

typedef decltype(__builtin_invoke(declval<forwarder&>(),
                                  declval<const char&>())) forwarded;
typedef decltype(__builtin_invoke(declval<ranked&>(), 'a')) ranked_result;
typedef decltype(__builtin_invoke(declval<taking_int>(), 1)) through_pointer;
typedef decltype(__builtin_invoke(declval<long (&)(int, char)>(), 1, 'a')) two;

static_assert(same<forwarded, const char&>::value, "");
static_assert(same<ranked_result, char>::value, "");
static_assert(same<through_pointer, int>::value, "");
static_assert(same<two, long>::value, "");

int main()
{
  return 0;
}
