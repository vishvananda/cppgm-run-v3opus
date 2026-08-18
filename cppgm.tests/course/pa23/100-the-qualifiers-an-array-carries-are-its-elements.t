// 3.9.3p5: a cv-qualifier written on an array is its *element's*, and the array
// is considered to carry the same qualification - so every reader that compares
// two types level by level has to read past the dimensions to find the
// qualifiers of the level it is comparing.  4.4's qualification conversion is
// what each of them is asking about, and it has four doors: an initialization,
// an argument reaching a parameter, 5.9p2's composite pointer type of two
// operands, and 14.8.2.1p2's conversion of an argument to what a deduction made
// of the parameter.
int room[3] = { 1, 2, 3 };
const int held[3] = { 4, 5, 6 };

int first_of(const int (*p)[3])
{
  return (*p)[0];
}

template<class T>
T deduced_first(const T (*p)[3])
{
  return (*p)[0];
}

// 4.4p4's second condition still has to hold: a level whose qualifiers differ
// needs `const` at every level above it, so this one converts and the pointer
// to a pointer below does not.
int through_two(const int (*const *p)[3])
{
  return (**p)[0];
}

template<class T, class = decltype(static_cast<const T (*)[3]>((T (*)[3])0))>
char qualifies(int);

template<class>
long qualifies(...);

int main()
{
  // The initialization, which adds `const` and `volatile` at the element of an
  // array a pointer names.
  const int (*added)[3] = &room;
  volatile int (*shaken)[3] = &room;
  const volatile int (*both)[3] = &room;

  // The argument, and the same conversion off a two-dimensional array's decay.
  int rows[2][3] = { { 7, 8, 9 }, { 1, 2, 3 } };
  const int (*decayed)[3] = rows;

  // 5.9p2's composite pointer type, of one qualified operand and one not.
  const int (*composite)[3] = true ? &room : &held;

  int (*plain)[3] = &room;
  const int (*const *above)[3] = &plain;

  const int total = (*added)[0] + (*shaken)[1] + (*both)[2] + decayed[1][0] +
    (*composite)[0] + first_of(&room) + deduced_first(&room) +
    through_two(above);

  static_assert(sizeof(qualifies<int>(0)) == sizeof(char),
                "adding const at an array's element is 4.4's conversion");

  return total == 1 + 2 + 3 + 1 + 1 + 1 + 1 + 1 ? 0 : 1;
}
