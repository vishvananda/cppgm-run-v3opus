// VALIDATION: compile-pass
// N3485 focus: 14.5.5 [temp.class.spec], 14.5.1.3 [temp.mem.class],
// 14.5.2 [temp.mem], 14.5.6.1 [temp.over.link]
// 14.5.5p1 leaves a member class template holding as many bodies as one written
// at namespace scope does, and 14.5.1.3p1's definition outside the class is a
// member of exactly one of them.  Which one is 14.5.6.1p5's signature of the
// arguments the declarator-id wrote, read over the places this definition's own
// head declared - which need not be spelled the way the pattern's own
// declaration spelled them, so `K *` here is the pattern `U *` was written as.
// 14.6p8 reads each definition where it stands against the class that body
// declares: `pointee` is a member of the pattern's own class and of no other,
// so a definition read against the primary's would not find it.

template<class T>
struct holder
{
  template<class U>
  struct slot
  {
    int width();
  };

  template<class U>
  struct slot<U *>
  {
    typedef U pointee;
    int width();
  };
};

template<class T>
template<class U>
int holder<T>::slot<U>::width()
{
  return static_cast<int>(sizeof(U));
}

template<class T>
template<class K>
int holder<T>::slot<K *>::width()
{
  pointee *reached = 0;
  return reached == 0 ? 100 : 0;
}

int main()
{
  holder<int>::slot<char> plain;
  holder<int>::slot<char *> pointed;
  return plain.width() == 1 && sizeof(pointed) == 1 ? 0 : 1;
}
