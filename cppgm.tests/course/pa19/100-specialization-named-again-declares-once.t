// 14.7.1p1: a class template specialization is instantiated where it is used
// in a context requiring a completely-defined type and nowhere else, so
// naming it a second time and naming it through 7.1.3p2's alias-declaration
// each leave the declaration standing.
template<class T> struct holder {
  static const int factor = T::factor;
  int base;
  int scaled() { return base * factor; }
};

struct box;
typedef holder<box> named;
typedef holder<box> named_again;
using aliased = holder<box>;
struct box { static const int factor = 3; };

int main() {
  aliased a;
  a.base = 13;
  return a.scaled() == 39 ? 0 : 1;
}
