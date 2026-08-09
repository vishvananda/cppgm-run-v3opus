// 3.4.3.1p2: in a using-declaration that is a member-declaration, a name that
// is the last component of its own nested-name-specifier names the
// constructors of the class that specifier nominates, whatever that component
// was spelled as.
struct Base { int held; explicit Base(int n) : held(n) {} };

struct Derived : Base {
  typedef Base Alias;
  using Alias::Alias;
};

int main() { Derived d(7); return d.held == 7 ? 0 : 1; }
