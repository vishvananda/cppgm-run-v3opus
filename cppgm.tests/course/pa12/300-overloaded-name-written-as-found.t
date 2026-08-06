// N3485 focus: 3.4.3.2 [namespace.qual], 13.4 [over.over]
// An id-expression is written the way the program spelled it, while the callee
// of a call is written with the name its declaration has.
namespace N { void f(int); void f(char); }
using namespace N;
void take(void(*)(int));
void use() { take(&f); N::f(1); }
