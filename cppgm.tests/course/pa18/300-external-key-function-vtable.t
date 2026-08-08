// N3485 10.3 [class.virtual] and the ABI's key function.
// The first non-pure, non-inline virtual member of a class is defined in
// exactly one translation unit, and that unit holds the class's table.  This
// unit only declares it, so the constructor here names a table it does not
// define.

struct KeyElsewhere
{
  KeyElsewhere() {}
  virtual int value();
};

int use()
{
  KeyElsewhere object;
  return 0;
}
