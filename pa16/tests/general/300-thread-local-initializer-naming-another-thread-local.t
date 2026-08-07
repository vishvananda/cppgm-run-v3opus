struct Counter {
  int n;
  Counter();
};

extern thread_local Counter tally;

thread_local int seen = tally.n;

thread_local Counter tally;

int main() {
  return seen;
}
