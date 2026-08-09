// 3.9p5 over an expression: 13.3.1.2p3's member candidates are looked up in
// the class of the left operand, so an expression of a specialization's type
// is a context that requires it completely defined - which a reference
// parameter naming the same specialization is not.
template<class T> struct holder {
  int base;
  int operator+(int n) { return base + n * T::factor; }
};

struct box { static const int factor = 3; };
typedef holder<box> named;

int add(named & h) { return h + 3; }

holder<box> storage;

int main() {
  storage.base = 30;
  return add(storage) == 39 ? 0 : 1;
}
