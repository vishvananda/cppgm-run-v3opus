// 9p6 and 12.6.2: a base subobject of a class that holds nothing has no bytes
// for a mem-initializer to carry, and 1.9p12 evaluates the operand only where
// evaluating it is something the program can observe.  4.10p3's base conversion
// is the address the operand already produced, moved to where the class put the
// base, so the initialization names the subobject it builds and reads nothing
// out of the one it was written from.
struct Empty {};

struct Counted : Empty {
  int value;

  Counted(int v) : Empty(), value(v) {}
  Counted(Counted const& other) : Empty(other), value(other.value + 1) {}
};

int main()
{
  Counted first(3);
  Counted second(first);
  return second.value - 4;
}
