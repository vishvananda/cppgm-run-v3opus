struct Counter {
  int n;
  Counter() : n(1) {}
};

thread_local Counter tally;

int main() {
  return tally.n + tally.n - 2;
}
