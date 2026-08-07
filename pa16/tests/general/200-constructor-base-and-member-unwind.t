int order;

struct Base {
  Base() { order = order * 10 + 1; }
  ~Base() { order = order * 10 + 4; }
};

struct Member {
  Member() { order = order * 10 + 2; }
  ~Member() { order = order * 10 + 5; }
};

struct Derived : Base {
  Member held;
  Derived() { order = order * 10 + 3; }
};

int main() {
  {
    Derived value;
  }
  return order == 12354 ? 0 : 1;
}
