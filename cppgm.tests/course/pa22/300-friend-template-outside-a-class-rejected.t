// VALIDATION: compile-fail
// N3485 focus: 11.3 [class.friend], 14.5.4 [temp.friend]
// 11.3p1 writes a friend declaration inside a class definition, and a head
// over one changes nothing about that: with no class around it there is no
// access to grant and nothing the declaration may name.

template<class U> friend class stranger;

int main()
{
  return 0;
}
