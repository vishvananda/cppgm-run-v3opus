namespace A {
  namespace B { typedef double T; extern long q; namespace S { typedef char TS; } }
  using namespace B;
  inline namespace I {
    typedef int T;
    extern short q;
    inline namespace J { typedef unsigned U; }
  }
}
A::T v;
A::U u;
short A::q;
A::S::TS s;

namespace C { inline namespace K { typedef float TK; } }
namespace D { using namespace C; }
D::TK w;
