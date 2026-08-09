struct box {
  box& operator=(const box& source);
  int held;
};

box& box::operator=(const box&) = default;

int main() {
  box first;
  box second;
  first.held = 4;
  second = first;
  return second.held;
}
