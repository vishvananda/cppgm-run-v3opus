struct part {
  part();
  part(const part& copied);
  int held;
};

part::part() : held(2) {}
part::part(const part& copied) : held(copied.held + 1) {}

struct box {
  box();
  box(const box&);
  part inner;
};

box::box() {}
box::box(const box& defined) = default;

int main() {
  box first;
  box second(first);
  return second.inner.held;
}
