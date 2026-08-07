struct T { int a; unsigned b; };

int take(T value) { return value.a; }

int main() {
  int one = int{7};
  unsigned two = unsigned{9};
  return take(T{1, 2}) + one + static_cast<int>(two) - 17;
}
