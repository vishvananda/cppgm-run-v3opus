// VALIDATION: compile-pass
// 8.5.1p2 and 8.5.1p7: the clauses reach the elements of an array member in
// order and every element none reached is value-initialized - which for an
// element of class type is the constructor 8.5p8 gives it and for a scalar is
// the zero of its type.  The elements are built where the argument stands, one
// after another from the one base the storage was named by.

struct element
{
  element() {}
  element(int) {}
};

struct of_class
{
  element run[3];
};

struct of_scalars
{
  int run[4];
};

void take_class(of_class);
void take_scalars(of_scalars);

int main()
{
  take_class(of_class{element(1), element(2)});
  take_scalars(of_scalars{9});
  return 0;
}
