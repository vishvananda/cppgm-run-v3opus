// N3485 focus: 7.3.2 [namespace.alias], 7.3.3 [namespace.udecl],
// 7.3.4 [namespace.udir], 8.2 [dcl.ambig.res]
// A using-declaration, a using-directive and a namespace alias each make a
// declaration reachable under a spelling other than the one it was written
// with.  A call of a function reached through any of the three is a call and
// not a declaration of its argument.
namespace outer
{
  void plain(int);
  template<class T> T pattern(T);
  namespace inner { void nested(int); }
}
using outer::plain;
using namespace outer;
namespace shorthand = outer::inner;
void use()
{
  int a = 1;
  0, plain(a);
  0, pattern(a);
  0, shorthand::nested(a);
  0, inner::nested(a);
}
