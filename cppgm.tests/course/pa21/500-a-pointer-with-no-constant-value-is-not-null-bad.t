// 4.10p1: the null pointer value is what a *null pointer constant* comes to,
// which is an integral constant expression of value zero or `nullptr`.  An
// object of pointer type that 5.19 gives no constant value is none of those:
// `static int *held;` has an address a constant expression reads and no value
// one does, and the two are different facts about the same declaration.
//
// So a reading that takes such an object for the null pointer answers a
// question the program never asked - and answers it wrongly, because the
// program's `held` is whatever the dynamic initialization left in it.  The
// static_assert below shall be a diagnostic and not a `true`.

static int *held;

constexpr int *identity(int *from)
{
  return from;
}

static_assert(identity(held) == 0, "a pointer with no constant value is null");

int main()
{
  return 0;
}
