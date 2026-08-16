// N3485 focus: 10.3 [class.virtual] p1 and p10 with 10 [class.derived] p1 and
// 9.2 [class.mem] p13 - a class holding a base subobject that carries a
// vpointer carries that one rather than adding its own, and the table it
// dispatches through is that base's with its own overriders written in.  A base
// that dispatches and begins where the object does is that base however many
// plain bases stand after it, and an empty base written before it takes no
// storage and so does not move it.

int trace[8];
int steps = 0;

struct dispatches
{
  dispatches() { trace[steps++] = 1; }
  virtual ~dispatches() { trace[steps++] = -1; }
  virtual int which() { return 1; }
  int a;
};

struct plain
{
  plain() { trace[steps++] = 2; }
  ~plain() { trace[steps++] = -2; }
  int b;
};

struct empty {};

struct over_two : dispatches, plain
{
  int which() { return 2; }
  int c;
};

struct below : over_two
{
  int which() { return 3; }
};

struct after_empty : empty, dispatches
{
  int which() { return 4; }
};

int dispatch_through(dispatches & object) { return object.which(); }

int main()
{
  {
    below one;
    one.a = 5;
    one.b = 6;
    one.c = 7;
    // 10.3p10: the table is the base's, so the overrider a class further down
    // wrote is what a call through that base reaches.
    if (dispatch_through(one) != 3) { return 1; }
    over_two & as_over = one;
    if (as_over.which() != 3) { return 2; }
    // 10p1 and 9.2p13: the base that dispatches begins where the object does,
    // and the plain one stands after it.
    dispatches * to_dispatches = &one;
    plain * to_plain = &one;
    if ((void *)to_dispatches != (void *)&one) { return 3; }
    if ((void *)to_plain == (void *)&one) { return 4; }
    if (to_dispatches->a != 5 || to_plain->b != 6) { return 5; }
    after_empty two;
    // 9p6 and 10.3p1: an empty base takes no storage, so the base that
    // dispatches still begins where the object does.
    dispatches * to_base = &two;
    if ((void *)to_base != (void *)&two) { return 6; }
    if (dispatch_through(two) != 4) { return 7; }
  }
  // 12.6.2p10 and 12.4p8: the base that dispatches is constructed first and
  // destroyed last, as the list wrote it.
  if (trace[0] != 1 || trace[1] != 2) { return 8; }
  if (trace[2] != -2 || trace[3] != -1) { return 9; }
  return 0;
}
