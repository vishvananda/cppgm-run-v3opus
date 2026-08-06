// N3485 focus: 14.2 [temp.names], 8.2 [dcl.ambig.res]
// A template-id of a function template names an overload set rather than a
// type, so `f(x)` and `f<int>(x)` are calls of it and not declarations of `x`,
// however the argument is written.
template<class T> T id(T);
void use()
{
  int a = 1;
  0, id(a);
  0, id<int>(a);
  0, id((a));
}
