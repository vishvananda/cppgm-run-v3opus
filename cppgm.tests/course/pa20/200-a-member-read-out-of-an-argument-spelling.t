// VALIDATION: compile-pass
// 5.2.5p1: a member access is written in a constant expression wherever one
// stands, and 14.2 leaves the constant expression written as a template
// argument a spelling rather than a tree - so the access is read out of the
// words the same way the walk over a tree reads it out of the nodes, and both
// reach the class's members through the one lookup.

struct point
{
  int x;
  int y;

  constexpr int sum() const { return x + y; }
};

struct holder
{
  point held;

  constexpr int taken() const { return held.sum(); }
};

struct built
{
  int a;
  int b;

  constexpr built(int v) : a(v), b(v * 2) {}

  constexpr int both() const { return a + b; }
};

template<int N>
struct chosen
{
  static const int value = N;
};

int subobject = chosen<point{2, 5}.x>::value;
int called = chosen<point{2, 5}.sum()>::value;
int nested = chosen<holder{point{3, 4}}.held.sum()>::value;
int through = chosen<holder{point{3, 4}}.taken()>::value;
int constructed = chosen<built(6).both()>::value;
int arithmetic = chosen<point{2, 5}.x + point{3, 4}.y * 2>::value;

int main()
{
  return subobject + called + nested + through + constructed + arithmetic;
}
