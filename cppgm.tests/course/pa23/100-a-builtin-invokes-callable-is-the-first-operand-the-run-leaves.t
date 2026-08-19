// 20.8.2p1 with 14.5.3p4: the operands of `__builtin_invoke` are the arguments
// it wrote read the way any other call's are, so a clause written `pattern...`
// stands for one operand per element of the run its packs are bound to - and
// which of them is the callable is a fact of the expanded run rather than of
// what the program wrote.
template<class T> T&& declval();
template<class A, class B> struct same { static const bool value = false; };
template<class A> struct same<A, A> { static const bool value = true; };

struct forwarder
{
  template<class T> T&& operator()(T&&) const;
};

template<class... Args>
struct whole_run
{
  typedef decltype(__builtin_invoke(declval<Args>()...)) type;
};

template<class... Args>
struct callable_written
{
  typedef decltype(__builtin_invoke(declval<long (*)(int, char)>(),
                                    declval<Args>()...)) type;
};

static_assert(same<whole_run<forwarder&, const int&>::type,
                   const int&>::value, "");
static_assert(same<whole_run<void (*)()>::type, void>::value, "");
static_assert(same<callable_written<int, char>::type, long>::value, "");

int main()
{
  return 0;
}
