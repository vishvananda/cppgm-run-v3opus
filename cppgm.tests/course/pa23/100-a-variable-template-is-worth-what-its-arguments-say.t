// VALIDATION: compile-pass
// 14.6.2p2: a template-id over a variable template whose argument list an
// enclosing head has yet to settle is a value-dependent expression.  Reading
// the primary's own initializer for it instead answers the whole program with
// one constant - and a trait written that way then enables or disables every
// candidate alike, which 14.8.2p8 was supposed to decide one specialization at
// a time.

template<bool, class T = void>
struct enable_if
{
};

template<class T>
struct enable_if<true, T>
{
  typedef T type;
};

template<bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<class T>
const bool pointed = false;

template<class T>
const bool pointed<T *> = true;

template<class T>
const int width = 1;

template<>
const int width<long> = 8;

template<int N>
struct sized
{
  static const int held = N;
};

char which(...);

template<class T, enable_if_t<pointed<T>, int> = 0>
long which(T);

template<class T>
struct measured
{
  typedef sized<width<T> > type;
};

static_assert(sizeof(which(0)) == sizeof(char), "");
static_assert(sizeof(which((int *)0)) == sizeof(long), "");
static_assert(measured<int>::type::held == 1, "");
static_assert(measured<long>::type::held == 8, "");

int main()
{
  return sizeof(which((char *)0)) == sizeof(long) ? 0 : 1;
}
