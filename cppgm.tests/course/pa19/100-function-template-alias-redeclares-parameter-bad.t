// N3485 focus: 14.6.1 [temp.local] a template-parameter shall not be
// redeclared within its scope, including a body no instantiation ever reads.

template<class T>
int shadow()
{
  using T = int;
  return 0;
}
