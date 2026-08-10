// N3485 focus: 14.1 [temp.param] p9 with 14.8.2 [temp.deduct] p5 - a default
// argument at a value place is 5.19's constant expression and not 8.1p1's
// type-id, so a type place it names has to *be* the deduced type where it is
// read rather than something a later substitution replaces.  A deduction that
// reached no argument for such a place therefore reads its default in a region
// binding every place before it to what the deduction settled, which is what
// lets `int N = sizeof(T)` and `int B = A + 1` name what came before them.
template<class T, int N = (int)sizeof(T)> int width(T) { return N; }

template<int A, int B = A + 1> int next() { return B; }

template<class T, class U = T, int N = (int)sizeof(U)> int held(T) { return N; }

int main()
{
  if (width('a') != 1) { return 1; }
  if (width(1) != 4) { return 2; }
  if (next<4>() != 5) { return 3; }
  if (next<4, 9>() != 9) { return 4; }
  if (held(1L) != 8) { return 5; }
  return 0;
}
