// VALIDATION: run-pass
// 6.7p3 asked of a member of a class template, which is read twice: once as the
// pattern the class body holds and again for the argument list a use gives it.
// The braces below are what make the program well formed - the declaration they
// hold belongs to a block that has closed by the time the next label stands -
// and both readings have to agree about that, because the pattern's own reading
// opens the same regions the instantiation's does.
//
// 6.4.2p1's other requirement stands beside it: a label reached by no switch is
// refused, and the ones here are each inside the statement that jumps to them.

template<class T>
struct dispatcher
{
  T run(T selector)
  {
    switch (selector)
    {
    case 0:
      {
        T counted = selector + 1;
        return counted;
      }
    case 1:
    case 2:
      return selector * 10;
    default:
      return selector;
    }
  }
};

int main()
{
  dispatcher<int> made;
  return made.run(0) == 1 && made.run(2) == 20 && made.run(7) == 7 ? 0 : 1;
}
