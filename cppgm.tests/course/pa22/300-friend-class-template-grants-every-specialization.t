// VALIDATION: compile-pass
// N3485 focus: 14.5.4 [temp.friend], 11.3 [class.friend]
// 11.3p11 puts the class a friend elaborated-type-specifier names in the
// namespace around the class the declaration is written in, and 14.5.4p1 makes
// one written under a head name a class *template* there - so the grant is
// between the two templates and reaches every specialization either of them
// makes, whichever of the two declarations the unit read first.

template<class T> class late;

class ahead
{
  int hidden;
public:
  ahead()
    : hidden(3)
  {
  }
  template<class U> friend class late;
};

class behind
{
  int hidden;
public:
  behind()
    : hidden(4)
  {
  }
  template<class U> friend class late;
};

template<class T> class late
{
public:
  static int read(ahead& a, behind& b)
  {
    return a.hidden + b.hidden;
  }
};

int main()
{
  ahead a;
  behind b;
  return late<int>::read(a, b) + late<char>::read(a, b) == 14 ? 0 : 1;
}
