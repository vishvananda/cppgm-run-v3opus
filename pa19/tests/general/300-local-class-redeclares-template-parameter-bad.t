// VALIDATION: compile-fail
// N3485 focus: 14.6.1 [temp.local], 9.8 [class.local]
//
// 14.6.1p6 refuses a redeclaration of a template-parameter anywhere in the
// template, which is every region nested in the one its head declared it in
// and every declaration that binds a name there - not only the typedef and the
// object a first reading of the rule reaches.  A class 9.8p1 makes local to
// the body of a function template binds one of those names.

template<class T>
int shadow()
{
  struct T
  {
    int held;
  };

  T made;

  made.held = 0;

  return made.held;
}

int main()
{
  return shadow<int>();
}
