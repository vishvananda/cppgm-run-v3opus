// 14.5.5.2p1 asks 14.8.2.5p4's match of two *patterns*, so a fixed place facing
// an entry that stands for a run takes nothing from it - otherwise `<Prev,
// Last>` and `<Prev, Others...>` are each at least as specialized as the other
// and every list they both match is ambiguous.
//
// 14.5.6.1p5's other half - one pattern redeclared over a pack is one
// declaration - is left to `400-partial-specialization-redecl-member-template-
// empty-pack.t`, whose checked-in `.ref` already pins it: `pa22/cppgm++-ref`
// reads three declarations of `twice<box<Head, Tail...> >` as three patterns
// and calls the naming ambiguous where `g++ -pedantic-errors` accepts it, so a
// fixture generated from it here would pin that refusal.

template<class... Types>
struct box
{
};

template<class T>
struct pick;

template<class Only>
struct pick<box<Only> >
{
  static const int value = 1;
};

template<class First, class Second>
struct pick<box<First, Second> >
{
  static const int value = 2;
};

template<class First, class... Rest>
struct pick<box<First, Rest...> >
{
  static const int value = 3;
};

static_assert(pick<box<int> >::value == 1, "one entry is the one-entry pattern");
static_assert(pick<box<int, char> >::value == 2, "two entries beat the run");
static_assert(pick<box<int, char, long> >::value == 3, "three entries take it");

int main()
{
  return pick<box<int, char> >::value == 2 ? 0 : 1;
}
