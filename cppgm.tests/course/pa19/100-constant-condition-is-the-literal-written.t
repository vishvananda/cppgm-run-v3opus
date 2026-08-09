// N3485 focus: 6.4 [stmt.select], 5.14 [expr.log.and] and 5.19 [expr.const] -
// a condition the branch is the jump one of its edges is, is a condition that
// *is* a literal.  An expression over one is not: a cast, a functional cast and
// a comma expression each leave a literal standing as the value the terminator
// tests, and the terminator stands.  And the block 5.14's right operand would
// be read in is not reserved at all where the operand before it says which edge
// is taken, because the blocks of a body are numbered by the order they were
// asked for.

int side()
{
  return 1;
}

int written(int p)
{
  if (true)
  {
    p = p + 1;
  }
  if (side())
  {
    p = p + 2;
  }
  return p;
}

int cast(int p)
{
  if ((int)1)
  {
    p = p + 1;
  }
  return p;
}

int functional(int p)
{
  if (int(1))
  {
    p = p + 1;
  }
  return p;
}

int pointer(int p)
{
  if ((int *)0)
  {
    p = p + 1;
  }
  return p;
}

int comma(int p)
{
  if (((void)0, 1))
  {
    p = p + 1;
  }
  return p;
}

int value_initialized(int p)
{
  // 8.5p7's zero the initialization is, which is the literal the expression is
  // rather than one over it.
  if (int())
  {
    p = p + 1;
  }
  return p;
}

int stopped(int p)
{
  if (false && side())
  {
    p = p + 1;
  }
  if (side())
  {
    p = p + 2;
  }
  return p;
}

int carried(int p)
{
  if (true && side())
  {
    p = p + 1;
  }
  if (side())
  {
    p = p + 2;
  }
  return p;
}

int nested(int p)
{
  if (true && true && side())
  {
    p = p + 1;
  }
  if (side())
  {
    p = p + 2;
  }
  return p;
}

int operand_over_a_literal(int p)
{
  if (true && ((void)0, 1))
  {
    p = p + 1;
  }
  return p;
}

int looped(int p)
{
  while (int(1))
  {
    p = p + 1;
    break;
  }
  return p;
}

typedef int * address;

int named_value(int p)
{
  // 5.14p1 where the value is named: an operand that folds and does not decide
  // the value leaves the operator worth its other one, and a literal of pointer
  // type - whose value is spelled where an address is wanted - decides nothing.
  int carried = (true && side()) + (false || side());
  int addressed = address() && side();
  return p + carried + addressed;
}

int main()
{
  return written(1) + cast(1) + functional(1) + pointer(1) + comma(1) +
         value_initialized(1) + stopped(1) + carried(1) + nested(1) +
         operand_over_a_literal(1) + looped(1) + named_value(1) - 32;
}
