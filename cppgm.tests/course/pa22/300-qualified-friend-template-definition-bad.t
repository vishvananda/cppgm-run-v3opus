// VALIDATION: compile-fail
// N3485 focus: 11.3 [class.friend], 14.5.4 [temp.friend]
// 11.3p6: a friend declaration defines a function only where the name it
// writes is unqualified.  11.3p10's qualified declarator-id names a function
// the region that name reaches already declared, and a declaration made
// somewhere else is no place to write a body - which a head over the
// declaration changes nothing about.

namespace store
{

template<class T>
int weigh(T t);

}

class crate
{
  int mass;

public:
  crate()
    : mass(1)
  {
  }

  template<class T>
  friend int store::weigh(T t)
  {
    return (int)t;
  }
};

int main()
{
  return store::weigh(0);
}
