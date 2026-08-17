// VALIDATION: compile-pass
// N3485 focus: 5.3.3 [expr.sizeof], 14.7.1 [temp.inst], 14.2 [temp.names]
// 5.3.3p1's other arm is an expression, and how large the type it has is, is
// 13.3's answer over a typed operand rather than anything a reading of the
// spelling could reach - so the operand is answered off the tree the parse kept
// beside the spelling it flattened into the name.  5.3.3p2 answers for the type
// a reference refers to, and 14.7.1p1 makes a specialization written under the
// operator one this use requires complete.

template<unsigned long N>
struct S
{
  static const unsigned long value = N;
};

template<unsigned long M>
struct box
{
  static const unsigned long value = M;
  char pad[3];
};

typedef char (&three)[3];
typedef char (&six)[6];

three pick(int);
six pick(long);

struct probe
{
  template<class Key>
  static three key_of(Key *);
};

int main()
{
  unsigned long total = 0;
  total += S<sizeof(pick(1))>::value;
  total += S<sizeof(pick(1L))>::value;
  total += S<sizeof(three)>::value;
  total += S<sizeof(probe::key_of(static_cast<int *>(0)))>::value;
  total += S<sizeof(box<4>)>::value;
  total += S<alignof(double) + sizeof(int)>::value;
  return total == 3 + 6 + 3 + 3 + 3 + 12 ? 0 : 1;
}
