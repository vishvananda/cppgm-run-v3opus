// VALIDATION: compile-fail
// N3485 focus: 5.2.9 [expr.static.cast], 3.9.1 [basic.fundamental]
//
// 5.2.9p4 says a cast to a type other than cv `void` is the
// direct-initialization of a temporary of that type from the operand, and
// 3.9.1p9 leaves an expression of type `void` no value to initialize one with.
// So the conversion to `void` runs one way only: `(void)0` is the
// discarded-value expression 5.18p1's left operand and 5.16p2's arm of `void`
// type are written as, and reading it back through a cast to a value type
// reaches nothing.
//
// The requirement stands on the cast rather than on what a fold makes of it:
// the program below writes no constant expression at all, and it is ill-formed
// all the same.

int measured()
{
  return (int)((void)0);
}

int main()
{
  return measured();
}
