// N3485 13.4p1: a written template-argument-list that completes two
// declarations of one name names neither of them where the naming has no target
// type for 13.4 to choose with - and `&f<int>` written inside a constant
// expression comes to an address where it stands, so there is no later.
//
// The set the naming builds is the same set whether the prefix was spelled as a
// name or as a decltype-specifier, so the question is asked of both: a reading
// that hands the set on lets 13.3 or a target type choose from it, and one that
// asks for no set at all has to answer here.

struct holder
{
  static holder make();

  template<class T>
  static int pick()
  {
    return 2;
  }

  template<class T>
  static int pick(T)
  {
    return 1;
  }
};

static_assert(&decltype(holder::make())::template pick<int> != nullptr,
              "the list completes two declarations and this naming chooses "
              "between none of them");

int main()
{
  return 0;
}
