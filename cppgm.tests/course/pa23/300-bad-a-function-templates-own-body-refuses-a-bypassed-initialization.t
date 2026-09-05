// VALIDATION: compile-fail
// 6.7p3 with 14.6p8: a template definition is read for what its own statements
// say before any argument list arrives, and what a jump into one of its blocks
// would bypass is one of the things they say.  The declaration below writes an
// initializer and names a type no template parameter stands for, so the reading
// that has only the pattern is already the one that can refuse it - and the
// program is refused where the template is defined rather than where a
// deduction gives it an argument.

template<class T>
int run(T selector)
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
