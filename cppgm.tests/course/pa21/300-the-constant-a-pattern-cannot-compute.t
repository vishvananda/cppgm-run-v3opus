// VALIDATION: compile-pass
// N3485 focus: 14.6 [temp.res], 8.3.4 [dcl.array], 5.19 [expr.const]
//
// 14.6p8 lets a reading of a template definition stand a value in wherever the
// definition names something an argument list has yet to settle: how large a
// dependent type is, how many elements a pack has, what a member of the pattern
// holds.  The whole point of the reading is that it goes through - and what it
// arrives at afterwards is not the specialization's answer.
//
// So every place that turns such a value into a decision has to ask whether one
// was stood in.  8.3.4p1's array bound is the place this is written at: `char
// check[sizeof(T) == 4 ? 1 : -1]` is the idiom the standard's own library uses,
// and reading it where the pattern stands takes the arm the stand-in chose and
// refuses the bound it made.  One element stands in until the instantiation
// reads the bound its arguments make, which is where 8.3.4p1 is asked.
//
// The same answer travels: a declaration of the pattern whose own initializer
// this reading stood a value in for holds no constant either - `static
// constexpr bool flags[] = { pick<T>::value... };` is an array as long as the
// pack, and its elements are the arguments' to say - so a later member of the
// pattern that reads it stands a value in for it in turn, and so does a call
// written as a template argument whose operands did.  7.2p1's enumerator is the
// same declaration once more.
//
// A reading that stood nothing in is unchanged: it ran out on the program's own
// expression and says so, which is what leaves a bound of zero, a negative
// bound and a non-constant one each the error they are.

typedef decltype(sizeof(0)) size_t;

constexpr size_t first_set(bool const* first, bool const* last)
{
  return first == last || *first ? 0 : 1 + first_set(first + 1, last);
}

template<class T>
struct pick
{
  static constexpr bool value = false;
};

template<>
struct pick<long>
{
  static constexpr bool value = true;
};

template<size_t N>
struct counted
{
  static constexpr size_t result = N;
};

template<class T>
struct sized
{
  // 8.3.4p1 over a bound that is value-dependent: the reading stands one value
  // in for `sizeof(T)`, so the arm it takes here says nothing and the bound
  // waits for the arguments.
  typedef char four_bytes[sizeof(T) == 4 ? 1 : -1];
  // 7.2p1: the enumerator's constant-expression is dependent too, so what it is
  // worth is the arguments' - and a bound that reads it back is theirs as well.
  enum { width = sizeof(T) };
  typedef char same_width[width == 4 ? 1 : -1];
  // 5.19p2 one name further along: the member's own initializer stood a value
  // in, so the member holds no constant the pattern knows.
  static constexpr size_t doubled = width * 2;
  typedef char eight_bytes[doubled == 8 ? 1 : -1];
};

template<class... T>
struct chosen
{
  // 14.5.3p4 with 8.3.4p1: as many elements as the pack has, which no reading
  // of the pattern counts.
  static constexpr bool flags[] = { pick<T>::value... };
  // 5.3.3p5's `sizeof...` and 5.19p2's address of the member above, read in one
  // constant expression by a call written where the pattern stands.
  typedef char second_one[first_set(flags, flags + sizeof...(T)) == 1 ? 1 : -1];
  // The same call written as a *template argument*, which reaches 5.19 through
  // the flattened spelling rather than through a tree.
  typedef counted<first_set(flags, flags + sizeof...(T))> where;
};

typedef sized<int>::four_bytes first_check;
typedef sized<int>::same_width second_check;
typedef sized<int>::eight_bytes third_check;
typedef chosen<int, long, char>::second_one fourth_check;
typedef char fifth_check[chosen<int, long, char>::where::result == 1 ? 1 : -1];

int main()
{
  return sizeof(first_check) + sizeof(second_check) + sizeof(third_check) +
         sizeof(fourth_check) + sizeof(fifth_check) == 5 ? 0 : 1;
}
