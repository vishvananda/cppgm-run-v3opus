// N3485 focus: 8.5.3p5 [dcl.init.ref] and 12.2p1 [class.temporary] - a
// reference bound to a prvalue binds a temporary, and 3.10p1 leaves a prvalue a
// value rather than an object, so the function gives that temporary storage of
// its own.  12.2p1 names the storage after the place that asked for it.

int taken(const long & value)
{
  return static_cast<int>(value);
}

const int & returned()
{
  return 5;
}

int declared()
{
  const long & held = 3;
  int && moved = 4;
  return static_cast<int>(held) + moved;
}

int converted()
{
  int narrow = 9;
  const long & widened = narrow;
  return static_cast<int>(widened);
}

int main()
{
  // 12.2p3 ends the temporary `returned` bound at the end of that return, so
  // the caller reads nothing through the reference: the shape this fixture is
  // about is the storage the return gave that temporary.
  const int & alias = returned();
  (void)&alias;
  return taken(1) + declared() + converted() - 17;
}
