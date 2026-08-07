int order;
int stop;

struct Part {
  int mark;
  Part(int m) : mark(m) { order = order * 10 + m; }
  ~Part() { order = order * 10 + mark; }
};

struct Pair {
  Part left;
  Part right;

  Pair() : left(1), right(2) {}

  ~Pair() {
    if (stop) {
      return;
    }
    order = order * 10 + 3;
  }
};

int main() {
  stop = 1;
  {
    Pair one;
  }
  return order == 1221 ? 0 : 1;
}
