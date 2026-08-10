// 3.4.3p1 and 7.1.6.2p1: a name is looked up *in* the region its prefix named,
// and a decltype-specifier is one of the two ways to write that prefix.  So the
// demand the other way makes of a settled prefix is the demand this one makes:
// a specialization an argument list has already settled is completed where the
// name stands, inside 14.6p8's reading of a template definition as well as
// outside it, and one an argument list has *not* settled leaves the name the
// 14.6.2p1 stand-in every other prefix leaves it.

template<int N>
struct holder
{
  typedef int type;
  struct in
  {
    typedef long type;
  };
  static const int value = N;
};

// The specialization is named here and nowhere else, so nothing has asked it
// for a definition before the template definitions below are read.
holder<3> make_it();

template<class M>
struct outer
{
  // 5.2.2p1: the operand is a call, which no lookup of its spelling could have
  // answered - and the type it names is settled where this definition stands.
  typedef decltype(make_it())::type held;
  // 3.4.3p1 over two components behind the same prefix.
  typedef decltype(make_it())::in::type nested;
  // 14.2: the same prefix written inside a template-argument-list, which
  // reaches this reading as text rather than as a tree.
  typedef holder<decltype(make_it())::value> named;
  // 5.19: and inside a constant expression, where 5.1.1p8's id-expression is
  // what the reading has to look up behind the prefix.
  static const int twice = decltype(make_it())::value * 2;
  held a;
  nested b;
};

// 14.6.2p1: the prefix an argument list has yet to settle is left standing, and
// the substitution is what looks the name up in the class it became.
template<class M>
struct deferred
{
  static M pick();
  typedef typename decltype(pick())::type held;
  held c;
};

static_assert(sizeof(outer<int>::held) == sizeof(int), "");
static_assert(sizeof(outer<int>::nested) == sizeof(long), "");
static_assert(outer<int>::named::value == 3, "");
static_assert(outer<int>::twice == 6, "");
static_assert(sizeof(deferred<holder<5> >::held) == sizeof(int), "");

int main()
{
  outer<int> o;
  o.a = 1;
  o.b = 2;
  deferred<holder<5> > d;
  d.c = 3;
  return o.a + o.b + d.c == 6 ? 0 : 1;
}
