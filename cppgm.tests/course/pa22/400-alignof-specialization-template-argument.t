// VALIDATION: compile-pass
// N3485 focus: 5.3.6 [expr.alignof], 5.3.3 [expr.sizeof], 14.7.1 [temp.inst],
// 14.6 [temp.res]
// 5.3.6p3 asks the same two things of `alignof`'s operand that 5.3.3p1 asks of
// `sizeof`'s: the type a reference refers to is the one measured, and it shall
// be complete - so 14.7.1p1 makes a specialization written under either
// operator one the use requires a definition of.  The demand is the same
// wherever the operator is written: at a template argument, in an array bound,
// in a static_assert, and inside a template definition that already settled the
// arguments of the specialization it names.

template<unsigned long N>
struct S
{
  static const unsigned long value = N;
};

template<class T>
struct wrap
{
  T first;
  double second;
};

template<unsigned long M>
struct box
{
  char pad[M];
};

template<class T>
struct reader
{
  typedef S<alignof(wrap<int>)> aligned;
  typedef S<sizeof(box<4>)> sized;
  static const unsigned long carried = alignof(wrap<char>) + sizeof(box<2>);
};

char bound[alignof(wrap<int>)];

static_assert(alignof(wrap<int>) == 8, "a settled specialization is complete");
static_assert(alignof(double &) == alignof(double), "5.3.6p3 reads a reference");

int main()
{
  unsigned long total = 0;
  total += S<alignof(wrap<int>)>::value;
  total += S<alignof(wrap<char> &)>::value;
  total += S<sizeof(box<alignof(wrap<int>)>)>::value;
  total += reader<int>::aligned::value;
  total += reader<int>::sized::value;
  total += reader<int>::carried;
  total += sizeof(bound);
  return total == 8 + 8 + 8 + 8 + 4 + 10 + 8 ? 0 : 1;
}
