// N3485 focus: 7.1.6.2 [dcl.type.simple] an unparenthesized class member access
// names an entity as much as an id-expression does, so the specifier stands for
// the type that member was declared with rather than for the lvalue the access
// is - which is the return type an instantiation writes into the object file.

struct cell
{
  int slot;
};

template<class T>
auto peek(T box) -> decltype(box.slot)
{
  return box.slot;
}

int main()
{
  cell one;
  one.slot = 42;
  decltype(one.slot) copy = 0;
  copy = peek(one);
  return copy - 42;
}
