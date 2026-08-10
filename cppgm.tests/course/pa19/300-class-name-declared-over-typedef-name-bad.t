// VALIDATION: compile-fail
// N3485 focus: 3.3.10 [basic.scope.hiding], 7.1.3 [dcl.typedef], 9.2 [class.mem]
//
// 7.1.3p3 lets a region declare a typedef-name it already declares, and
// 7.1.3p6 lets a class declare a typedef-name for a class-name it already
// declares.  Neither runs the other way: what 3.3.10p2 binds a class-name as
// is the tag an elaborated-type-specifier reaches, and a typedef-name is not
// one, so a class declared where a typedef-name of that spelling stands
// declares one name as two different kinds of type.

typedef int amount;

struct amount
{
  int held;
};

int main()
{
  amount a;

  a.held = 0;

  return a.held;
}
