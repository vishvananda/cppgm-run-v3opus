// VALIDATION: compile-pass
// 14.2p4: a name written after a prefix no argument list has settled may be a
// template-id of its own, which is what the `template` keyword says.  The list
// it wrote is part of what the name stands for - two lists after one name are
// two members - so it is read where the reading stands and built again where
// the substitution arrives, and the member template the settled class declares
// is then asked for the specialization: a member class template through
// 14.7.1p1's instantiation and a member alias template through its own type-id.
//
// 14.8.2p8 is the other half: a prefix whose class declares no such member
// template is a type this substitution cannot build, which discards the
// candidate that asked for it rather than refusing the program.

template<bool B, class T = void>
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
struct wrapper
{
  typedef T type;
};

struct policy
{
  // A member class template and a member alias template, which 14.3.3p1 reads
  // through two different exits once the prefix is a class.
  template<class U>
  struct boxed
  {
    typedef U type;
  };

  template<class U>
  using pointer = U *;

  template<int N>
  struct at
  {
    enum { value = N };
  };
};

struct bare
{
};

// The prefix is a place, so every component after it stands for itself until an
// argument list names the class.
template<class P, class U>
struct through
{
  typedef typename P::template boxed<U>::type held;
  typedef typename P::template pointer<U> address;
  enum { seventh = P::template at<7>::value };
};

// 14.8.2p8 read through a detector: the member template is looked for in the
// class the arguments made, and a class without it drops the candidate.
template<class P>
struct has_boxed
{
  template<class X>
  static char probe(typename X::template boxed<int> *);

  template<class X>
  static long probe(...);

  static const bool value = sizeof(probe<P>(0)) == sizeof(char);
};

// The same list written twice under one prefix is one member; two lists are two.
template<class P, class A, class B>
struct pair_of
{
  typedef typename P::template boxed<A>::type first;
  typedef typename P::template boxed<B>::type second;
  typedef typename P::template boxed<A>::type again;
};

template<class P, class U, enable_if_t<has_boxed<P>::value, int> = 0>
int accepted(U value)
{
  typename through<P, U>::held held = value;
  typename through<P, U>::address address = &held;
  return (int)*address;
}

int main()
{
  through<policy, int>::held one = 1;
  through<policy, int>::address at_one = &one;

  pair_of<policy, int, long>::first first = 2;
  pair_of<policy, int, long>::second second = 3;
  pair_of<policy, int, long>::again again = 4;

  wrapper<typename policy::boxed<char>::type>::type letter = 'a';

  const int total = (int)*at_one + (int)first + (int)second + (int)again +
      (int)(letter - 'a') + (int)through<policy, int>::seventh +
      accepted<policy>(5);

  return total - 22 + (has_boxed<policy>::value ? 0 : 1) +
      (has_boxed<bare>::value ? 1 : 0);
}
