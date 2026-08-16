// VALIDATION: compile-pass
// 7.1.5p3: the typedefs, alias-declarations and static_asserts a constexpr
// function's body may write around its one return statement, read in a region
// that binds the parameters to what the arguments came to.

typedef unsigned long size_type;

constexpr size_type spread(size_type a, int b)
{
  typedef int held;
  using again = held;
  static_assert(sizeof(again) == 4, "");
  return a * sizeof(again) + b;
}

template<size_type N>
struct box
{
  static const size_type value = N;
};

static_assert(spread(2, 3) == 11, "");
static_assert(box<spread(4, 1)>::value == 17, "");

int main()
{
  return 0;
}
