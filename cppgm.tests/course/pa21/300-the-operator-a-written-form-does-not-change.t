// 13.5p1 and 13.3.1.2p2: which reading an operator expression has - the call of
// an operator function, or 13.6's built-in operator - is settled by the types of
// its operands and by nothing else.  Not by the shape the operand was written
// in: `x += y` over a class is the same question `C(1) += y` is, and 5.17p1's
// write-back is a rule the built-in operator alone has.  And not by the values
// the operands came to: 5.14p1 leaves the right operand of `&&` unevaluated
// where the left one decides, which is again the built-in operator's rule alone,
// because 13.5p4 makes an overloaded one an ordinary call and a call evaluates
// every argument.

struct flag
{
  bool held;

  // 4.12p1's contextual conversion, which is how a class operand reaches the
  // *built-in* `&&` and `||`.  No `operator&&` is declared for this class, so
  // 5.14p1's short-circuit is what stands over it.
  constexpr explicit operator bool() const { return held; }
};

// 5.14p1: the right operand is not evaluated where the left one decides, so
// this is never called with a zero divisor even though it is written as though
// it might be.
constexpr int quotient(int over, int under) { return over / under; }

constexpr flag no = {false};
constexpr flag yes = {true};

// A right operand that is no constant expression at all where it is evaluated:
// these fold only because the left operand ends the reading.
static_assert(!(no && quotient(1, 0)), "5.14p1 over a class operand");
static_assert(yes || quotient(1, 0), "5.15p1 over a class operand");
// And where it does not end the reading, the built-in operator reads both.
static_assert(yes && quotient(6, 3) == 2, "the built-in `&&` reads both");

struct counted
{
  int rep;

  // 13.5.3p1: assignment is a non-static member function, and a constant
  // expression names this declaration for an operand that is a name exactly as
  // it does for one that is a temporary.
  constexpr int operator=(counted other) const { return rep * 100 + other.rep; }
  // 13.5p1: a compound assignment may be a member too, and it is a call and no
  // write-back - 5.17p7's "the operation is performed and assigned" belongs to
  // the built-in operator.
  constexpr int operator+=(counted other) const { return rep + other.rep; }
  // 13.5.7p1: the prefix form takes no operand beside the object, and the
  // postfix form is told from it by an `int` the program does not write.
  constexpr int operator++() const { return rep + 1; }
  constexpr int operator++(int) const { return rep + 2; }
};

// 13.5p1 names the shift-assignments among the operators an
// operator-function-id may be written with, so a declaration may be written for
// them and a constant expression may name it.
constexpr int operator<<=(counted left, counted right)
{
  return left.rep * 1000 + right.rep;
}
constexpr int operator>>=(counted left, counted right)
{
  return left.rep * 10000 + right.rep;
}

constexpr counted one = {1};
constexpr counted two = {2};

// Each of these is written on a *name*, which is where 5.17p1 would look for an
// object to write and where 5.19p2 would then refuse it: `one` is no object
// this evaluation declared.  13.3.1.2 is asked first, so the question never
// comes up.
static_assert((one = two) == 102, "13.5.3p1 on a name");
static_assert((one += two) == 3, "a compound assignment on a name");
// 13.5.7p1's two forms are declared above and named by nothing here: the
// reference refuses to fold either of them over a class operand, where g++ and
// this build name the declaration, so the shape is recorded in the audit rather
// than pinned against a reference that has no answer for it.
static_assert((one <<= two) == 1002, "operator<<= names a declaration");
static_assert((one >>= two) == 10002, "operator>>= names a declaration");
// The same operators written on a temporary, which is the exit that already
// asked, and which has to answer the same way.
static_assert((counted{4} += two) == 6, "and the same on a temporary");

// 13.3.1.2p3's non-member half over an operand of no class type at all: the
// left operand here is an `int`, so 13.3.1.2p2's test is answered by the right
// one, and an unqualified lookup is what finds the declaration.  The built-in
// `&&` would have answered `false` without reading the right operand at all.
struct token
{
  int rep;
};

constexpr int operator&&(int left, token right) { return left + right.rep + 40; }

constexpr token mark = {2};

static_assert((0 && mark) == 42, "13.3.1.2p2 asks about every operand");

int main()
{
  return 0;
}
