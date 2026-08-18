// VALIDATION: compile-pass
// N3485 focus: 8.3.4 [dcl.array], 14.5.5 [temp.class.spec], 14.8.2.5 [temp.deduct.type]
// 8.3.4p1's bound is a constant expression, and a pattern may write a template
// parameter as the whole of one.  Such a bound is a place and not a number: it
// is deduced from the number the argument's own declarator arrived at, it tells
// two patterns apart wherever two places are written, and 14.3p1's substitution
// puts the argument's number back wherever the pattern's body wrote the type.
// 14.5.5.2's ordering reads two such bounds against each other as it reads two
// type places, so a pattern that writes a number or a cv-qualifier beside one is
// the more specialized of the two.

template<class C>
struct probe
{
  static const unsigned long which = 0;
};

template<class T, unsigned long N>
struct probe<T[N]>
{
  typedef const T *pointer;
  static const unsigned long which = 1;
  static const unsigned long count = N;
};

template<class T>
struct probe<T[3]>
{
  static const unsigned long which = 2;
  static const unsigned long count = 3;
};

template<class T, unsigned long N>
struct probe<const T[N]>
{
  static const unsigned long which = 3;
  static const unsigned long count = N;
};

template<class A, class B>
struct pair_probe
{
  static const unsigned long which = 0;
};

template<class T, unsigned long N, unsigned long M>
struct pair_probe<T[N], T[M]>
{
  static const unsigned long which = N * 10 + M;
};

template<class C>
struct nested
{
  static const unsigned long which = 0;
};

template<class T, unsigned long N>
struct nested<T[N][2]>
{
  static const unsigned long which = N;
};

template<class T, unsigned long N>
unsigned long length(T (&)[N])
{
  return N;
}

template<class T, unsigned long N>
struct holder
{
  T cells[N];
  unsigned long span();
  static const unsigned long bytes = sizeof(T[N]);
};

template<class T, unsigned long N>
unsigned long holder<T, N>::span()
{
  return sizeof(cells) / sizeof(T);
}

int main()
{
  int five[5];
  char three[3];
  holder<int, 4> box;
  box.cells[0] = 6;
  box.cells[3] = 7;
  probe<int[5]>::pointer read = 0;
  return probe<int[5]>::which == 1 && probe<int[5]>::count == 5 &&
                 probe<int[3]>::which == 2 &&
                 probe<const int[9]>::which == 3 &&
                 probe<const int[9]>::count == 9 && probe<int>::which == 0 &&
                 pair_probe<int[2], int[7]>::which == 27 &&
                 pair_probe<int[2], char[7]>::which == 0 &&
                 nested<char[6][2]>::which == 6 &&
                 nested<char[2][6]>::which == 0 && length(five) == 5 &&
                 length(three) == 3 && box.span() == 4 &&
                 holder<char, 5>::bytes == 5 &&
                 sizeof(box.cells) == 4 * sizeof(int) &&
                 box.cells[0] + box.cells[3] == 13 && read == 0
             ? 0
             : 1;
}
