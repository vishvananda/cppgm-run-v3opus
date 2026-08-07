int order;

struct Trace {
  int mark;
  Trace(int m) : mark(m) { order = order * 10 + m; }
  ~Trace() { order = order * 10 + 9; }
};

struct Plain {
  Plain() {}
};

struct Holder {
  Trace first;
  Plain plain;
  int count;
  Trace second;

  Holder() : first(1), count(7), second(2) {}
};

int main() {
  {
    Holder held;
    order = order * 10 + held.count;
  }
  return order == 1279 * 10 + 9 ? 0 : 1;
}
