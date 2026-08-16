// VALIDATION: compile-fail
// N3485 focus: 7.1.5 [dcl.constexpr], 12.1 [class.ctor]
//
// 7.1.5p9 says a `constexpr` object shall be initialized, and 8.5p6 says a
// declaration that wrote no initializer initializes one of class type by calling
// the class's default constructor.  Here that constructor is the one 12.1p5
// gives a class that declared none, and what it does for a member of scalar type
// with no brace-or-equal-initializer is nothing at all - so it is no constexpr
// constructor and `blank` is initialized by nothing.  The requirement is on the
// declaration and not on a use: nothing below reads `blank`, and the program is
// ill-formed all the same.

struct uninitialized_member
{
  int value;
};

constexpr uninitialized_member blank;

int main()
{
  return 0;
}
