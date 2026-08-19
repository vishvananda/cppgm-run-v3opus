// VALIDATION: run-pass
// 8.3.5p1 with 8.3p1: the ellipsis of a parameter-declaration stands in the
// declarator wherever a ptr-operator was written, and 8.3p1 lets a declarator
// hold another - so a place whose type needs parentheses writes the `...`
// *inside* them.  `int (& ... Rs)[2]` and `int (* ... Ps)()` declare a run
// exactly as `int & ... Rs` does, and the one reader of that syntax has to walk
// through the parentheses to find it: a place whose run neither the type nor
// 14.5.3p1's arity saw is one a list of none gives too few arguments to.

int pair_one[2] = { 1, 2 };
int pair_two[2] = { 3, 4 };

int seven()
{
  return 7;
}

int eight()
{
  return 8;
}

template<int (& ... Rs)[2]>
struct arrayed
{
  static const int count = sizeof...(Rs);
};

template<int (* ... Ps)()>
struct called
{
  static const int count = sizeof...(Ps);
};

template<int (& ... Rs)[2]>
int first_of()
{
  return sizeof...(Rs) == 0 ? 0 : arrayed<Rs...>::count;
}

template<int (* ... Ps)()>
int run_all()
{
  return called<Ps...>::count;
}

int main()
{
  return arrayed<pair_one, pair_two>::count == 2 && arrayed<>::count == 0 &&
      arrayed<pair_one>::count == 1 && called<seven, eight>::count == 2 &&
      first_of<pair_one, pair_two>() == 2 && first_of<>() == 0 &&
      run_all<seven>() == 1
    ? 0 : 1;
}
