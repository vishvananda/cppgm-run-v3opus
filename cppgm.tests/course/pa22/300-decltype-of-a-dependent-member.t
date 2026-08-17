// VALIDATION: compile-pass
// N3485 focus: 14.6.2.2 [temp.dep.expr], 7.1.6.2 [dcl.type.simple], 14.6.2 [temp.dep]
// 7.1.6.2p4 asks what an id-expression *names* - an object, a function or an
// enumerator - and a name written after a prefix that depends on a template
// parameter names none of the three until the arguments arrive.  So the
// specifier stands for a type of its own and 14.7.1p1 reads the same
// id-expression again against the class the arguments made, which is the door
// every other dependent operand of `decltype` already came back through.

template<class D>
struct box
{
  static decltype(D::count) held;
  typedef decltype(D::inner::wide) reached;
  typedef decltype((D::count)) bound;

  static decltype(D::count) doubled() { return D::count * 2; }
  static int taking(decltype(D::count) v) { return static_cast<int>(v); }
};

template<class D>
decltype(D::count) box<D>::held;

template<class T>
struct pair_of
{
  static const int n = 2;
};

template<class T>
struct with_base : pair_of<T>
{
  // 14.6.2.1p6's member of an unknown specialization is the operand here.
  typedef decltype(with_base::n) counted;
};

struct thing
{
  static int count;
  struct inner { static long wide; };
};

int thing::count = 3;
long thing::inner::wide = 4;

int main()
{
  box<thing>::held = 5;
  box<thing>::reached wide = 6;
  box<thing>::bound bound = thing::count;
  with_base<int>::counted counted = with_base<int>::n;
  return box<thing>::held == 5 && wide == 6 && bound == 3 &&
         box<thing>::doubled() == 6 && box<thing>::taking(7) == 7 &&
         counted == 2
    ? 0 : 1;
}
