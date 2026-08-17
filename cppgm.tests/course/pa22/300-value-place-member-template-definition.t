// VALIDATION: compile-pass
// N3485 focus: 14.5.2 [temp.mem], 14.5.6.1 [temp.over.link], 14.1 [temp.param]
// A member template whose head declares a non-type parameter, defined outside
// the class.  14.5.6.1's equivalence compares the places two heads declared,
// and 14.1p4's value place is one only where the type it binds a value of
// agrees as well - which is what matches this definition to its declaration.

template<class T>
struct counter
{
  template<class I, int Step, bool Twice>
  int advance(I x) const;
};

template<class T>
template<class I, int Step, bool Twice>
int counter<T>::advance(I x) const
{
  return int(x) + (Twice ? Step + Step : Step);
}

int main()
{
  counter<int> c;
  return c.advance<int, 3, false>(1) == 4 &&
         c.advance<int, 3, true>(1) == 7 ? 0 : 1;
}
