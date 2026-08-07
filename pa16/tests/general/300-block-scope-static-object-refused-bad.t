void tick() {
  static int count = 0;
  count = count + 1;
}

int main() {
  tick();
  tick();
  return 0;
}
