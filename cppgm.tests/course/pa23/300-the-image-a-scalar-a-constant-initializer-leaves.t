// VALIDATION: compile-pass
// N3485 focus: 3.6.2 [basic.start.init], 5.19 [expr.const], 3.7.1 [basic.stc.static]
//
// 3.6.2p2 asks nothing about `const` and nothing about the type either.
//
// The clause says an object with static storage duration whose initialization
// is a constant initializer holds what that initialization came to before the
// program runs.  5.19p3's is the other question one fold settles - what a
// *name* of the object is worth - and it is answered for a `const` object of
// integral or enumeration type alone.
//
// A scalar parts the two exactly as an object of class type does.  The walk
// that lays an image out reads a value off the line the dump spells wherever it
// can, so `int n = 3;` needs neither clause; but a member of an object a
// constructor built, an element of an array, a member of a base subobject and
// what 12.3.2p1's conversion function returns are none of them a line with a
// value on it, and there the value the fold arrived at is the only answer there
// is.  Each of the objects below is one of those, written with no `const` on
// it, at each of the three places 3.6.2p2's sentence names: a namespace, the
// definition 9.4.2p2 writes outside a class, and 3.7.1p3's object a block
// declared `static`.

struct held
{
  int first;
  long second;
  double third;

  constexpr held(int n)
    : first(n), second(n + 1), third(n + 0.5)
  {
  }
};

struct based : held
{
  int last;

  constexpr based(int n)
    : held(n), last(n * 2)
  {
  }
};

struct converts
{
  int held_value;

  constexpr converts(int n)
    : held_value(n)
  {
  }

  constexpr operator int() const
  {
    return held_value;
  }
};

constexpr held one(4);
constexpr based two(5);
constexpr int numbers[3] = { 7, 8, 9 };

int from_a_member = one.first;
long from_a_wider_member = one.second;
double from_a_floating_member = one.third;
int from_a_base = two.first;
int from_the_derived_part = two.last;
int from_an_element = numbers[1];
int from_a_temporary = held(11).first;
bool from_a_comparison = one.first == 4;

struct writes_them_out
{
  static int outside_the_class;
};

int writes_them_out::outside_the_class = one.first;

int from_a_block()
{
  static int declared_static = numbers[2];
  return declared_static;
}

int main()
{
  return from_a_member == 4 && from_a_wider_member == 5 &&
         from_a_floating_member == 4.5 && from_a_base == 5 &&
         from_the_derived_part == 10 && from_an_element == 8 &&
         from_a_temporary == 11 && from_a_comparison &&
         writes_them_out::outside_the_class == 4 && from_a_block() == 9
    ? 0
    : 1;
}
