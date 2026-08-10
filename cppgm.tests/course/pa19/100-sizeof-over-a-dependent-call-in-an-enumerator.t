// 14.6p8 and 5.3.3p1: how large the operand of a `sizeof` written over a
// dependent expression is, an argument list is what says - so the reading of
// the pattern looks the names up and stands one value in its place, and each
// specialization reads the same expression against its own arguments.
struct base_kind {};
struct other_kind {};

template<class From, class To>
struct convertible {
  static char probe(To);
  static int probe(...);
  enum { value = sizeof(probe((From)0)) };
};

int main()
{
  return convertible<base_kind *, base_kind *>::value == 1 &&
                 convertible<base_kind *, other_kind *>::value == 4
             ? 0
             : 1;
}
