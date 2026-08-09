// N3485 focus: 8.2 [dcl.ambig.res] parentheses around a name in a
// parameter-declaration hold a parameter-clause where that name is a type-name
// and the parameter's own declarator-id where it is not, which 3.4 answers and
// the grammar cannot.

namespace shapes
{
template<class T>
struct crate
{
  T held;
};
}

int weigh(shapes::crate<int>(load))
{
  return load.held;
}

int main()
{
  shapes::crate<int> one;
  one.held = 42;
  return weigh(one) - 42;
}
