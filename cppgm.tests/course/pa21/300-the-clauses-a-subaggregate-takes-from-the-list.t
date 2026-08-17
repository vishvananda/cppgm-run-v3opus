// VALIDATION: compile-pass
// N3485 focus: 8.5.1 [dcl.init.aggr], 8.5.2 [dcl.init.string], 5.19 [expr.const]
//
// 8.5.1p11 lets the braces around a subaggregate's own clauses be left out, and
// then the clauses standing in the enclosing list initialize *its* subobjects
// rather than it.  So how many clauses a subobject takes is what its own walk of
// its subobjects arrives at, and not one clause per member: `{7, 11, 13, 17}`
// written for a class whose one member is `int[4]` fills the four elements, and
// `{1, 2, 3}` for a class holding `int[2]` and an `int` fills both.
//
// 8.5.2p1 is the other reading a subobject may take that is no clause of the
// enclosing list at all: an array of character type written with a string
// literal holds the literal's own code units, with 8.5.1p7 value-initializing
// every element past them.  It is an initialization of its own rather than a
// conversion, so it stands wherever the array does - a declaration of one, a
// member of a class an aggregate initializer fills, and 12.6.2p8's
// brace-or-equal-initializer alike.
//
// 5.19 reads all of it back: what a constant expression holds for an object of
// class or array type is what its subobjects hold, so a subscript and a member
// access written over the folded object are what say which clause reached which
// subobject.

struct row
{
  int cells[4];
};

struct pair_and_tail
{
  int leading[2];
  int trailing;
};

struct nested_pair
{
  int first;
  int second;
};

struct holding_a_class
{
  nested_pair inner;
  int outer;
};

struct labelled
{
  char label[5];
  int weight;
};

// 8.5.1p11 one level down, with every clause written and with the tail 8.5.1p7
// value-initializes.
constexpr row filled = {7, 11, 13, 17};
constexpr row partly = {7, 11};
constexpr row braced = {{7, 11, 13, 17}};

static_assert(filled.cells[0] == 7, "");
static_assert(filled.cells[3] == 17, "");
static_assert(partly.cells[1] == 11, "");
static_assert(partly.cells[2] == 0, "");
static_assert(braced.cells[2] == 13, "");

// 8.5.1p11 where the elided subaggregate is followed by a member of its own.
constexpr pair_and_tail split = {1, 2, 3};
static_assert(split.leading[0] == 1, "");
static_assert(split.leading[1] == 2, "");
static_assert(split.trailing == 3, "");

// 8.5.1p11 where the subaggregate is a class rather than an array.
constexpr holding_a_class held = {4, 5, 6};
static_assert(held.inner.first == 4, "");
static_assert(held.inner.second == 5, "");
static_assert(held.outer == 6, "");

// 8.5.1p11 read the other way: a clause that *is* a value of the subobject's
// own class initializes the whole of it and leaves nothing standing for its
// members.
constexpr nested_pair source = {8, 9};
constexpr holding_a_class copied = {source, 10};
static_assert(copied.inner.second == 9, "");
static_assert(copied.outer == 10, "");

// 8.5.2p1 at a declaration of the array, with the braces and without them, and
// with 8.3.4p3's bound taken from the literal.
constexpr char two[3] = "ab";
constexpr char two_braced[3] = {"ab"};
constexpr char counted[] = "abc";

static_assert(two[0] == 'a', "");
static_assert(two[2] == 0, "");
static_assert(two_braced[1] == 'b', "");
static_assert(counted[3] == 0, "");
static_assert(sizeof(counted) == 4, "");

// 8.5.2p1 where the array is a member an aggregate initializer reaches, with
// 8.5.1p7's zeroes past the literal.
constexpr labelled tagged = {"hi", 12};
static_assert(tagged.label[0] == 'h', "");
static_assert(tagged.label[1] == 'i', "");
static_assert(tagged.label[2] == 0, "");
static_assert(tagged.label[4] == 0, "");
static_assert(tagged.weight == 12, "");

// 8.5.1p11 read again where the type comes from an argument list and the
// declaration stands inside an instantiated function body, which 3.3.2 makes
// the same initialization as one written at namespace scope.
template<class T, unsigned N>
struct sized
{
  T elements[N];
};

template<class T>
void reading()
{
  constexpr sized<T, 4> flat = {30, 31, 32, 33};
  static_assert(flat.elements[0] == 30, "");
  static_assert(flat.elements[3] == 33, "");

  constexpr sized<T, 2> written = {{40, 41}};
  static_assert(written.elements[1] == 41, "");
}

int main()
{
  reading<int>();
  return filled.cells[0] + split.trailing - 10;
}
