// VALIDATION: compile-pass
// N3485 focus: 12.4 [class.dtor] pure virtual destructor
// 9.2's member-declarator writes a pure-specifier on a destructor exactly as
// on any other virtual member function, and 10.4p2 leaves the class abstract.
// Nothing here creates an object of it, so the unit holds no vtable at all.

struct B
{
  virtual ~B() = 0;
};

B *p;

int main()
{
  return p == 0 ? 0 : 1;
}
