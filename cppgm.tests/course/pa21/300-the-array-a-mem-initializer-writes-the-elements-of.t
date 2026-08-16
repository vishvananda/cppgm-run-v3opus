// VALIDATION: compile-pass
// N3485 focus: 12.6.2 [class.base.init], 8.5.1 [dcl.init.aggr], 5.19 [expr.const]
//
// 3.9p10 and 8.3.4p6 make a member of array type a subobject holding a list of
// its own exactly as one of class type holds a list of its subobjects, so a
// mem-initializer written for it is neither of the two shapes 12.6.2p2 has for
// a member of any other type.  Its clauses are 8.5.1p2's elements and not
// 13.3.1.3's arguments, and none of them is the one value a conversion reaches
// - `arr{1, 2}` writes two elements, `arr{5}` writes one and leaves 8.5.1p7 to
// value-initialize the rest, and `arr{}` leaves 8.5p7 to value-initialize them
// all.
//
// 12.6p1 says what an array of class type no mem-initializer names is worth:
// default-initializing an array default-initializes each of its elements,
// which is that class's own default constructor once per element.
//
// 10p1 puts all of that behind a base class subobject too: the member is
// reached through the entry the base stands at, and the image the definition
// holds lays the elements out at the bytes 9.2p13 gave them.

struct counted
{
  int value;

  constexpr counted() : value(4)
  {
  }

  constexpr counted(int given) : value(given)
  {
  }
};

struct written
{
  int pair[2];
  int partial[3];
  int emptied[2];

  constexpr written()
    : pair{1, 2}, partial{5}, emptied{}
  {
  }
};

struct built
{
  counted elements[2];

  constexpr built()
  {
  }
};

struct nested
{
  int grid[2][2];

  constexpr nested() : grid{{1, 2}, {3, 4}}
  {
  }
};

struct carried : written
{
  int extra;

  constexpr carried() : written(), extra(9)
  {
  }
};

constexpr written one;
constexpr built made;
constexpr nested rows;
constexpr carried through;

static_assert(one.pair[0] == 1, "");
static_assert(one.pair[1] == 2, "");
static_assert(one.partial[0] == 5, "");
static_assert(one.partial[2] == 0, "");
static_assert(one.emptied[0] == 0, "");
static_assert(one.emptied[1] == 0, "");
static_assert(made.elements[0].value == 4, "");
static_assert(made.elements[1].value == 4, "");
static_assert(rows.grid[0][1] == 2, "");
static_assert(rows.grid[1][0] == 3, "");
static_assert(through.pair[1] == 2, "");
static_assert(through.partial[0] == 5, "");
static_assert(through.extra == 9, "");

int main()
{
  return through.pair[0] + through.partial[2] - 1;
}
