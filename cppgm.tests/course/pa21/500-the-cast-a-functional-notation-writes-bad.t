// VALIDATION: compile-fail
// N3485 focus: 5.2.3 [expr.type.conv], 5.2.9 [expr.static.cast], 3.9.1 [basic.fundamental]
//
// 5.2.3p1 says that a simple-type-specifier followed by a parenthesized
// expression is equivalent to the corresponding cast expression, so the two
// spellings are one construct and 5.2.9p4 is written about both of them: the
// cast is the direct-initialization of a temporary of the target's type from
// the operand, and 3.9.1p9 leaves an expression of type `void` no value to
// initialize one with.
//
// So `int(e)` over a `void` operand reaches nothing, exactly as `(int)e` does,
// and the reading has to stand where the two doors meet rather than at either
// of them - which is what the program below asks for: the cast is written in
// the functional notation, and nothing about it is a constant expression.

void report();

int measured()
{
  return int((void)report());
}

int main()
{
  return measured();
}
