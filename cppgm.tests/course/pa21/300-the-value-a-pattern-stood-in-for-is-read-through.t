// VALIDATION: compile-pass
// N3485 focus: 14.6 [temp.res], 14.7.1 [temp.inst], 5.19 [expr.const],
//              7.2 [dcl.enum], 9.6 [class.bit], 7.6.2 [dcl.align]
//
// 14.6p8 lets a reading of a template's definition stand one value in wherever
// an argument list is what settles something - the size of a dependent type,
// the length of a pack, the value a call of a member the pattern declares comes
// to.  What that stand-in is worth is nothing, and it is worth nothing however
// the reading ends: a place that reads *through* it runs out, because a value
// of type `int` is no object of class type and no array, and the refusal that
// makes is about the stand-in and not about the program.
//
// So every place that decides something from a constant expression answers "the
// arguments'" for both - for a value the reading arrived at, and for a reading
// that ran out on its own stand-in.  8.3.4p1's bound and 7.1.5p9's declaration
// already did; 7.2p1's enumerator, 7p4's static_assert, 9.6p1's bit-field
// width, 7.6.2p1's alignment-specifier and 14.3.2p5's template argument each
// stand where a pattern does too, and each is written below reading through a
// call whose value only an argument list can give.
//
// The stand-in decides nothing: 14.7.1p1 reads each of them again with the
// arguments bound, and the answers differ between two specializations, which is
// what says the pattern's reading settled none of them.

struct box
{
  int v;
  constexpr box(int n) : v(n) {}
  constexpr int twice() const { return v * 2; }
};

template<int N>
struct chosen
{
  static constexpr int value = N;
};

template<class T>
struct holder
{
  // 14p1: a member of the class this pattern describes, whose definition the
  // reading of the pattern has not got - so a call of it stands a value in.
  static constexpr box make() { return box(sizeof(T)); }

  // 7.2p1 over a member access on that stand-in, and 7.2p1's successor after
  // it, which writes no constant-expression of its own.
  enum { first = make().v, second, third = make().twice() };

  // 7p4: the same reading in a static_assert written inside the pattern.
  static_assert(make().v > 0, "an object of class type has a member");

  // 14.3.2p5 as a template argument, over a member access and a member call.
  typedef chosen<make().v> by_member;
  typedef chosen<make().twice()> by_call;

  // 9.6p1's width and 7.6.2p1's alignment, each reading through the same call.
  unsigned counted : make().v;
  alignas(make().v) unsigned char aligned;

  // 8.3.4p1's bound, which already answered this way, beside them.
  typedef char sized[make().v];

};

// 14.7.1p1: the arguments answer all of them, and the answers differ.
static_assert(holder<int>::first == 4, "the member access is the arguments'");
static_assert(holder<char>::first == 1, "in both directions");
static_assert(holder<int>::second == 5, "7.2p1's successor with it");
static_assert(holder<char>::second == 2, "and for the other argument list");
static_assert(holder<int>::third == 8, "the member call is theirs too");
static_assert(holder<double>::third == 16, "whatever the arguments make it");
static_assert(holder<int>::by_member::value == 4, "and the template argument");
static_assert(holder<char>::by_member::value == 1, "for either list");
static_assert(holder<int>::by_call::value == 8, "written as a call");
static_assert(holder<double>::by_call::value == 16, "for either list");
static_assert(sizeof(holder<int>::sized) == 4, "the bound is the arguments'");
static_assert(sizeof(holder<char>::sized) == 1, "as it always was");

// 9.6p1 and 7.6.2p1 are read off the object the arguments made.
struct measured
{
  holder<int> wide;
};

static_assert(sizeof(measured) >= 4, "the width and the alignment are laid out");

// The pattern's own errors stay the pattern's: a member the program did not
// write `constexpr` on is refused where the arguments reach the definition, and
// a reading that stood nothing in refuses where it stands.  Both are written in
// the -bad file beside this one.

int main()
{
  return holder<int>::first + holder<char>::second == 6 ? 0 : 1;
}
