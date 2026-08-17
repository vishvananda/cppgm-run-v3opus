// VALIDATION: compile-pass
// N3485 focus: 3.3.10 [basic.scope.hiding], 3.4.4 [basic.lookup.elab],
//              5.2.3 [expr.type.conv], 5.2.2 [expr.call]
//
// 3.3.10p2 lets a class or enumeration name share a region with a variable, a
// data member, a function or an enumerator of that name, and hides the class
// wherever the other one is visible - in either declaration order.  3.4.4p2
// then leaves the class reachable through one thing only: an
// elaborated-type-specifier, whose lookup ignores every declaration that is not
// a type.
//
// The two readings part company at the shape the grammar cannot tell apart.
// `S(1, 2, 3)` is 5.2.3p1's cast written in functional notation when `S` names
// a type and 5.2.2p1's call of a function when it does not, and what says which
// is 3.4.1's ordinary lookup - the one that finds the function.  A reading that
// asks 3.4.4p2's question instead answers "the class" at every one of those
// sites and refuses a program both a call and a cast are written in.
//
// So a constant expression asks the same lookup the expression layer asks: the
// call folds, `struct S` beside it still names the class, and a use of the name
// where 8.1's declarator wants a type reaches the function and no class at all.

struct hidden
{
  int held;
  constexpr hidden() : held(1) {}
};

// 3.3.10p2: declared after the class, and the class is hidden from here on.
constexpr int hidden(int first, int second, int third)
{
  return first + second + third;
}

// 3.3.10p2 again, with the two written the other way round - the clause is
// written "in any order", so nothing here turns on which declaration came
// first.
constexpr int item(int first, int second)
{
  return first * second;
}

struct item
{
  int held;
  constexpr item() : held(2) {}
};

namespace inner
{
  struct part
  {
    int held;
    constexpr part() : held(3) {}
  };

  constexpr int part(int only)
  {
    return only + 10;
  }
}

// 5.2.2p1 through the ordinary lookup, unqualified and qualified alike.
constexpr int by_name = hidden(1, 2, 3);
constexpr int by_scope = ::item(2, 4);
constexpr int by_namespace = inner::part(5);
static_assert(by_name == 6, "the function the ordinary lookup finds");
static_assert(by_scope == 8, "a qualified name is the same lookup");
static_assert(by_namespace == 15, "and so is one in a namespace");

// 3.4.4p2: the class the function hides, reached by the one specifier whose
// lookup ignores the function.
constexpr struct hidden first_object;
constexpr struct item second_object;
constexpr struct inner::part third_object;
static_assert(first_object.held == 1, "the class is still there");
static_assert(second_object.held == 2, "in either declaration order");
static_assert(third_object.held == 3, "and inside a namespace");

// 5.2.3p1's cast is the *other* reading of the shape, and a name no
// declaration hides is what writes it.
struct alone
{
  int held;
  constexpr alone(int only) : held(only) {}
};

constexpr int cast_written = alone(7).held;
static_assert(cast_written == 7, "an unhidden class is a cast");

// 3.4.1p1: a declaration in a block scope hides both, and what it hides them
// with is what the name means there.
constexpr int shadowing(int hidden)
{
  return hidden * 2;
}

static_assert(shadowing(4) == 8, "a parameter hides the pair");

// 14.2 and 5.2.3p1: the same question at the other door a fold reaches a call
// through, where the call is written as a template argument and arrives as a
// spelling rather than as a tree.
template<int N>
struct counted
{
  static constexpr int result = N;
};

typedef counted<hidden(4, 5, 6)> flattened;
static_assert(flattened::result == 15, "a flattened call is a call too");

int main()
{
  return by_name + by_scope + by_namespace + first_object.held +
                 second_object.held + third_object.held + cast_written +
                 flattened::result ==
             57
           ? 0
           : 1;
}
