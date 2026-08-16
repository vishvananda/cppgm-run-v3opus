// N3485 focus: 11.2 [class.access.base] p4 and p5 with 8.5 [dcl.init] p14 and
// 6.6.3 [stmt.return] p2 - a conversion to a base class is well formed where the
// access every base-specifier between the two classes gives reaches the place
// the conversion is *written*.  The initializer of a declaration and the operand
// of a return are written where the declaration and the return are, so a member
// of the class that named the base reaches its own base there exactly as an
// argument or a cast written in the same body does.

struct held
{
  int n;
};

struct keeps : protected held
{
  void hold(int value) { n = value; }
  int from_initializer()
  {
    // 11.2p4: written in a member of the class that named the base, which
    // reaches a base its own base-specifier made protected.
    held * through = this;
    return through->n;
  }
  held * from_return() { return this; }
  int from_argument() { return takes(this); }
  int from_reference()
  {
    const held & bound = *this;
    return bound.n;
  }
  static int takes(held * p) { return p->n; }
};

struct below : keeps
{
  int from_derived()
  {
    // 11.2p1: a class derived from the one that named the base reaches a
    // protected base-specifier of it.
    held * through = this;
    return through->n;
  }
};

struct hidden : private held
{
  void hold(int value) { n = value; }
  int reaches()
  {
    held * through = this;
    return through->n;
  }
  friend int befriended(hidden & object);
};

int befriended(hidden & object)
{
  // 11.2p1 and 11.3p1: a friend of the class reaches what the class reaches.
  held * through = &object;
  return through->n;
}

int main()
{
  below one;
  one.hold(3);
  if (one.from_initializer() != 3) { return 1; }
  if (one.from_return()->n != 3) { return 2; }
  if (one.from_argument() != 3) { return 3; }
  if (one.from_reference() != 3) { return 4; }
  if (one.from_derived() != 3) { return 5; }
  hidden two;
  two.hold(4);
  if (two.reaches() != 4) { return 6; }
  if (befriended(two) != 4) { return 7; }
  return 0;
}
