inline int counter() { static int seen = 0; return ++seen; }

template<int N> int slot() { static int held = N; return held; }

struct box { int keep() { static int inside = 3; return inside; } };

int pick(int) { static int one = 1; return one; }

int pick(double) { static int two = 2; return two; }

namespace deep { int down() { static int under = 4; return under; } }

int main()
{
  const int total = counter() + slot<1>() + slot<2>() + box().keep() +
                    pick(0) + pick(0.0) + deep::down();
  return total == 14 ? 0 : 1;
}
