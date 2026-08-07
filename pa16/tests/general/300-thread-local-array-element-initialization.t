struct Cell {
  int v;
  Cell() : v(3) {}
};

thread_local Cell row[2];

int main() {
  return row[0].v - row[1].v;
}
