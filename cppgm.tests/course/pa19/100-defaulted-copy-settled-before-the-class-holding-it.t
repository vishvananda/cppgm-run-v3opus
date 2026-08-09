// 8.4.2p2 read the other way round: a class written *after* the definition
// outside the class reads the answer that definition already settled, so one
// program is not read two ways by where its classes stand.
struct Held {
  int value;

  Held(int v) : value(v) {}
  Held(const Held &);
};

inline Held::Held(const Held &) = default;

struct Keeps {
  Held held;

  Keeps(int v) : held(v) {}
};

int main()
{
  Keeps first(3);
  Keeps second(first);
  return second.held.value - 3;
}
