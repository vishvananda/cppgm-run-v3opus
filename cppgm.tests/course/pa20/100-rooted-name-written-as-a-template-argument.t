// N3485 focus: 3.4.3 [basic.lookup.qual] p1 - a name written `::x` names the
// global namespace's `x`, and 14.2 [temp.names] p1 lets it carry a
// template-argument-list of its own.  A type argument and a value argument are
// each one name however it is rooted.
template<class T>
struct holder {
  static const int value = T::value;
};

struct plain {
  static const int value = 7;
};

template<class T>
struct rooted {
  static const int value = 9;
};

const int shared = 4;

template<int N>
struct box {
  static const int value = N;
};

int main()
{
  if (holder< ::plain>::value != 7) { return 1; }
  if (holder< ::rooted<int> >::value != 9) { return 2; }
  if (box< ::shared>::value != 4) { return 3; }
  if (box< ::shared + 1>::value != 5) { return 4; }
  return 0;
}
