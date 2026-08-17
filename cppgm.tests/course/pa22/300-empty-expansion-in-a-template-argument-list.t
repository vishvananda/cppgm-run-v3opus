// N3485 focus: 14.5.3p4 [temp.variadic] with 14.3p1 [temp.arg] - an expansion
// written in a template-argument-list is not one argument but as many as the
// run its packs stand for, which may be none.  So what says a list gives a
// template more arguments than it has parameters is the count once the
// expansions have come to what they stand for, and never the count of the
// entries the list wrote.

template<class T> struct one { T slot; };

template<class... A> struct forward { typedef one<A...> type; };

template<int N, int... M> struct vals { static const int n = N; };

template<int... M> struct lead { typedef vals<1, M...> type; };

template<class P, class U> struct rebind { typedef U type; };

template<template<class, class...> class A, class T, class... Args, class U>
struct rebind<A<T, Args...>, U> { typedef A<U, Args...> type; };

template<class X, class Y> struct same { static const bool value = false; };
template<class X> struct same<X, X> { static const bool value = true; };

int main()
{
  forward<int>::type held;
  held.slot = 4;
  if (held.slot != 4) {
    return 1;
  }
  if (lead<>::type::n != 1) {
    return 2;
  }
  if (!same<rebind<one<int>, char>::type, one<char> >::value) {
    return 3;
  }
  return 0;
}
