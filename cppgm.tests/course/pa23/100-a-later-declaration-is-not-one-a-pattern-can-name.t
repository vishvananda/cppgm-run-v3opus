// 14.6.4.2p1 and 3.4.2p1: a call in a template that depends on a parameter has
// its candidates found by 3.4.1 from the *definition* context and by 3.4.2 from
// the associated namespaces.  A reading the pattern left standing is made again
// where the arguments arrive, by which time the namespace has gone on being
// declared into - and a declaration made after the pattern was written is one
// the second reading may not find.  It matters because 3.4.2p1 lets an ordinary
// declaration that is not a function suppress argument-dependent lookup
// altogether, so an object declared later under the name of a hidden friend
// would hide it.

template<class...>
struct make_void
{
  typedef void type;
};

template<class... T>
using void_t = typename make_void<T...>::type;

struct no
{
  static const int answer = 0;
};

struct yes
{
  static const int answer = 1;
};

template<class T>
T&& value();

namespace lib
{

struct arg
{
};

struct held
{
  friend int reach(arg, held);
};

struct early
{
};

} // namespace lib

// Declared before every pattern below, so it is one their readings do find.
int seen(lib::early);

template<class T, class U, class = void>
struct callable : no
{
};

template<class T, class U>
struct callable<T, U, void_t<decltype(reach(value<T>(), value<U>()))> > : yes
{
};

template<class T, class = void>
struct visible : no
{
};

template<class T>
struct visible<T, void_t<decltype(seen(value<T>()))> > : yes
{
};

// An object of this name would suppress 3.4.2's lookup where it were visible.
struct blocker
{
  void operator()(int, int) const;
};

extern blocker reach;

// And an ordinary function declared after the patterns, which is neither found
// by 3.4.1 from the definition context nor associated with any argument.
int seen(lib::held);

int main()
{
  int settled = 0;
  settled += callable<lib::arg, lib::held>::answer == 1 ? 0 : 1;
  settled += visible<lib::early>::answer == 1 ? 0 : 1;
  settled += visible<lib::held>::answer == 0 ? 0 : 1;
  return settled;
}
