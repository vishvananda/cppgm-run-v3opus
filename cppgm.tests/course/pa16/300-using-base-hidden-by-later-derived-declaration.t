// 7.3.3p14: a member function the derived class declares hides the one a
// using-declaration written before it brought in with the same parameter list,
// rather than conflicting with it.
struct base_hidden_later {
  int f() { return 1; }
  int f(int x) { return x; }
};

struct derived_hidden_later : base_hidden_later {
  using base_hidden_later::f;
  int f(int x) { return 9; }
};

int main() {
  derived_hidden_later d;
  return d.f() + d.f(2) - 10;
}
