int order;

struct Node {
  Node() { order = order * 3 + 1; }
  ~Node() { order = order * 3 + 2; }
};

struct Holder {
  Node row[4];
  Node last;
  Holder() : row{Node(), Node()}, last() {}
};

int main() {
  {
    Holder holder;
  }
  return order == 29645 ? 0 : 1;
}
