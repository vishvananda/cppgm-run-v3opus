struct Point {
  double x;
  float y;

  Point() : x(0), y(0) {}

  void scale() { x = x + .5; y = y * .25e1f; }
};

int main()
{
  Point p;
  p.y = .75f;
  p.scale();
  return p.x == .5 && p.y == 1.875f ? 0 : 1;
}
