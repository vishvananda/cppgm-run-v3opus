// VALIDATION: compile-pass
// 8.5.1p1 with 5.2.3p2/p3 and 12.6.2: a class that declares a constructor of
// its own is no aggregate, so `T(x)` at a value place is 8.5p16's
// direct-initialization and the object it comes to is what the constructor's
// mem-initializers hold - read in declaration order, with a member of class
// type built by its own constructor and the whole answer reached through
// 12.3.2p1's conversion function.

struct inner
{
  int doubled;
  constexpr inner(int x) : doubled(x * 2) {}
};

struct held
{
  inner part;
  int tail;
  constexpr held(int x) : part(x), tail(x + 1) {}
  constexpr operator int() const { return part.doubled * 100 + tail; }
};

struct later
{
  int scaled;
  constexpr later(int x);
  constexpr operator int() const { return scaled; }
};

constexpr later::later(int x) : scaled(x * 5) {}

struct nothing
{
  int only;
  constexpr nothing() : only(9) {}
  constexpr operator int() const { return only; }
};

template<int N>
struct box
{
  static const int value = N;
};

static_assert(box<held(2)>::value == 403, "");
static_assert(box<later(3)>::value == 15, "");
static_assert(box<nothing()>::value == 9, "");

int main()
{
  return box<held(1)>::value == 202 ? 0 : 1;
}
