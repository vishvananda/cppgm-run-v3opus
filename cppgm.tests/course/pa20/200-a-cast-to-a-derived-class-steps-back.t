// N3485 focus: 5.2.9 [expr.static.cast] p11 with 10 [class.derived] p1 and 9.2
// [class.mem] p13 - a cast from a pointer or an lvalue of a base class to a
// class derived from it names the object that base subobject is part of.  The
// subobject begins where the derived class put it, so the address the cast
// hands back is the operand's own moved *back* by that place - which is nothing
// at all for a base standing at the object's first byte and the base's offset
// for every other one.

struct first
{
  int a;
};

struct second
{
  int b;
};

struct both : first, second
{
  int c;
};

struct dispatches
{
  virtual int which() { return 1; }
};

struct carries : dispatches
{
  int d;
  int which() { return 2; }
};

both one;
carries two;

int main()
{
  one.a = 1;
  one.b = 2;
  one.c = 3;
  // 4.10p3: the conversion to the second base names its own subobject, which
  // does not begin where the object does.
  second * to_second = &one;
  if ((void *)to_second == (void *)&one) { return 1; }
  // 5.2.9p11: the cast back names the object again, so what it hands back is
  // the address the object has and not the one the subobject has.
  both * back = static_cast<both *>(to_second);
  if ((void *)back != (void *)&one) { return 2; }
  if (back->a != 1 || back->b != 2 || back->c != 3) { return 3; }
  // 5.2.9p11 again, through a reference: the same step, and no test of the
  // operand, because a reference names an object.
  second & bound = one;
  both & named = static_cast<both &>(bound);
  if (&named != &one) { return 4; }
  if (named.c != 3) { return 5; }
  // 5.2.9p11 through the base a class that dispatches put after its vpointer:
  // that base does not begin where the object does either.
  dispatches * to_base = &two;
  if ((void *)to_base != (void *)&two) { return 6; }
  two.d = 7;
  carries * to_carries = static_cast<carries *>(to_base);
  if (to_carries != &two) { return 7; }
  if (to_carries->d != 7) { return 8; }
  if (to_carries->which() != 2) { return 9; }
  // 5.2.9p11: a base standing at the object's first byte leaves the address
  // the operand held.
  first * to_first = &one;
  both * from_first = static_cast<both *>(to_first);
  if ((void *)from_first != (void *)&one) { return 10; }
  return 0;
}
