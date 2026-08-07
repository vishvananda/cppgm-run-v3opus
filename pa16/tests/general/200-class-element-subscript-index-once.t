// VALIDATION: compile-pass
// 5.2.1p1: the subscript of an array whose element has no register width is one
// expression, read once, whose value the byte count of an element scales.

struct Cell {
  int first;
  int second;
};

int pick(Cell * cells, int at)
{
  return cells[at].second;
}

int main()
{
  Cell cells[2] = {{1, 2}, {3, 4}};
  return pick(cells, 1) == 4 ? 0 : 1;
}
