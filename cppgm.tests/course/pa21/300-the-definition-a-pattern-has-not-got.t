// VALIDATION: compile-pass
// N3485 focus: 14 [temp], 14.6 [temp.res], 14.7.1 [temp.inst], 5.19 [expr.const]
//
// 14p1 declares no function until a template is instantiated, so a reading of
// the *pattern* holds the definition of nothing the pattern declares: the body
// of a member of the class the template describes is read against types no
// argument list has given yet, and a specialization named with an argument that
// depends on a parameter is a declaration made for an argument list that says
// nothing.
//
// 14.6p8 already says what such a reading does with a name whose value the
// arguments settle: it stands one value in and carries the answer that a value
// was stood in.  A *call* is the same sentence.  A reading that instead calls
// the missing body 7.1.5p2's error refuses the pattern where it stands -
// `typedef bool_constant<enabled()> type;` is written in the class that
// declares `enabled`, and there is no reading of that typedef in which the
// definition is already there.
//
// The stand-in decides nothing.  14.7.1p1 reads the same call again with the
// arguments bound, and that reading is where 5.19 and 7.1.5p2 are answered -
// which is why a member the program did not write `constexpr` on is still the
// program's error, found at the instantiation and not at the pattern.

template<bool B>
struct bool_constant
{
  static constexpr bool value = B;
  static constexpr int width = B ? 10 : 20;
};

template<class T>
constexpr unsigned long sized()
{
  return sizeof(T);
}

template<class T>
struct holder
{
  // 14p1: a member of the class this pattern describes.  Its body is read here
  // against a `T` no argument list has settled, and no definition is recorded.
  static constexpr bool enabled()
  {
    return sizeof(T) == sizeof(int);
  }

  // 5.19 written in a *later member* of the same pattern, which is where the
  // definition above is not yet anything to call.
  typedef bool_constant<enabled()> chosen;

  // 14.2 with 14.8.2: a specialization of a function template made over an
  // argument that depends on this pattern's parameter is the same answer one
  // door along.
  typedef bool_constant<(sized<T>() == 4)> measured;

  // 8.3.4p1 reads the same call, and is a place 14.6p8 already stands a value
  // in at - so the bound the pattern computes says nothing and the one the
  // arguments make is the answer.
  typedef char counted_elements[enabled() ? 1 : 2];
};

// 14.7.1p1: the arguments are what answer all three, and the answers differ -
// which is what says the stand-in decided nothing.
static_assert(holder<int>::chosen::value, "int is the width of an int");
static_assert(!holder<char>::chosen::value, "char is not");
static_assert(holder<int>::measured::value, "the function template agrees");
static_assert(!holder<double>::measured::value, "and disagrees");
static_assert(holder<int>::chosen::width == 10, "the specialization is chosen");
static_assert(holder<char>::chosen::width == 20, "by the arguments alone");
static_assert(sizeof(holder<int>::counted_elements) == 1, "the bound is theirs");
static_assert(sizeof(holder<char>::counted_elements) == 2, "in both directions");

// 14.7.1p1 over a member of a class template named from outside every pattern,
// which is the reading that has the definition.
static_assert(holder<int>::enabled(), "the instantiation folds the call");
static_assert(sized<double>() == 8, "and so does a function template's own");

int main()
{
  return holder<int>::chosen::width + holder<char>::chosen::width == 30 ? 0 : 1;
}
