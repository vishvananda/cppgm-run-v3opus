// VALIDATION: compile-pass
// N3485 focus: 14.7.3 [temp.expl.spec], 14.5.2 [temp.mem]
// A `template<>` head declares no place at all, so what stands under it is
// written where the template-declaration stands: `template<> int box<int>::id();`
// declares the member the class specialization already has, and a second head
// under it is the member template's own.  14.7.3p1 then makes the definition
// written for one class specialization the one every naming of that member of
// that specialization is read from, and leaves the primary's body to the rest.

template<class T>
struct box
{
  static int id() { return 1; }

  template<class U>
  int apply(U) { return 1; }
};

template<>
int box<int>::id();

template<>
template<class U>
int box<int>::apply(U)
{
  return 2;
}

template<>
template<class U>
int box<long>::apply(U)
{
  return 3;
}

template<>
int box<int>::id()
{
  return 2;
}

int main()
{
  box<int> written;
  box<long> other;
  box<char> pattern;
  const bool ids = box<char>::id() == 1 && box<int>::id() == 2;
  const bool members = written.apply(0) == 2 && other.apply(0L) == 3 &&
    pattern.apply('a') == 1;
  return ids && members ? 0 : 1;
}
