// VALIDATION: compile-pass
// N3485 focus: 5.2.3 [expr.type.conv], 8.5.1 [dcl.init.aggr], 12.2 [class.temporary]
//
// 5.2.3p3's `T{...}` over an *array* type is the one spelling that writes an
// array prvalue, and 12.2p1 makes it an object - so the function gives it
// storage and the clauses initialize its elements there, which is the walk a
// declaration of an array already gets over storage no declaration named.
// 8.5.3p5 names that storage after what asked for it, so a discarding names it
// one way and every other reading another; and 4.2 marks the point a name of
// the array became a pointer view of it, at a subscript and at an argument
// alike.  14.5.3p4's expansion writing the clauses is the shape that needs each
// element of the run to reach an element of the array.

typedef int A3[3];
typedef int M23[2][3];

struct C
{
  int a;
};

typedef C CA2[2];

int sink = 0;

int bump(int n)
{
  sink += n;
  return n;
}

int use(const int * p)
{
  return p[0] + p[2];
}

template<class... T>
int spread()
{
  using A = int[sizeof...(T)];
  (void)A{ (bump(sizeof(T)), 0)... };
  return 0;
}

int main()
{
  (void)A3{};
  (void)A3{ bump(1) };
  (void)M23{ { bump(2), bump(4), bump(8) }, { bump(16), bump(32), bump(64) } };
  (void)CA2{ { bump(128) }, { bump(256) } };
  const int width = sizeof(A3{ 1, 2, 4 });
  const int read = use(A3{ 1, 0, 2 }) + A3{ 4, 8, 16 }[1];
  const int expanded = spread<char, short, int>();
  return expanded + (width != 12) + (read != 11) + (sink != 518);
}
