// VALIDATION: compile-fail
// 6.7p3 at the one declaration that cannot be written without an initializer.
// 8.3.2p2 makes every reference of automatic storage duration one a declaration
// binds where it stands, so a label written below it is always a jump into the
// scope of something the jump itself never bound - and the exemption p3 gives a
// declaration with no initializer has nothing to be asked of, because a
// reference is of no type it names and wrote an initializer anyway.

int run(int selector, int held)
{
  switch (selector)
  {
  case 0:
    int &bound = held;
    return bound;
  default:
    return 2;
  }
}

int main()
{
  return run(1, 3);
}
