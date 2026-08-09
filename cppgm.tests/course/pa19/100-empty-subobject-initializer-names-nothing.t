// N3485 focus: 12.1p5 [class.ctor], 9p6 [class] and 12.8p15 [class.copy] - the
// constructor of a subobject that holds nothing has nothing to do, so the
// address the step named is no part of the program either.  A constructor given
// an object is the one exception: 12.8p15's transfer carries a value whatever
// the bytes come to, so the subobject it writes into is named however little a
// memberwise copy of an empty class writes.

struct empty
{
};

template<class T>
struct over : empty
{
  empty held;
  T value;

  over()
    : empty(),
      held(),
      value()
  {
  }

  over(const empty & given, T start)
    : empty(),
      held(given),
      value(start)
  {
  }
};

int main()
{
  over<int> built;
  empty source;
  over<int> transferred(source, 7);
  return built.value + transferred.value - 7;
}
