int seed();

void tick() {
  thread_local int count = seed();
  count = count + 1;
}

int main() {
  tick();
  tick();
  return 0;
}
