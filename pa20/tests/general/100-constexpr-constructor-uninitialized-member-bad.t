// 7.1.5p4: every non-static data member of a class a constexpr constructor
// builds shall be initialized by one of its mem-initializers, so the object
// below is no constant expression and the value place has no argument.

struct partial
{
  int named;
  int left;
  constexpr partial(int x) : named(x) {}
  constexpr operator int() const { return named; }
};

template<int N>
struct box
{
  static const int value = N;
};

int main()
{
  return box<partial(1)>::value;
}
