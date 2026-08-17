// VALIDATION: compile-pass
// N3485 focus: 14.7.3 [temp.expl.spec], 14.7.1 [temp.inst], 14.5.5 [temp.class.spec]
// 14.7.1p1 makes the definition of a member of a class template specialization
// the template's own body read again for those arguments, and 14.7.3p1 lets the
// program write one out for exactly one argument list instead - so the written
// one is the definition that specialization has and the read one is no second
// definition of anything.  Every other list is still read from the body it
// matched, which for `tag<T*>` is the pattern's and not the primary's.

template<class T>
struct tag
{
  static int first() { return 1; }
  static int second() { return 3; }
  static const int mark;
};

template<class T>
const int tag<T>::mark = 5;

template<class T>
struct tag<T*>
{
  static int first() { return 10; }
  static int second() { return 30; }
};

template<>
int tag<int>::first()
{
  return 2;
}

template<>
int tag<int>::second()
{
  return 4;
}

template<>
const int tag<int>::mark = 6;

template<>
int tag<int*>::first()
{
  return 20;
}

int main()
{
  const bool primary = tag<char>::first() == 1 && tag<char>::second() == 3 &&
    tag<char>::mark == 5;
  const bool written = tag<int>::first() == 2 && tag<int>::second() == 4 &&
    tag<int>::mark == 6;
  const bool pattern = tag<char*>::first() == 10 && tag<char*>::second() == 30;
  const bool over_pattern = tag<int*>::first() == 20 &&
    tag<int*>::second() == 30;
  return primary && written && pattern && over_pattern ? 0 : 1;
}
