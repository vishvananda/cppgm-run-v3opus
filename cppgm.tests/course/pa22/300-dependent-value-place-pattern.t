// 14.3.2p1: a non-type argument is the bits it holds together with the type it
// was converted to, and only the second of those can still name a place - which
// is what `template<class T, T v>` writes.  So `flag<T, true>` in a pattern is
// the settled `true` over a type the match deduces, and a `flag<bool, false>`
// facing it does not match.  14.8.2.5p5 asks the same of a non-deduced context:
// the pattern read back with each place standing for what it was deduced to has
// to be the argument list itself.

template<class T, T v>
struct flag
{
  static const T value = v;
};

template<class... Cond>
struct every
{
  static const int value = 0;
};

template<class... T>
struct every<flag<T, true>...>
{
  static const int value = 1;
};

template<class T>
struct wrapper
{
  typedef T inner;
  typedef T *outer;
};

template<class Owner, class Named>
struct picked
{
  static const int value = 0;
};

template<class Owner>
struct picked<Owner, typename Owner::inner>
{
  static const int value = 5;
};

template<class Owner>
struct picked<Owner, typename Owner::outer>
{
  static const int value = 6;
};

struct thing {};

static_assert(every<>::value == 1, "an empty run matches the pattern");
static_assert(every<flag<bool, true> >::value == 1, "one true element");
static_assert(every<flag<bool, true>, flag<bool, true> >::value == 1,
              "two true elements");
static_assert(every<flag<bool, false> >::value == 0,
              "a false element is not a true one");
static_assert(every<flag<bool, true>, flag<bool, false> >::value == 0,
              "one false element is enough");
static_assert(picked<wrapper<thing>, thing>::value == 5,
              "the member the prefix names is what settles the pattern");
static_assert(picked<wrapper<thing>, thing *>::value == 6,
              "and the other pattern takes the other list");

int main()
{
  return every<flag<bool, true>, flag<bool, false> >::value == 0 ? 0 : 1;
}
