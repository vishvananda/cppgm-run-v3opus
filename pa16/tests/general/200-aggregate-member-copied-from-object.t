// VALIDATION: compile-pass
// 8.5.1p2 copy-initializes a member whose type is an aggregate class from a
// clause of that same class type, rather than reading the clause as 8.5.1p11's
// elided braces around the members of the subaggregate.

struct Point {
  int x;
  int y;
};

struct Line {
  Point start;
  int weight;
};

Point origin = {3, 4};

int main()
{
  Line line = {origin, 5};
  return line.start.x == 3 && line.start.y == 4 && line.weight == 5 ? 0 : 1;
}
