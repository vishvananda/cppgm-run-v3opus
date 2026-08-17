// VALIDATION: compile-pass
// N3485 focus: 1.4 [intro.compliance], 5.2.9 [expr.static.cast], 5.19 [expr.const]
//
// 1.4p8 lets an implementation reserve names for functions of its own, and
// `__builtin_expect` is the one whose *definition* the implementation states
// rather than leaving to an object file: it takes the value a condition came to
// and the value that condition is expected to take, and hands the first of them
// back.  So it is an ordinary declaration of `long (long, long)` - 13.3 ranks
// it, 4.5 and 4.7 bring an operand of any other integral type to its
// parameters, and 5.3.3 measures its result - and a call of it crosses no
// boundary at all: 5.2.2p8 still evaluates both operands, and what the call is
// worth is the first.
//
// 5.19 reads it too, because the value a call of it comes to is one the
// implementation states rather than one a body computes.
//
// Beside it, 5.2.9p4: any expression converts to cv `void`, and what it becomes
// is a discarded-value expression.  It is still evaluated - which is what lets
// 5.18p1's comma and 5.16p2's arm of `void` type carry it - and it holds no
// value for anything to read.

void report();

constexpr int guarded(int index, int bound)
{
  return __builtin_expect(!(index < bound), 0) ? -1 : index;
}

static_assert(guarded(1, 4) == 1, "");
static_assert(guarded(9, 4) == -1, "");

// 5.2.9p4 at the two places a `void` operand can stand: 5.18p1's left operand
// and 5.16p2's second arm, with the reading of the first operand deciding which
// of them runs.
constexpr int discarded(int value)
{
  return (value < 0) ? report() : (void)0, value + 1;
}

static_assert(discarded(4) == 5, "");
static_assert((((void)0, 7)) == 7, "");

// 1.4p8 puts the reserved function in the global namespace, so a qualified name
// reaches the one declaration and not a second.
constexpr long qualified(long held)
{
  return ::__builtin_expect(held, 1);
}

static_assert(qualified(6) == 6, "");

// 5.3.3p1 measures the declaration's own return type, and 4.7 brings a `bool`
// operand to the parameter the same way an ordinary call's argument is brought.
static_assert(sizeof(__builtin_expect(1, 0)) == sizeof(long), "");
static_assert(__builtin_expect(true, false) == 1, "");

// A body lowers the same call: both operands run where the program wrote them
// and the value is the first, with no call boundary and no declaration owed.
long hinted(long held)
{
  return __builtin_expect(held, 0) + __builtin_expect(held, 1);
}

int main()
{
  return static_cast<int>(hinted(0)) + guarded(0, 2);
}
