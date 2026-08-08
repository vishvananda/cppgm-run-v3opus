// N3485 5.2.2p1 [expr.call]: a virtual function named with a qualified-id is
// the declaration that id names, so the call is not dispatched - through a
// pointer exactly as through an object expression.

struct Base
{
  virtual int f() const { return 1; }
};

struct Derived : Base
{
  int f() const { return 2; }
};

int through_pointer(const Derived *object)
{
  return object->Base::f() + object->f();
}

int main()
{
  Derived object;
  return through_pointer(&object) - 3;
}
