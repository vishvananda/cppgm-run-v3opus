// VALIDATION: compile-pass
// N3485 focus: 3.2 [basic.def.odr], 9.4.2 [class.static.data], 14.7.1 [temp.inst]
// 14.7.1p1 makes the definition of a static data member of a class template
// specialization the template's own read again for those arguments, so it is
// storage no unit wrote and belongs to the program where the program reaches
// it.  3.2p3 says which naming reaches it: a use that reads nothing but the
// value 9.4.2p3 folded reads no object at all, while one that reads the place -
// an address taken, a reference bound - is the use that asks for the storage.

template<class T>
struct box
{
  static constexpr int folded = 4;
  static constexpr int addressed = 5;
  static constexpr int bound = 6;
};

template<class T>
constexpr int box<T>::folded;

template<class T>
constexpr int box<T>::addressed;

template<class T>
constexpr int box<T>::bound;

template<int N>
struct tally
{
  static int n() { return N; }
};

int through_pointer(const int* p)
{
  return *p;
}

int through_reference(const int& r)
{
  return r;
}

int main()
{
  // Read for the value alone: an array bound, a template argument, a
  // comparison.  None of the three names the object.
  char sized[box<int>::folded];
  const int listed = tally<box<int>::folded>::n();
  // Read as a place: the address, and a reference bound to it.
  const int* held = &box<int>::addressed;
  const int& tied = box<int>::bound;
  return sizeof sized == 4 && listed == 4 && box<int>::folded == 4 &&
         through_pointer(held) == 5 && through_reference(tied) == 6
    ? 0 : 1;
}
