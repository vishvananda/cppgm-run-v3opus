// VALIDATION: compile-pass
// 14.3.2p1: an argument at a value place is the value it was converted to, and
// the name a specialization is written under is built from that value - so a
// value whose digits alone would not say which value it is carries the type it
// was converted to.  7.2p9 leaves an enumeration's value no spelling of its
// own and 2.14.6p1 gives `bool` two literals of its own.

enum policy
{
  p0,
  p1,
  p2
};

template<class T, T P>
struct control
{
  virtual ~control() noexcept {}

  virtual int value() noexcept
  {
    return (int)P;
  }
};

int main()
{
  control<policy, p2> chosen;
  control<bool, true> flagged;
  control<char, 'x'> lettered;
  control<int, -5> negative;
  return chosen.value() == 2 && flagged.value() == 1 &&
         lettered.value() == 120 && negative.value() == -5
    ? 0 : 1;
}
