// N3485 focus: 5.16p3, 5.16p4 [expr.cond], 8.5.3 [dcl.init.ref]
// Each operand of a conditional expression is converted to an lvalue reference
// to the other's type, and the one conversion that binds says what the result
// denotes.  Two lvalues of one type both bind; two whose types differ only in
// cv-qualification bind one way only, and the result is the lvalue of the more
// qualified of the two.
int plain;
const int qualified;
int use(bool choose)
{
  int& same = choose ? plain : plain;
  const int& more = choose ? plain : qualified;
  const int& other = choose ? qualified : plain;
  int value = choose ? plain : 3;
  return same + more + other + value;
}
