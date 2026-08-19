// 8.3.4p1 and 8.3.5p1: a bound and a parameter-clause bind tighter than the `*`
// or `&` of a declarator derived from what they belong to, so a spelling
// written from the *type* has to write the parentheses the program's own
// declarator wrote - and a cv-qualifier inside them stands closed up against
// what it qualifies where at the top level it stands apart.  It is the name
// every naming of one argument list reaches, so two arguments that differ only
// in which level a bound or a qualifier is at stay two names.
struct held { int m; };
template<class T> struct holder { static int counted; };
template<class T> int holder<T>::counted = 1;
int main()
{
  return holder<int (*)[2]>::counted + holder<int (*)()>::counted +
         holder<int (*)(int, char)>::counted +
         holder<int (*const)[2]>::counted + holder<int (*[2])[3]>::counted +
         holder<int held::*>::counted + holder<int *[2]>::counted - 7;
}
