// VALIDATION: compile-pass
// N3485 focus: 7.3.4 [namespace.udir] p2
// Expected: a using-directive written in a block makes its names usable in
// that block alone, at the level of the nearest namespace enclosing both.  So
// two functions each nominating a different namespace that declares one name
// are two unambiguous lookups, not one ambiguity - and a name an enclosing
// namespace declares at the same level still makes the lookup ambiguous.

namespace Left { int value = 1; }
namespace Right { int value = 2; }

int from_left()
{
  using namespace Left;
  return value;
}

int from_right()
{
  using namespace Right;
  return value;
}

int nested()
{
  int total = 0;
  { using namespace Left; total += value; }
  { using namespace Right; total += value; }
  return total;
}

int main() { return from_left() + from_right() + nested(); }
