// VALIDATION: compile-pass
// 8.3.6p1 with 7.1.5p2: a call that stops short of a place the declaration
// gave a default-argument is read as if the argument stood there, so the fold
// fills it - which makes the call at namespace scope a value the image holds
// and not one the program runs.

constexpr int spread(int a, int b = 2, int c = 3) { return a * 100 + b * 10 + c; }

template<int N>
struct box
{
  static const int value = N;
};

static const int folded = spread(1);

static_assert(spread(1, 5) == 153, "");
static_assert(box<spread(4)>::value == 423, "");

int main()
{
  return folded == 123 ? 0 : 1;
}
