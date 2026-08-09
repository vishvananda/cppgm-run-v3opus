// N3485 focus: 6.4 [stmt.select] and 6.5 [stmt.iter] - a condition the
// lowering wrote as a literal is the jump one of the branch's edges is, and an
// operand of 5.14's `&&` that is one leaves the operator standing for its other
// operand rather than opening a block for it.

int side()
{
  return 1;
}

int chosen()
{
  if (true)
  {
    return 1;
  }
  return 0;
}

int skipped()
{
  if (false)
  {
    return 1;
  }
  return 2;
}

int looped()
{
  while (true)
  {
    return 3;
  }
  return 0;
}

int once()
{
  do
  {
    return 4;
  }
  while (false);
}

int left_true()
{
  if (true && side())
  {
    return 5;
  }
  return 0;
}

int left_false()
{
  if (false && side())
  {
    return 6;
  }
  return 7;
}

int computed()
{
  // 5.19 is not what this asks: the value is one an instruction computes, so
  // the branch stands.
  if (1 == 1)
  {
    return 8;
  }
  return 0;
}

int main()
{
  return chosen() + skipped() + looped() + once() + left_true() +
         left_false() + computed() - 30;
}
