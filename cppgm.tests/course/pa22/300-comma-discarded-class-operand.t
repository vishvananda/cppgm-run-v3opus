// VALIDATION: compile-pass
// N3485 focus: 5.18 [expr.comma], 5 [expr]
//
// 5.18p1's left operand is a discarded-value expression exactly as 6.2p1's
// statement and 5.2.9p4's cast to `void` are, so 5p11 leaves an object of class
// type it is worth standing in storage of the function's and 8.5.3p5 names that
// storage after the discarding.  p1 also makes the *result* the right operand,
// so a comma written left of another comma hands its object on to be discarded
// there - which is the same walk the cast to `void` already took.

struct mark
{
  int seen;
  mark() { seen = 1; }
};

struct tag
{
};

mark make_mark()
{
  return mark();
}

template<class T>
int run()
{
  int cells[] = { (T(), 0), (T(), tag(), 1), ((void)T(), 2) };
  return cells[0] + cells[1] + cells[2];
}

int main()
{
  int a = (mark(), 4);
  int b = (make_mark(), 5);
  int c = (mark(), tag(), 6);
  return run<mark>() + a + b + c - 18;
}
