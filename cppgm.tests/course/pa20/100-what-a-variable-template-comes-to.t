// N3485 focus: 14.5.1 [temp.class] p1 with 5.19 [expr.const] p2 - a head over an
// object declares a template whose specialization is the constant its
// initializer evaluates to, so it stands wherever a constant is asked for and
// declares no object anywhere.  14.5.5's pattern and 14.7.3's explicit
// specialization each answer for one argument list as they do for a class.
template<class T>
constexpr unsigned long width = sizeof(T);

template<class T>
constexpr int rank = 0;

template<class T>
constexpr int rank<T *> = 1;

template<class T>
constexpr int rank<T &> = 2;

template<>
constexpr int rank<char> = 7;

template<class T>
struct box {
  static const int value = rank<T>;
};

template<class T>
unsigned long read()
{
  return width<T>;
}

int main()
{
  if (width<char> != 1) { return 1; }
  if (read<long>() != sizeof(long)) { return 2; }
  if (rank<long> != 0) { return 3; }
  if (rank<long *> != 1) { return 4; }
  if (rank<long &> != 2) { return 5; }
  if (rank<char> != 7) { return 6; }
  if (box<long *>::value != 1) { return 7; }
  return 0;
}
