// 13.3: what a using-declaration brought in is ranked beside the derived
// class's own declarations, so an exact match on the brought-in one wins over
// a conversion on the class's own.
struct base_ranked {
  int f(int x) { return 1; }
};

struct derived_ranked : base_ranked {
  using base_ranked::f;
  int f(long x) { return 2; }
};

int main() {
  derived_ranked d;
  return d.f(1) + d.f(1L) - 3;
}
