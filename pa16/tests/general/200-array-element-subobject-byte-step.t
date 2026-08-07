struct Cell { int a; int b; };

struct Grid { Cell cells[2]; int k; };

int main() {
  Grid g = { {{1, 2}, {3, 4}}, 5 };
  return g.cells[0].a + g.cells[1].b + g.k - 10;
}
