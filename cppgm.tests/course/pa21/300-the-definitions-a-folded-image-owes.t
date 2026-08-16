// VALIDATION: compile-pass
// N3485 focus: 3.2 [basic.def.odr], 8.4.2 [dcl.fct.def.default]
//
// 3.6.2p2 makes the storage of every object here what its initialization comes
// to, so no call of a constructor survives in any of them.  What survives is the
// question of whose the *definition* is.  8.4.2p1 gives a constructor the class
// declared a definition the standard writes, and going through that definition
// is what makes it this unit's: the fold of `read_by_fold` reads it, and the
// lowering reads `holder`'s to lay `built` out item by item.  Storage that
// merely holds 3.6.2p1's zero goes through neither, so `never_folded` - default-
// initialized, value-initialized and over an array alike - owes no definition
// every unit may hold; and one the standard declared as well as defined, as
// `standard` has, is named by nothing the program wrote at all.

struct read_by_fold
{
  int value;
  explicit read_by_fold() = default;
};

struct standard
{
  int value;
};

struct never_folded
{
  int value;
  never_folded() = default;
};

struct holder
{
  int first;
  int second;

  constexpr holder(int one, int two) : first(one), second(two) {}
};

constexpr read_by_fold folded = read_by_fold();
constexpr standard blank = standard();
constexpr holder built(4, 5);

never_folded spare = never_folded();
never_folded row[3];
static never_folded slot = never_folded();

int main()
{
  return (folded.value == 0 && blank.value == 0 && built.second == 5 &&
          spare.value == row[2].value + slot.value)
    ? 0 : 1;
}
