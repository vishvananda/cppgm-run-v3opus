// A floating value is a constant this translation computes with: 5p10 brings
// two operands to one of 3.9.1p8's floating types, 4.8 rounds the answer to the
// width the object has, and 3.6.2p2's image holds that value rather than the
// digits some operand was written with.

constexpr float narrowed(double x) { return x; }
constexpr double scaled(double x) { double t = x; for (int i = 0; i < 3; ++i) { t = t / 2.0; } return t; }

constexpr float rounded = narrowed(16777217.0);
constexpr double eighth = scaled(1.0);
constexpr long double wide = 1.0L / 4.0L;
constexpr long double elements[] = {0.5L, 1.5L, 2.5L};

static_assert(rounded == 16777216.0f, "the initializer rounds to the object");
static_assert(eighth == 0.125, "a loop over floating values");
static_assert(wide > 0.2L && wide < 0.3L, "a long double quotient");
static_assert(elements[2] == 2.5L, "an element of a constant array");
static_assert(16777217 == 16777216.0f, "5p10 converts the integral operand");
static_assert(18446744073709551615ULL > 1.0e19L, "the magnitude is preserved");

int main()
{
  return 0;
}
