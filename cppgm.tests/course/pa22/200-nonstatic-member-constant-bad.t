// VALIDATION: compile-fail
// N3485 focus: 5.1.1 [expr.prim.general], 5.19 [expr.const]
// An id-expression that denotes a non-static data member is written as part of
// a class member access, to form a pointer to member, or in an unevaluated
// operand.  A constant expression that names one on its own has no object it
// is a member of.

struct owner
{
  const int value = 1;
};

static const int rejected = owner::value;

int main()
{
  return rejected;
}
