// VALIDATION: compile-fail
// N3485 focus: 7.3.4 [namespace.udir] p2
// Expected: 7.3.4p2's names can be used in the scope the directive appears in,
// so a directive written in one function's body reaches no lookup in another -
// the level its names appear at is not a place every lookup can see them from.

namespace Hidden { int value = 1; }

int writes_the_directive()
{
  using namespace Hidden;
  return value;
}

int elsewhere()
{
  return value;
}

int main() { return writes_the_directive() + elsewhere(); }
