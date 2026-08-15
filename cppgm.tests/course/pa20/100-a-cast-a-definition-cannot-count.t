// N3485 focus: 14.6 [temp.res] p8 with 5.2.3 [expr.type.conv] p1 and 14.5.3
// [temp.variadic] p4 - a template definition is read before any argument list
// says how long the runs it names are, so an entry written `pattern...` stands
// for itself there and the cast whose operand list came to it is worth nothing
// the definition knows.  The reading stands a value in for that, exactly as it
// does for `sizeof...`, which is what lets 8.3.4p1's bound and 5.19's
// static_assert be written with one at all - and each argument list that
// arrives afterwards is read for itself.
template<int... N>
struct room {
  int slots[int(N...)];
  static const int held = int(N...);
  static_assert(int(N...) != 0, "a cast the definition cannot count");
};

// 14p1: a template a program never names is read all the same, and this one
// says nothing an argument list has settled.
template<int... N>
struct never_named {
  int slots[int(N...)];
  static const int held = int(N...);
};

template<class... A>
struct measured {
  // The same three lists inside a definition, over a run the head declared.
  static int one(A... a)
  {
    const int paren(a...);
    int direct(a...);
    return paren * 100 + direct * 10 + int(a...);
  }
};

int main()
{
  room<3> three;
  if (sizeof(three.slots) / sizeof(int) != 3) { return 1; }
  if (room<3>::held != 3) { return 2; }
  room<5> five;
  if (sizeof(five.slots) / sizeof(int) != 5) { return 3; }
  if (room<5>::held != 5) { return 4; }
  if (measured<int>::one(2) != 222) { return 5; }
  return 0;
}
