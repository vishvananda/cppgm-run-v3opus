// VALIDATION: compile-pass
// 3.6.2p2: the constructor of an element of a namespace-scope array writes only
// what its arguments hold, so the array holds that image before the program
// runs rather than being built by a startup body.

struct Cell {
  int tag;
  int weight;

  Cell(int t, int w) : tag(t), weight(w) {}
};

Cell cells[2] = {{1, 10}, {2, 20}};

int main()
{
  return cells[0].tag == 1 && cells[0].weight == 10 && cells[1].tag == 2 &&
    cells[1].weight == 20 ? 0 : 1;
}
