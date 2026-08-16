// VALIDATION: compile-pass
// 5.2.3p3 with 12.3.2p1: an object of literal class type written at a value
// place, whose braced list holds a comma that belongs to the initializer and
// not to the template-argument-list, reaching the place through a constexpr
// conversion function that reads the object's members.

struct pair
{
  int high;
  int low;
  constexpr operator int() const { return high * 10 + low; }
};

struct wrapped
{
  pair held;
  constexpr operator int() const { return 1; }
};

template<int N>
struct box
{
  static const int value = N;
};

static_assert(box<pair{3, 4}>::value == 34, "");
static_assert(box<pair{}>::value == 0, "");
static_assert(box<wrapped{}>::value == 1, "");

int main()
{
  return box<pair{1, 2}>::value == 12 ? 0 : 1;
}
