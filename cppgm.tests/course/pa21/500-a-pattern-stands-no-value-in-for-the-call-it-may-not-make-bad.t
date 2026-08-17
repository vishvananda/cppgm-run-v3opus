// VALIDATION: compile-fail
// N3485 focus: 14 [temp], 14.6 [temp.res], 7.1.5 [dcl.constexpr],
//              5.19 [expr.const]
//
// 14.6p8's stand-in is for what an *argument list* settles and for nothing
// else.  A call of a member the program did not write `constexpr` on is not
// one of those: 7.1.5p2 refuses it whatever the arguments are, so no reading of
// any specialization would answer it, and standing a value in for it would
// leave a program 5.19 refuses translated.
//
// So the gate is the declaration and not the reading: `counted` is a member of
// the class this pattern describes and its definition is one the reading of the
// pattern has not got, exactly as a `constexpr` member's is - and the refusal
// stands, because what the reading has not got is beside the point when 7.1.5p2
// has already answered.  The reading that stood nothing in ran out on what the
// program wrote and says so.

template<int N>
struct chosen
{
  static constexpr int value = N;
};

template<class T>
struct holder
{
  // 7.1.5p2: no declaration wrote `constexpr` on this one.
  static int counted() { return sizeof(T); }

  // 5.19: a template argument shall be a constant expression, and this call is
  // not one for any argument list at all.
  typedef chosen<counted()> measured;
};

typedef holder<int>::measured asked;

int main()
{
  return asked::value;
}
