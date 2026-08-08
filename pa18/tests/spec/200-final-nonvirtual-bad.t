// VALIDATION: compile-fail
// N3485 focus: 9.2 [class.mem] virt-specifier placement
// Expected: a virt-specifier-seq appears only in the declaration of a virtual
// member function, so `final` on one that is not virtual is ill formed.

struct B
{
  void f() final;
};
