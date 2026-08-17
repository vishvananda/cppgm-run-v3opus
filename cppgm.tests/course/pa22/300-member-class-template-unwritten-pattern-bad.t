// VALIDATION: compile-fail
// N3485 focus: 14.5.5 [temp.class.spec], 14.5.1.3 [temp.mem.class],
// 14.5.2 [temp.mem]
// 9.4.2p1 with 14.5.1.3p1: a definition written outside the class declares a
// member of a class the program declared, and the arguments the declarator-id
// wrote here name a body no member of `holder` has - so the definition is a
// member of nothing rather than a member of the primary that stands beside it.

template<class T>
struct holder
{
  template<class U>
  struct slot
  {
    int width();
  };
};

template<class T>
template<class U>
int holder<T>::slot<U *>::width()
{
  return 100 + static_cast<int>(sizeof(U));
}

int main()
{
  holder<int>::slot<char> plain;
  return plain.width() == 1 ? 0 : 1;
}
