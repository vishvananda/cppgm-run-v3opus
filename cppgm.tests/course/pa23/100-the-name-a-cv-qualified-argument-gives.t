// 7.1.6.1p1: `const int` and `int const` are one type, and a name written from
// the type has one spelling of it - the trailing one, which is the only side a
// derived declarator can write a cv-qualifier on.  So the internal name of a
// specialization named over a qualified argument is spelled the one way at
// every level of the declarator, and two arguments that differ only in which
// level carries the qualifier stay two names.
template<class T> struct holder
{
  static int counted;
};
template<class T> int holder<T>::counted = 1;
int main()
{
  return holder<const int>::counted + holder<int>::counted +
         holder<int *const>::counted + holder<const int *>::counted +
         holder<const int *[2]>::counted - 5;
}
