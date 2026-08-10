// N3485 focus: 14.7.2 [temp.explicit] p8 and p11 - an explicit instantiation
// definition of a class template specialization is the one declaration 3.2p3
// has no use to point at, so a member whose definition 14.7.1p1 put aside is
// asked for it here as a call would ask.  Both members are defined outside the
// class and neither is called anywhere in this unit.

template<class T>
struct holder
{
  T held;

  int taken();
  int stashed();
};

template<class T>
int holder<T>::taken()
{
  return held + held;
}

template<class T>
int holder<T>::stashed()
{
  return held - held;
}

template struct holder<int>;

int main()
{
  return 0;
}
