// N3485 focus: 14.5.5 [temp.class.spec] p1 and 14.5.5.2 [temp.class.order] p1 -
// a partial specialization declares a second body for the template it names,
// and an argument list that matches more than one pattern is read from the
// pattern every other one it matches is at least as general as.  A list that
// matches none is read from the primary's own pattern.
template<class T, class U>
struct pair_of {
  static const int which = 0;
};

template<class T>
struct pair_of<T, T> {
  static const int which = 1;
};

template<class T, class U>
struct pair_of<T *, U *> {
  static const int which = 2;
};

template<class T>
struct pair_of<T *, T *> {
  static const int which = 3;
};

// 14.5.5p2: one pattern is one declaration however many times it is written,
// so the body below belongs to the declaration above it rather than beside it.
template<class T>
struct pair_of<T &, T>;

template<class T>
struct pair_of<T &, T> {
  static const int which = 4;
};

int main()
{
  if (pair_of<char, long>::which != 0) { return 1; }
  if (pair_of<char, char>::which != 1) { return 2; }
  if (pair_of<char *, long *>::which != 2) { return 3; }
  if (pair_of<char *, char *>::which != 3) { return 4; }
  if (pair_of<char &, char>::which != 4) { return 5; }
  return 0;
}
