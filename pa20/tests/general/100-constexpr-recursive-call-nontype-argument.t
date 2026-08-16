// VALIDATION: compile-pass
// 7.1.5p3 and 14.3.2p1: a constexpr function that calls itself, read once per
// distinct argument list, with the answer written at a non-type place.

constexpr unsigned long fac(unsigned long n)
{
  return n <= 1 ? 1 : n * fac(n - 1);
}

template<unsigned long N>
struct box
{
  static const unsigned long value = N;
};

static_assert(fac(10) == 3628800UL, "");
static_assert(box<fac(12)>::value == 479001600UL, "");

int main()
{
  return box<fac(5)>::value == 120 ? 0 : 1;
}
