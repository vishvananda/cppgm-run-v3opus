// VALIDATION: compile-pass
// N3485 focus: 14.7.3 [temp.expl.spec], 14.7.1 [temp.inst], 14.5.1.3 [temp.mem.func]
// 14.7.3p1 says which of the two definitions of a member of a class template
// specialization this unit holds: 14.7.1p1's reading of the pattern gives one
// to every argument list, and a `template<>` the program writes out for one
// list is that member's definition instead.  Which definition the unit holds is
// what the clause answers and not which of the two the program wrote first, so
// a member definition written *below* an explicit specialization of it is a
// reading 14.7.1p1 does not make for those arguments - at each of the tiers a
// definition of a member can be written at.

template<class T>
struct tag
{
  static const int value;
  static int plain();
  struct in { static int nested(); };
  tag();
  ~tag();
  operator int();
  int held;
};

int side = 0;

// Every `template<>` here stands above the template's own definition of the
// same member, which is the order the reading has to be left unmade in.
template<> const int tag<int>::value = 7;
template<> int tag<int>::plain() { return 7; }
template<> int tag<int>::in::nested() { return 7; }
template<> tag<int>::tag() { held = 7; }
template<> tag<int>::~tag() { side += 7; }
template<> tag<int>::operator int() { return 7; }

template<class T> const int tag<T>::value = 1;
template<class T> int tag<T>::plain() { return 1; }
template<class T> int tag<T>::in::nested() { return 1; }
template<class T> tag<T>::tag() { held = 1; }
template<class T> tag<T>::~tag() { side += 1; }
template<class T> tag<T>::operator int() { return 1; }

int through_pointer(const int* p)
{
  return *p;
}

int main()
{
  int built = 0;
  {
    tag<int> written;
    tag<char> read_again;
    built = written.held * 10 + read_again.held;
  }
  // The two destructors ran: 7 for the written definition and 1 for the
  // reading of the pattern.
  const int destroyed = side;
  return built == 71 && destroyed == 8 &&
         tag<int>::plain() == 7 && tag<char>::plain() == 1 &&
         tag<int>::in::nested() == 7 && tag<char>::in::nested() == 1 &&
         through_pointer(&tag<int>::value) == 7 &&
         through_pointer(&tag<char>::value) == 1
    ? 0 : 1;
}
