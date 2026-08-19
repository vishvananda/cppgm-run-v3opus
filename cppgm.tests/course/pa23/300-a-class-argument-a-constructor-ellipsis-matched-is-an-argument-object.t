// 5.2.2p7 [expr.call] with 12.8p15 [class.copy]: an argument the ellipsis
// matched is passed as it stands, and a value of class type is storage rather
// than a value - so what crosses the boundary is the object, named after the
// call that asked for it exactly as 5.2.2p4's by-value parameter object is.
//
// A constructor is written against no parameter list a call of it can be read
// through - 12.1p1's own object parameter stands first - so its arguments are
// walked at a door of their own, and the answer there has to be the answer
// every other call gets.  Every spelling that reaches that door is written out
// here: a declaration's own parenthesized initializer, 5.2.3p1's cast, a
// mem-initializer, and 12.6.2p6's delegation.
struct pair
{
  int first;
  int second;
};

struct holder
{
  int kept;
  holder(int n, ...) : kept(n) {}
  holder() : holder(4, pair()) {}
};

holder declared(1, pair());

int through_a_cast()
{
  return holder(2, pair()).kept;
}

struct derived : holder
{
  derived() : holder(3, pair()) {}
};

derived built;

holder delegated;

int main()
{
  return 0;
}
