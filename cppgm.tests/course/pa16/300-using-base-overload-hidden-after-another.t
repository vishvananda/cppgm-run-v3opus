// 7.3.3p14: a declaration a using-declaration brought in is one the class hides
// rather than one 13.1 redeclares, so it never enters the index that says which
// declaration a declarator redeclares - and a third overload written after two
// of them were hidden declares a function of its own.
struct hidden_chain_base {
  int reached() { return 1; }
  int reached(int given) { return given + 1; }
  int reached(double given) { return (int)given + 2; }
};

struct hidden_chain_derived : hidden_chain_base {
  using hidden_chain_base::reached;
  int reached() { return 5; }
  int reached(double given) { return (int)given + 7; }
};

int main() {
  hidden_chain_derived derived;
  return derived.reached() + derived.reached(1) + derived.reached(2.0) - 16;
}
