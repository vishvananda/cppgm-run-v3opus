// VALIDATION: compile-fail
// N3485 focus: 9.2 [class.mem], 7.1.3 [dcl.typedef], 3.3.10 [basic.scope.hiding]
//
// The same question a class-name asks, asked by an enum-name and asked in a
// class: 9.2p1 lets a class declare a member once, and the typedef-name the
// member-specification already declared is what the enumeration's name would
// have to be declared over.

struct holder
{
  typedef int tag;

  enum tag
  {
    first
  };
};

int main()
{
  return 0;
}
