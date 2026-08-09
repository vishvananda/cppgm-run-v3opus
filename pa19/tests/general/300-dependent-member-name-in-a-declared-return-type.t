// VALIDATION: compile-pass
// N3485 focus: 14.6.2 [temp.dep], 14.2 [temp.names]
//
// A name written after a prefix that depends on a template parameter is a
// member of a class only an argument list names.  The object file writes such a
// type as that prefix and then the name, so a function template whose declared
// return type is one is named for the two facts it wrote rather than for the
// parameter standing on its own - and every component after the prefix is a
// member of the one before it.

struct inner
{
  typedef int leaf;
};

struct outer
{
  typedef inner mid;
  int held;
};

template<class T>
typename T::mid::leaf take(T *from)
{
  return from->held;
}

int main()
{
  outer made;
  made.held = 0;
  return take(&made);
}
