// VALIDATION: compile-pass
// N3485 focus: 8.5.3 [dcl.init.ref], 1.4 [intro.compliance], 5.2.2 [expr.call]
//
// 8.5.3p5: where a reference binds a prvalue, what it binds is a temporary of
// the *referenced* type, initialized from that value - so the storage the
// function gives the temporary is the storage the reference reads through, and
// the two have to be one type.  Usually nothing has to ask: a conversion the
// initialization needed stands in the tree as a line of its own, and the value
// arrives already at the referenced type.
//
// 1.4p8's branch hint is where they part.  A call of it is worth its first
// operand and computes nothing, so what the body lowers is the operand's own
// reading - at the operand's width, whatever 5.2.2p10 says the call's type is.
// A reference bound to it therefore names storage the operand's width laid out
// and reads it at the width the reference was declared with, which is one
// object of two sizes unless the initialization of the temporary is where the
// conversion is asked.

long counted(long held)
{
  const long &bound = __builtin_expect(held, 0);
  return bound;
}

// 8.5.3p5 again where the initialization is 3.6.2p2's: the temporary the
// reference binds is one the startup body lays out, and the operand written for
// it is an `int` where the reference reads a `long`.

const long &held_globally = __builtin_expect(6, 0);

int main()
{
  return static_cast<int>(counted(3) + held_globally);
}
