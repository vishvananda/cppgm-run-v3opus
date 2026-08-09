// VALIDATION: compile-fail
// N3485 focus: 14.6 [temp.res], 3.4.1 [basic.lookup.unqual], 3.3.2 [basic.scope.pdecl]
//
// 14.6p8 makes a template definition ill-formed where no valid specialization
// could be generated from it, so 3.4p1 looks up the names it writes that no
// template parameter stands in the way of - and an initializer is an
// expression of that definition exactly as the operand of a `return` is.  The
// name below is written in one, depends on no template parameter, and nothing
// declares it, so no argument list could make this template valid.

template<class T>
int settled(T v)
{
  int held = nowhere_at_all;

  return held;
}

int main()
{
  return 0;
}
