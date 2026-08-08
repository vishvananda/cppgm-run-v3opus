// N3485 12.6.2p6 [class.base.init]: a delegating constructor initializes no
// base and no member of its own - the target constructor initializes the whole
// object - so the vpointer 12.1p11 writes is the target's store and not one the
// delegating constructor repeats before calling it.

struct Delegating
{
  Delegating();
  Delegating(int given);

  virtual int value();

  int held;
};

Delegating::Delegating()
	: Delegating(7)
{
}

Delegating::Delegating(int given)
	: held(given)
{
}

int Delegating::value()
{
  return held;
}

int main()
{
  Delegating object;
  Delegating *through = &object;
  return through->value() == 7 ? 0 : 1;
}
