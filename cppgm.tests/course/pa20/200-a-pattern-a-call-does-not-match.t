// N3485 focus: 14.5.3 [temp.variadic] p4 with 14.8.2.1 [temp.deduct.call] p1 -
// the pattern of a trailing pack expansion is read once per element, so what a
// call has to write at each of the places it comes to is an argument that pair
// deduces.  A pattern that is a class template specialization is read per
// element like any other one, and an argument that is not a specialization of
// that template deduces nothing at that place - so the call names no
// specialization at all rather than one whose places take whatever was written.
template<class T>
struct wrap {
  T v;
};

template<class... A>
int f(wrap<A>... p)
{
  return sizeof...(A);
}

int main()
{
  // 14.8.2.1p1: the first pair deduces `A` from `wrap<int>`, and the second is
  // an `int` against `wrap<A>`, which is not a specialization of `wrap`.
  return f(wrap<int>(), 2);
}
