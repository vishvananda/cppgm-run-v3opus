// VALIDATION: compile-pass
// N3485 focus: 13.3.1.4 [over.match.copy], 12.3.2 [class.conv.fct], 8.5 [dcl.init]
//
// 8.5p16 copy-initializes a place of class type from a value of another, and
// 13.3.1.4p1 gives that one candidate set two halves: the converting
// constructors of the place's class, and the conversion functions of the
// value's own.  A fold asks both in the order `match_by_value` asks them, so
// 5.19p3's converted constant expression is one rule at a place of class type
// exactly as it is at an arithmetic one.
//
// The place is filled the same way wherever 8.5 fills one from a single value:
// a declaration written `= x`, a clause of an aggregate, an element of an
// array, 12.6.2p8's brace-or-equal-initializer, an argument of a call, and
// 6.6.3p2's return statement.  4.10p3's base class subobject is the other half
// of the same reading, so a value of a derived class reaches a place of its
// base without a conversion function being asked for at all.

struct payload
{
  int held;

  constexpr payload(int given) : held(given)
  {
  }
};

struct source
{
  int seed;

  constexpr source(int given) : seed(given)
  {
  }

  constexpr operator payload() const
  {
    return payload(seed + 1);
  }
};

struct aggregate
{
  payload first;
  payload second;
};

struct holding
{
  payload kept = source(20);
};

constexpr source origin(1);

constexpr payload assigned = origin;
constexpr aggregate clauses = {origin, source(5)};
constexpr payload elements[2] = {origin, source(7)};
constexpr holding defaulted;

constexpr int reads(payload given)
{
  return given.held;
}

constexpr payload gives(source given)
{
  return given;
}

static_assert(assigned.held == 2, "");
static_assert(clauses.first.held == 2, "");
static_assert(clauses.second.held == 6, "");
static_assert(elements[1].held == 8, "");
static_assert(defaulted.kept.held == 21, "");
static_assert(reads(origin) == 2, "");
static_assert(gives(source(9)).held == 10, "");

int main()
{
  return assigned.held - 2;
}
