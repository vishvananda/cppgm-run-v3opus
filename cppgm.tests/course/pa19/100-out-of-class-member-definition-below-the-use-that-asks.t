// N3485 focus: 14.6.4.1 [temp.point] p1 and 14.7.1 [temp.inst] p1 - a member
// whose out-of-class definition stands *below* the use that names it has a
// point of instantiation at the end of the translation unit, so this unit still
// writes the body.  A specialization the definition is written above gets the
// same body, and one made after it does too.

template<class T>
struct holder
{
  T held;

  int doubled();
  int quadrupled();
};

int above()
{
  holder<int> early;

  early.held = 2;

  return early.doubled();
}

template<class T>
int holder<T>::doubled()
{
  return quadrupled() / 2;
}

template<class T>
int holder<T>::quadrupled()
{
  return held + held + held + held;
}

int below()
{
  holder<char> late;

  late.held = 3;

  return late.doubled();
}

int main()
{
  return above() + below();
}
