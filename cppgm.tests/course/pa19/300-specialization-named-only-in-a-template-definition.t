// VALIDATION: compile-pass
// N3485 focus: 14.6 [temp.res], 14.7.1 [temp.inst], 14.5.1.3 [temp.mem.func]
//
// 14.7.1p1 makes a template-id written in a template definition being read for
// what 14.6p8 says about it a declaration rather than a use requiring one, so
// the specialization it names is one no instantiation asked for.  A later
// declaration is read for the specializations that *were* asked for -
// 14.5.1.3p1's out-of-class member definition below is read for each of them,
// and so is the definition the template itself gets - and a specialization
// only the reading of a pattern ever named is on neither list.
//
// `mark<int>` is named once, in a body no call instantiates, and the program
// asks for `mark<char>` alone.

template<class T>
struct mark
{
  T held;

  int size();
};

template<class U>
int uninstantiated(U v)
{
  mark<int> *never = 0;

  return never == 0 ? 0 : 1;
}

template<class T>
int mark<T>::size()
{
  return sizeof(T);
}

int main()
{
  mark<char> asked;

  asked.held = 0;

  return asked.size() - 1;
}
