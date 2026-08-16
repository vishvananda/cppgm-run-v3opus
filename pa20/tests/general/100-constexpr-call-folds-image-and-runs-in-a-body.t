// VALIDATION: compile-pass
// 7.1.5p2 with 3.6.2p2: a call of a constexpr function written outside every
// body is the value the program image holds, and the same call written inside
// one is a call the program makes - so the definition is emitted for the second
// and not for the first.

constexpr int twice(int a) { return a + a; }

struct holder
{
  static constexpr int limit() { return 7; }
};

static const int folded = twice(holder::limit());

int main(int argc, char ** argv)
{
  (void)argv;
  return folded == 14 && twice(argc) >= 2 ? 0 : 1;
}
