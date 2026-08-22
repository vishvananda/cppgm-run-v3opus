// N3485 focus: 5.3.7 [expr.unary.noexcept] the token a conversion was not written with
int thrower();
int calm() throw();

int at_reference(const long &v) throw();
int at_rvalue_reference(long &&v) throw();
int at_value(long v) throw();

typedef int scalar;
scalar counted = 3;

int main() {
  // 5.3.7p3: each of these calls is `throw()`, so what the answer turns on is
  // the operand under it.  8.5.3p5 materializes a temporary for the reference
  // to bind, and that line is a conversion the program wrote no operator on -
  // so the `~` belongs to the operand below it and to nothing else.  A walk
  // that read the token off the conversion would stop at 5.2.4p2's exit and
  // answer `true` for a call that throws.
  if (noexcept(at_reference(~thrower()))) { return 1; }
  if (noexcept(at_rvalue_reference(~thrower()))) { return 2; }
  if (noexcept(at_value(~thrower()))) { return 3; }
  if (noexcept(at_reference(-thrower()))) { return 4; }
  if (noexcept(at_reference(thrower()))) { return 5; }

  if (!noexcept(at_reference(~calm()))) { return 6; }
  if (!noexcept(at_rvalue_reference(~calm()))) { return 7; }

  // 5.2.4p2: the one line whose own token the walk reads, which stops there.
  scalar *risky = &counted;
  if (!noexcept(risky->~scalar())) { return 8; }
  return 0;
}
