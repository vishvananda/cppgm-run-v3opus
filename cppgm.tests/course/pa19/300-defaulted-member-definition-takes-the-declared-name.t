struct part {
  part();
  part(const part& copied);
  int held;
};

part::part() : held(2) {}
part::part(const part& copied) : held(copied.held + 1) {}

struct box {
  box();
  box(const box& source);
  part inner;
};

box::box() {}
box::box(const box&) = default;

int main() {
  box first;
  box second(first);
  return second.inner.held;
}
