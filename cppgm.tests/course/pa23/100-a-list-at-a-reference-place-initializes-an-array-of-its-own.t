// VALIDATION: run-pass
// 8.5.3p5 at a braced-init-list written where a reference binds.  The list
// initializes a temporary of the type the reference *refers to*, so the object
// the clauses reach is an array wherever that type is one - and the fact the
// list leaves behind has to carry the array and not the reference, or every
// reader below asks the wrong question.  A reader that asked the reference took
// the list for a scalar, kept its first clause alone, and handed the array's
// whole storage that one value: three clauses written and one stored, at a
// width no conversion reaches.
//
// The four doors one list has to that place are a plain reference to an array,
// a deduced bound, an element that is itself an array, and the empty list a
// bound of its own counts - and beside them the argument that *names* an array,
// which binds the object it names and stands up no storage at all.

int third(int const (&a)[3])
{
  return a[2];
}

int inner(int const (&a)[2][2])
{
  return a[1][1];
}

template<class T, unsigned N>
int last(T const (&a)[N])
{
  return a[N - 1];
}

template<class T>
int width(T const &, unsigned char const (&)[sizeof(T)])
{
  return sizeof(T);
}

int main()
{
  int named[3] = { 7, 8, 9 };
  const int one = third({ 1, 2, 3 });
  const int two = inner({ { 1, 2 }, { 3, 4 } });
  const int three = last<int>({ 5, 6, 7 });
  const int four = width<int>(0, {});
  const int five = third(named);
  return one + two + three + four + five == 3 + 4 + 7 + 4 + 9 ? 0 : 1;
}
