struct Counter {
  int n;
  Counter();
};

extern thread_local Counter tally;

int read_tally() {
  return tally.n;
}

thread_local Counter tally;

int main() {
  return read_tally();
}
