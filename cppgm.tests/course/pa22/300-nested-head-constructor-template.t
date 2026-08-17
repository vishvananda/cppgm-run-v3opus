// VALIDATION: compile-pass
// N3485 focus: 14.5.2 [temp.mem]
// 14.5.2p3: the out-of-class definition of a constructor template of a class
// template writes the class template's head and the member's own.  12.6.2's
// mem-initializers are the specialization's to run, and the class they name
// members of is the one the definition belongs to rather than the region the
// reading of the pattern happens to stand in.

template<class T>
struct box
{
  T v;

  template<class U>
  box(U u);
};

template<class T>
template<class U>
box<T>::box(U u)
  : v(T(u) + T(2))
{
}

int main()
{
  box<int> b(5);
  return b.v == 7 ? 0 : 1;
}
