// VALIDATION: run-pass
// 8.3.6p1 writes a default argument `= initializer-clause` and nothing else,
// so a parameter takes no brace-or-equal-initializer.  8.2p1's ambiguity is
// therefore settled for `holder made(seed{});`: `seed{}` is a
// parameter-declaration under no rule, and the run declares an object.

struct seed
{
  int written;
};

struct holder
{
  int held;
  holder(seed && from) : held(from.written) {}
  holder(seed && first, seed && second) : held(first.written + second.written) {}
};

int taking(seed from = {4})
{
  return from.written;
}

int main()
{
  holder one(seed{3});
  holder two(seed{3}, seed{4});
  return one.held + two.held + taking() == 14 ? 0 : 1;
}
