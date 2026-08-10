// 13.3.1.2p1 and 13.6p17: an operator expression with an operand of
// enumeration type gathers the built-in candidates beside the operator
// functions ordinary lookup and 3.4.2 reach, and the built-in `operator|` is
// written over 4.5p3's promoted operands - so it reads two enumerators through
// an integral promotion where the class `operator|` a namespace declares would
// need a user-defined conversion of each.
namespace units {

struct width {
  int held;
  width(int given = 0) : held(given) {}
};

width operator|(width, width const &);
width operator&(width, width const &);

enum option_type {
  first_option = 1 << 2,
  second_option = 1 << 3
};

option_type operator&(option_type, option_type);

int combined(option_type flags)
{
  return ((int)first_option | second_option) & flags;
}

}

int main()
{
  return units::combined(units::second_option) == 8 ? 0 : 1;
}
