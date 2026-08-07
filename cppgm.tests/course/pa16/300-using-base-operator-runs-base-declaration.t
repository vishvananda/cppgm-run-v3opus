// 13.3.1.2 and 7.3.3p1: an operator expression is the call 13.3.1.2 says it is,
// and where 13.3 chose the declaration a using-declaration made, the function it
// runs is the base's - on the base class subobject of the object the expression
// named, under the name the base's declaration has in the object file.
struct operator_base {
  int held;
  int operator+(int given) { return held + given; }
  int operator()(int given = 3) { return held * given; }
};

struct operator_derived : private operator_base {
 public:
  using operator_base::operator+;
  using operator_base::operator();
  operator_derived() { held = 2; }
};

int main() {
  operator_derived derived;
  return (derived + 1) + derived() - 9;
}
