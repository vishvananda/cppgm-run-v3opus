// N3485 focus: 14.7.1 [temp.inst] p1 - the implicit instantiation of a class
// template specialization instantiates the declarations of its members and not
// their definitions, so a member function defined *outside* the class waits for
// the use that names it exactly as one defined inside it does.  `stashed` is
// the member nothing calls, and its body could not be instantiated for `int` at
// all; `taken` is the one a call names, and its body is written.

int side;

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

// 14.7.1p1: `T` is `int` in every specialization this unit makes, and `int` has
// no member to name - which is a body no argument list here could read.  It is
// no error until something asks for the definition.
template<class T>
int holder<T>::stashed()
{
  return held.width();
}

int main()
{
  holder<int> one;

  one.held = 3;

  side = one.taken();

  return side;
}
