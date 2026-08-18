// 14.3.3p1: a template written at a template place shall declare places that
// place accepts, asked of an argument a substitution settled and not only of
// one a spelling looked up.
template<template<class> class F> struct helper { typedef F<int> type; };
struct wrong_kind { template<int> struct member {}; };
template<class T> struct use { typedef helper<T::template member> type; };
use<wrong_kind>::type u;
int main() { return 0; }
