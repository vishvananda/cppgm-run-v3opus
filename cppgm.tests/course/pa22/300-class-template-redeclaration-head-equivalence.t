// VALIDATION: compile-pass
// N3485 focus: 14.5.6.1 [temp.over.link], 14.1 [temp.param]
// 14.1p2 lets every declaration of one template spell its places by names of
// its own, so two heads that declare the same places are the same head - a
// value place over an earlier one, a pack, and a template place alike.

template<class T>
struct plain;

template<class U>
struct plain
{
  U held;
};

template<class T, T v>
struct valued;

template<class U, U w>
struct valued
{
  static const U held = w;
};

template<class... T>
struct run;

template<class... U>
struct run
{
  static const int width = sizeof...(U);
};

template<class W>
struct box
{
};

template<template<class> class F>
struct outer;

template<template<class> class G>
struct outer
{
  G<int> held;
};

int main()
{
  plain<int> one;
  one.held = 1;
  outer<box> two;
  (void)two;
  return one.held + valued<int, 2>::held + run<int, char, long>::width - 6;
}
