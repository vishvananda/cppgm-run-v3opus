int constant_three(int) {
  return 3;
}

int constant_three(int ignored);

int main() {
  return constant_three(4) - 3;
}
