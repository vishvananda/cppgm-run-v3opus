// 8.4.2p2 and 12.8p12: `= default` written outside the class settles what the
// standard's definition of that member comes to, and a class already completed
// *over* it read the answer that changed - so the class holding one, the class
// deriving from one and an array of them each carry the copy the bytes are.
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

struct Extends : Held {
  Extends(int v) : Held(v) {}
};

int main()
{
  Keeps first(3);
  Keeps second(first);
  Extends third(4);
  Extends fourth(third);
  return second.held.value + fourth.value - 7;
}
