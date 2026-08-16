// VALIDATION: compile-fail
// N3485 focus: 7.1.5 [dcl.constexpr], 12.1 [class.ctor]
//
// 7.1.5p4's second bullet says each parameter type of a constexpr constructor
// shall be a literal type, which is the same sentence 7.1.5p3 writes for every
// other constexpr function.  A constructor is declared by a walk of its own -
// no return type, no declarator-id a lookup binds - so the requirement has to
// be asked there as well as at an ordinary function definition, and this is the
// declaration that asks it.  `nonliteral` declares a constructor of its own and
// initializes nothing, so 3.9p10 gives it no constexpr constructor and no
// literal type.

struct nonliteral
{
  nonliteral();
};

struct holder
{
  constexpr holder(nonliteral)
  {
  }
};

int main()
{
  return 0;
}
