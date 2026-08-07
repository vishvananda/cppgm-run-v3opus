int order;

struct Value {
  int held;
  Value(int given) { held = given; order = order * 3 + 1; }
  ~Value() { order = order * 3 + 2; }
};

struct Mark {
  Mark() { order = order * 3 + 1; }
  ~Mark() { order = order * 3 + 2; }
};

struct Holder {
  Value row[3];
  Mark last;
  Holder() : row{Value(1), Value(2), Value(3)}, last() {}
};

int main() {
  {
    Holder holder;
  }
  return order == 3320 ? 0 : 1;
}
