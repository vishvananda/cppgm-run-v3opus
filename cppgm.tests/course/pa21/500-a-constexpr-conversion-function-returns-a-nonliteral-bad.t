// VALIDATION: compile-fail
// N3485 focus: 7.1.5 [dcl.constexpr], 12.3.2 [class.conv.fct]
//
// 12.3.2p1 makes a conversion function an ordinary member function whose name
// is a type, so 7.1.5p3's second bullet asks of that type what it asks of every
// other constexpr function's return type.  A conversion function reaches
// neither the walk that reads a function definition nor the one that reads a
// constructor, so the requirement is asked at a third door - and `nonliteral`,
// whose destructor 3.9p10's first bullet reads, is not a type any of them may
// return.

struct nonliteral
{
  ~nonliteral()
  {
  }
};

struct source
{
  constexpr operator nonliteral() const
  {
    return nonliteral();
  }
};

int main()
{
  return 0;
}
