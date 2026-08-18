// VALIDATION: compile-pass
// 6.6.3p2 and 12.1: which of the ABI's two entry points a base subobject built
// inside an instantiation asks this unit for.  A body a class instantiation put
// aside is the class's own, so the entry it names is the only one owed; a body
// a use of a class-returning function template asked for stands at the end of
// the chain 6.6.3p2 puts the object in the caller's storage for, and owes both.

template<class T> struct base_a { T v; base_a(T x) : v(x) {} };
template<class T> struct derived_a : base_a<T> { derived_a(T x) : base_a<T>(x) {} };

template<class T> struct base_b { T v; base_b(T x) : v(x) {} };
template<class T> struct derived_b : base_b<T> { derived_b(T x) : base_b<T>(x) {} };

template<class T> struct base_c { T v; base_c(T x) : v(x) {} };
template<class T> struct derived_c : base_c<T> { derived_c(T x) : base_c<T>(x) {} };

template<class T> struct maker {
  derived_a<T> held(T x) { return derived_a<T>(x); }
  derived_c<T> written(T x);
};

template<class T> derived_c<T> maker<T>::written(T x) { return derived_c<T>(x); }

template<class T> derived_b<T> free_made(T x) { return derived_b<T>(x); }

int main()
{
  maker<int> m;
  derived_a<int> a = m.held(1);
  derived_b<int> b = free_made(2);
  derived_c<int> c = m.written(3);
  return c.v - b.v - a.v == 0 ? 0 : 1;
}
