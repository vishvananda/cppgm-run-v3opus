// 5.3.3p1: the operand of `sizeof` is either an expression, which is
// unevaluated, or a parenthesized type-id - so `sizeof(pick((source)0))` in an
// enumerator's constant-expression is 5.4's cast inside a call and not a
// type-id, and 5.19 asks that expression for its size rather than its value.
struct source {};
struct other {};

char pick(source *);
int pick(...);

enum { reached = sizeof(pick((source *)0)) };
enum { missed = sizeof(pick((other *)0)) };

int main()
{
  return reached == 1 && missed == 4 ? 0 : 1;
}
