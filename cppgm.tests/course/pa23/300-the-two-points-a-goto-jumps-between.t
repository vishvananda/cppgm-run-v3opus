// VALIDATION: run-pass
// 6.7p3 read as the pair of points it is: a goto is refused where a variable is
// in scope where it lands and not where it stands.  That is one question about
// the regions open at each end - in the innermost region both of them stand in,
// what was declared between the two, and in every region the jump *enters*,
// whatever that region had declared above the label.
//
// Neither half answers yes for the three shapes below.  A label written above
// the declarations of the block it opens is reached by a jump before any of
// them; a declaration with no initializer is one 6.7p3 exempts wherever it
// stands; and a goto that lands above a declaration of the block it is written
// in *leaves* that region rather than entering it, so the initialization it
// jumped back over simply runs again.

int run(int selector)
{
  int total = 0;
  if (selector == 0)
  {
    goto entry;
  }
  if (selector == 1)
  {
    goto uninitialized;
  }
  {
  entry:
    int counted = 3;
    total = total + counted;
  }
  {
    int held;
  uninitialized:
    held = 5;
    total = total + held;
  }
again:
  int rounds = 1;
  total = total + rounds;
  if (total < 10)
  {
    goto again;
  }
  return total;
}

int main()
{
  return run(0) == 10 && run(1) == 10 && run(2) == 10 ? 0 : 1;
}
