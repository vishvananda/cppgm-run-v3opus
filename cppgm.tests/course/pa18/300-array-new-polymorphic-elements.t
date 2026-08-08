// N3485 12.1p11 [class.ctor] and 5.3.4p15 [expr.new]: constructing an object of
// a polymorphic class writes the vpointer whatever else its constructor comes
// to, so each element a new-expression creates is constructed - a constructor
// whose body is empty and whose class initializes no member of its own is no
// exception, because the vpointer is what its call is for.

struct ArrayElement
{
  ArrayElement() noexcept;
  ~ArrayElement();

  virtual int value();

  int held;
};

ArrayElement::ArrayElement() noexcept
{
}

ArrayElement::~ArrayElement()
{
  held = 0;
}

int ArrayElement::value()
{
  return 1;
}

int main()
{
  ArrayElement *elements = new ArrayElement[3];
  ArrayElement *first = elements;
  int seen = first->value();
  delete[] elements;
  return seen == 1 ? 0 : 1;
}
