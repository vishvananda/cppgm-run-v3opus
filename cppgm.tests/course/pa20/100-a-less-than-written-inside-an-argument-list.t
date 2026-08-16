// VALIDATION: compile-pass
// 14.2 and 5.9: a `<` written inside a template-argument-list is the operator
// wherever the name before it is no template, which the flattened spelling of
// the name holds only in the `>` that closes the list.

template<class> struct rank
{
  static const int value = 1;
};

template<bool> struct picked
{
  typedef int type;
};

template<bool, class, class> struct chosen;

template<class A, class B>
struct dependent
{
  // Both operands name a member of a dependent specialization, so nothing but
  // the closing `>` says which of the two `<` opens the list.
  typedef typename chosen<rank<A>::value < rank<B>::value, B, A>::type type;
};

static const int lower = 1;
static const int higher = 2;

typedef picked<lower < higher>::type one;
typedef picked<(higher > lower)>::type grouped;
typedef picked<sizeof(int) < sizeof(long)>::type sized;
typedef picked<0 < rank<int>::value>::type after;
typedef picked<lower <= higher>::type or_equal;

template<bool, bool> struct pair
{
  typedef int type;
};

typedef pair<lower < higher, higher < lower>::type two;

template<class T>
struct nested
{
  typedef typename picked<0 < rank<T>::value>::type type;
};

int main()
{
  one a = 0;
  grouped b = 0;
  sized c = 0;
  after d = 0;
  or_equal e = 0;
  two f = 0;
  nested<char>::type g = 0;
  return a + b + c + d + e + f + g;
}
