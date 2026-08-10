// 3.4.3p1 and 14.7.1p1: a nested-name-specifier is looked up *in* the region
// its component named, which for a class template specialization an argument
// list has already settled is a context requiring that specialization to be
// completely defined - so 14.6p8's reading of a template definition asks for
// the definition where the prefix is settled, exactly as 10p1's base class
// does, and leaves it unasked only where the prefix still depends on a
// parameter.

template<bool>
struct choose
{
  typedef int type;
};

template<class>
struct hold
{
  typedef long type;
};

template<class M>
struct make
{
  // The prefix is settled where this definition stands, so the reading has to
  // complete `choose<true>` here rather than leave the name unresolvable.
  typedef typename choose<true>::type settled;
  static bool const lower = sizeof(M) != 0;
  // And one written over a member of the class being read is settled too.
  typedef typename choose<lower>::type over_a_member;
  // 14.6.2p1: this one is not, and is left to the argument list.
  typedef typename hold<M>::type deferred;
};

typedef make<int>::settled settled;
typedef make<int>::over_a_member over_a_member;
typedef make<int>::deferred deferred;

static_assert(sizeof(settled) == sizeof(int), "");
static_assert(sizeof(over_a_member) == sizeof(int), "");
static_assert(sizeof(deferred) == sizeof(long), "");

int main()
{
  settled a = 1;
  over_a_member b = 2;
  deferred c = 3;
  return a + b == 3 && c == 3 ? 0 : 1;
}
