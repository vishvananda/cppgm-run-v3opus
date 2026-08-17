// VALIDATION: compile-pass
// N3485 focus: 14.5.2 [temp.mem]
// A constructor template declared in a class body and defined outside it.
// 14.5.2p1 makes the member a function template whose name is the class's own,
// and 3.4.1p8 puts the head the definition wrote inside the class while its
// declarator is read - so `U` names the place that head declared.

struct point
{
  int v;

  template<class U>
  point(U u);
};

template<class U>
point::point(U u)
  : v(int(u) + 1)
{
}

int main()
{
  point p(6);
  return p.v == 7 ? 0 : 1;
}
