// 12.9p1 and 12.9p2: the candidate set of inherited constructors holds the
// base's own parameter-type-list and the shorter ones that result from omitting
// its defaulted parameters from the end, and 12.9p2's constructor
// characteristics carry no default-argument of their own - so a class that
// inherits a constructor with one still gets 12.1p5's default constructor and
// nothing is ambiguous between them.
struct default_argument_base {
  int held;
  default_argument_base(int given = 7) : held(given) {}
};

struct default_argument_derived : default_argument_base {
  using default_argument_base::default_argument_base;
};

int main() {
  default_argument_derived defaulted;
  default_argument_derived written(4);
  return defaulted.held + written.held - 11;
}
