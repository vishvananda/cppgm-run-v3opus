// VALIDATION: compile-pass
// N3485 focus: 8.5 [dcl.init], 7.1.5 [dcl.constexpr], 12.6.2 [class.base.init]
//
// 8.5p6 makes a declaration that wrote no initializer an initialization like
// any other: the object is default-initialized, which for one of class type
// calls the class's default constructor.  12.1p5 makes the constructor the
// standard defines a constexpr constructor exactly where every non-static data
// member is reached by 12.6.2p8's brace-or-equal-initializer - so `implied` is a
// constant 5.19 reads, `standard_constructor()` written out is the same answer,
// and 3.6.2p2 gives both of them an image and no startup body.  `local` is that
// class where no image stands at all, which is what still asks this unit for the
// definition the fold read.
//
// 3.9p10 makes an array of literal type a literal type too, so `grid` holds a
// list of lists that a second subscript reads - one clause per element down the
// object rather than a row of expressions the declaration then places.

struct written_constructor
{
  int value;

  constexpr written_constructor() : value(7)
  {
  }
};

struct standard_constructor
{
  int value = 5;
  int spare = 6;
};

struct nothing_at_all
{
};

constexpr written_constructor wrote;
constexpr standard_constructor implied;
constexpr nothing_at_all blank;
constexpr standard_constructor made = standard_constructor();
constexpr int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};

static_assert(wrote.value == 7, "");
static_assert(implied.value == 5 && implied.spare == 6, "");
static_assert(made.spare == 6, "");
static_assert(grid[1][2] == 6, "");

int main()
{
  written_constructor built;
  standard_constructor local;
  return wrote.value + implied.value + grid[0][0] + local.value +
    built.value - 20;
}
