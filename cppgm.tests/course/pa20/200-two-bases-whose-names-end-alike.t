// N3485 focus: 12.6.2 [class.base.init] p2 and p6 with 14.6.2 [temp.dep] p3 -
// a mem-initializer-id names one base of the class, and two bases may be
// classes whose names end in the same component, so which subobject an entry
// initializes is the class the id names and not the last component of it.
// 14.6.2p3 is a fact of each base-specifier the same way: a name written in a
// template definition is looked up in the bases whose own specifier named a
// settled class, and left unlooked-up only in the ones an argument list settles.

namespace outer
{

struct part
{
  part(int value) : n(value) {}
  int n;
};

}

struct holder
{
  struct part
  {
    part(int value) : m(value) {}
    int m;
  };
};

struct both : outer::part, holder::part
{
  // 12.6.2p2: two entries, one per base, told apart by the class each id names.
  both() : outer::part(1), holder::part(2) {}
};

struct settled
{
  int of_settled;
};

template<class T>
struct over : settled, T
{
  // 14.6.2p3: `of_settled` is declared by a base no argument list settles, so
  // the definition finds it where it stands; a name of `T`'s is reached
  // through the object instead.
  int from_settled() { return of_settled; }
  int from_argument() { return this->of_argument; }
};

struct argument
{
  int of_argument;
};

int main()
{
  both one;
  // 10.2p2: each base declares a name of its own, so an unqualified lookup
  // reaches the member of the subobject that declares it.
  if (one.n != 1) { return 1; }
  if (one.m != 2) { return 2; }
  over<argument> two;
  two.of_settled = 3;
  two.of_argument = 4;
  if (two.from_settled() != 3) { return 3; }
  if (two.from_argument() != 4) { return 4; }
  return 0;
}
