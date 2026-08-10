struct base_of {
  base_of(int);
  int held;
};

struct derived : base_of {
  using base_of::base_of;
};

base_of::base_of(int given) : held(given + 1) {}

int main() {
  derived made(4);
  return made.held;
}
