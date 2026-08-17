// VALIDATION: compile-fail
// N3485 focus: 9.5 [class.union], 8.5.1 [dcl.init.aggr], 5.19 [expr.const]
//
// 8.5.1p15 initializes a union from the first member the braces reach and no
// other, and 9.5p1 leaves at most one member of a union holding a value at a
// time.  So a constant of union type holds *that* member, and every other one
// of them holds nothing a constant expression may read - 5.19p2's
// lvalue-to-rvalue conversion has no value to read there, and 8.5.1p7's
// value-initialized tail, which is what an aggregate's members past the last
// clause hold, is no part of a union at all.
//
// The read below is therefore the program's own error rather than a value: an
// object holding a zero would answer it, which is exactly what a list padded
// out to the members a union declares would have made of it.

union holder
{
  int counted;
  double measured;
};

constexpr holder held = { 5 };
constexpr double read_back = held.measured;

int main()
{
  return static_cast<int>(read_back);
}
