// N3485 focus: 10 [class.derived] p1 with 9.2 [class.mem] p13, 10.2
// [class.member.lookup] p2, 4.10 [conv.ptr] p3 and 12.6.2 [class.base.init]
// p10 - a base-specifier-list of more than one entry gives an object one
// subobject per entry.  Each stands at a byte of its own, a member of each is
// found through the class that declares it, a conversion to each names its own
// subobject, and the constructors run in the order the list was written and the
// destructors in the reverse of it.

int trace[8];
int steps = 0;

struct first
{
  first() { trace[steps++] = 1; }
  ~first() { trace[steps++] = -1; }
  int a;
  int of_first() { return a; }
};

struct second
{
  second() { trace[steps++] = 2; }
  ~second() { trace[steps++] = -2; }
  long b;
  long of_second() { return b; }
};

struct empty {};

struct both : first, empty, second
{
  both() { trace[steps++] = 3; }
  ~both() { trace[steps++] = -3; }
  char c;
};

struct below : both
{
  int d;
};

int main()
{
  {
    below one;
    one.a = 4;
    one.b = 5;
    one.c = 6;
    one.d = 7;
    // 10.2p2: each name is declared by one of the classes above, so the lookup
    // finds it there whichever base subobject holds it.
    if (one.of_first() != 4) { return 1; }
    if (one.of_second() != 5) { return 2; }
    if (one.c != 6 || one.d != 7) { return 3; }
    // 4.10p3: the conversion names the subobject, so the second base is a
    // different address from the first and both hold what was written.
    first * to_first = &one;
    second * to_second = &one;
    if (to_first->a != 4) { return 4; }
    if (to_second->b != 5) { return 5; }
    if ((void *)to_first == (void *)to_second) { return 6; }
    if ((void *)to_first != (void *)&one) { return 7; }
    // 9.2p13: the object holds all of them, so it is at least as large as the
    // two that carry storage.
    if (sizeof(below) < sizeof(int) + sizeof(long)) { return 8; }
    if (sizeof(empty) != 1) { return 9; }
  }
  // 12.6.2p10 and 12.4p8: first, second, both - and then the reverse.
  if (steps != 6) { return 10; }
  if (trace[0] != 1 || trace[1] != 2 || trace[2] != 3) { return 11; }
  if (trace[3] != -3 || trace[4] != -2 || trace[5] != -1) { return 12; }
  return 0;
}
