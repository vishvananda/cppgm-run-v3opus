// N3485 focus: 14.8.1 [temp.arg.explicit] a template-id may write only a
// leading part of the argument list and leave the rest to the call, and the
// written part is substituted before the pairs are deduced - so a parameter it
// made non-dependent takes 13.3's conversion rather than a deduction that
// would refuse the argument.

template<class First, class Second>
int blend(First one, Second two)
{
  return one + two;
}

int main()
{
  long wide = 40;
  return blend<int>(wide, 2) - 42;
}
