// VALIDATION: compile-pass
// N3485 focus: 14.7.1 [temp.inst], 14.5.1.3 [temp.mem.func], 14.6 [temp.res]
//
// 14.5.1.3p1's out-of-class member definition is read for every specialization
// the unit has already asked for, and for every one it asks for afterwards.
// The two specializations below are each named twice - once in a body 14.6p8
// reads where it stands and no call ever instantiates, and once by the use
// that actually asks for them - and the member definition stands between the
// two namings.  Each specialization is one declaration however many times it
// is named, so each gets that member definition exactly once.

template<class T>
struct pair_of
{
  T held;

  int size();
};

template<class U>
int uninstantiated(U v)
{
  pair_of<int> *first = 0;

  pair_of<char> *second = 0;

  return (first == 0) + (second == 0);
}

template<class T>
int pair_of<T>::size()
{
  return sizeof(T);
}

int main()
{
  pair_of<int> counted;

  pair_of<char> narrow;

  counted.held = 0;

  narrow.held = 0;

  return counted.size() + narrow.size() - 5;
}
