// VALIDATION: compile-pass
// N3485 focus: 14.5.3 [temp.variadic], 5.2.2 [expr.call], 14.3.2 [temp.arg.nontype]
// 14.5.3p4's expansion stands in every list a program can write, and 5.2.2p1's
// argument list written inside a template argument is one of them.  Whether an
// operand is a pattern is settled before it is read, because the `...` stands
// after it and a pattern that is a pack's own name is no operand at all while
// the pack is bound to a run.

template<unsigned long N>
struct S
{
  static const unsigned long value = N;
};

constexpr unsigned long tally()
{
  return 0;
}

template<class A1, class... A>
constexpr unsigned long tally(A1 first, A... rest)
{
  return static_cast<unsigned long>(first) + tally(rest...);
}

template<int... Ns>
struct values
{
  typedef S<tally(Ns...)> total;
};

template<class... Ts>
struct widths
{
  typedef S<tally(sizeof(Ts)...)> total;
};

template<class L, class R>
struct pair
{
  static const unsigned long value = sizeof(L) + 100 * sizeof(R);
};

template<class... Ls>
struct crossed
{
  template<class... Rs>
  struct with
  {
    typedef S<tally(pair<Ls, Rs>::value...)> total;
  };
};

int main()
{
  unsigned long total = 0;
  total += values<1, 2, 3, 4>::total::value;
  total += values<>::total::value;
  total += widths<char, int, double>::total::value;
  total += crossed<char, int>::with<int, char>::total::value;
  return total == 10 + 0 + 13 + 505 ? 0 : 1;
}
