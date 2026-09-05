// VALIDATION: run-pass
// 6.7p3 names what a jump into a block is allowed to skip: a variable of scalar
// type, of a class type with a trivial default constructor and a trivial
// destructor, of a cv-qualified one of those, or of an array of them - and
// declared with no initializer.  Nothing runs where such a declaration stands,
// so the object it names is one the program can only read after storing into
// it, which is what makes the jump harmless and is why the same declarations
// written *with* an initializer are not.
//
// The other three shapes here are the same sentence read from outside the
// blocks the jump enters.  A declaration the block around the switch made is in
// scope at the jump's own origin; one a block that has already closed made is
// in scope at neither end of it; and 6.4p3's condition declares into the region
// the jump comes from.  So the count a label asks for is each region's own, and
// only the regions standing between the condition and the label are asked.

struct flat
{
  int first;
  int second;
};

int run(int selector)
{
  int outer = 4;
  switch (int settled = selector)
  {
  case 0:
    {
      int closed = 8;
      outer = outer + closed - 8;
    }
    int scalar;
    int elements[2];
    flat trivial;
    scalar = outer;
    elements[0] = 1;
    trivial.first = 2;
    return scalar + elements[0] + trivial.first;
  default:
    return outer + settled;
  }
}

int main()
{
  return run(0) == 7 && run(1) == 5 ? 0 : 1;
}
