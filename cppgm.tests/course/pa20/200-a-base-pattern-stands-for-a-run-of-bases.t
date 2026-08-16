// N3485 focus: 14.5.3 [temp.variadic] p4 with 10 [class.derived] p1 and 12.6.2
// [class.base.init] p1 - a base-specifier written `Bases...` declares one
// direct base per element of the run its packs are bound to, and a
// mem-initializer written `Base<I>(...)...` initializes one of them per element
// in the same lockstep.  A run of two is two subobjects and not a list this
// milestone refuses.

int trace[8];
int steps = 0;

template<int N> struct numbered
{
  numbered(int given) : held(given * N) { trace[steps++] = N; }
  int held;
  int of_numbered() { return held; }
};

struct carries { long spare; };

template<int... Ns> struct over : numbered<Ns>...
{
  over() : numbered<Ns>(2)... {}
};

template<class... Bases> struct all : Bases...
{
  all() : Bases()... {}
};

struct one { int a; one() : a(11) {} };
struct two { int b; two() : b(22) {} };

int main()
{
  over<1, 3, 5> made;
  // 14.5.3p4: the run holds three elements, so the class holds three base
  // subobjects, each standing at a byte of its own and each initialized by its
  // own reading of the pattern.
  numbered<1> * first = &made;
  numbered<3> * middle = &made;
  numbered<5> * last = &made;
  if (first->held != 2) { return 1; }
  if (middle->held != 6) { return 2; }
  if (last->held != 10) { return 3; }
  if (steps != 3) { return 4; }
  if (trace[0] != 1 || trace[1] != 3 || trace[2] != 5) { return 5; }
  if ((void *)first == (void *)middle) { return 6; }
  if ((void *)middle == (void *)last) { return 7; }
  if (first->of_numbered() != 2 || last->of_numbered() != 10) { return 8; }
  // 14.5.3p4 over a run of types, with a mem-initializer for each.
  all<one, two, carries> every;
  every.spare = 33;
  if (every.a != 11 || every.b != 22 || every.spare != 33) { return 9; }
  // 14.5.3p4 over a run of none: the class derives from nothing at all.
  all<> none;
  if (sizeof(none) != 1) { return 10; }
  return 0;
}
