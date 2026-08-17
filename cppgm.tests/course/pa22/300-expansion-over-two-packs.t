// 14.5.3p4 lets one expansion be written over more than one pack, and 14.5.3p6
// makes them the same length - `pr<A, B>...` is one run of pairs and a run per
// place.  The substitution reading already read one over as many packs as its
// pattern named; the deduction reading took one place and refused every other
// pattern, so `box<pr<A, B>...>` matched nothing at all.
//
// Only a run of one is pinned here.  `pa22/cppgm++-ref` answers `box<>` and
// `box<pr<int, char>, pr<long, short> >` from the primary where `g++
// -pedantic-errors` and this build bind both places, so a fixture generated
// from it would pin those two refusals rather than the rule.

template<class... Types>
struct box
{
};

template<class First, class Second>
struct pr
{
};

template<class T>
struct split
{
  static const int value = 0;
};

template<class... A, class... B>
struct split<box<pr<A, B>...> >
{
  static const int value = 1 + (int)sizeof...(A) * 10 + (int)sizeof...(B);
};

template<class T>
struct fixed
{
  static const int value = 0;
};

template<class... A>
struct fixed<box<pr<A, int>...> >
{
  static const int value = 2;
};

static_assert(split<box<pr<int, char> > >::value == 12,
              "one pair is one element of each run");
static_assert(split<box<int> >::value == 0, "an entry that is no pair");
static_assert(fixed<box<pr<char, int> > >::value == 2,
              "a fixed place beside a pack the same expansion names");
static_assert(fixed<box<pr<char, long> > >::value == 0,
              "and an element the fixed place does not take");

int main()
{
  return split<box<pr<int, char> > >::value == 12 ? 0 : 1;
}
