// VALIDATION: compile-fail
// 6.7p3: it is possible to transfer into a block, but not in a way that
// bypasses a declaration with initialization.  A switch statement reaches each
// of its labels by a jump from its own condition, so a `default` written below
// a declaration the case above it made is a jump from a point where that
// variable is not in scope to a point where it is - and 6.7p3 exempts nothing
// about this one, because it was declared with an initializer.
//
// The blocks between the two ends are what the question is asked of, and not
// the declarations they hold: the label asks each of them whether it has
// declared anything a jump may not bypass, which is a count the region carries.

int run(int selector)
{
  switch (selector)
  {
  case 0:
    int counted = 1;
    return counted;
  default:
    return 2;
  }
}

int main()
{
  return run(1);
}
