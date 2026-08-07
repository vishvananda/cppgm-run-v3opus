// VALIDATION: compile-pass
struct adder
{
  int base;

  int add(int given) const { return base + given; }
  static int twice(int given) { return given + given; }
};

int free_add(int given) { return given + 1; }

int main()
{
  adder a;
  a.base = 2;
  return (a.add)(3) == 5 && (adder::twice)(2) == 4 && (free_add)(1) == 2
    ? 0 : 1;
}
