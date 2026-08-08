// N3485 5.2.10p5 [expr.reinterpret.cast] against 4.10p1 [conv.ptr]: a cast the
// program wrote whose operand is a null pointer constant is 4.10p1's conversion
// and produces the null pointer value, which nothing computes - except where
// `reinterpret_cast` names 5.2.10p5's reading of the integer as an address,
// which 5.2.10p1 lets no other conversion stand in for.

struct CastTarget
{
  virtual int value();

  int held;
};

int CastTarget::value()
{
  return held;
}

int main()
{
  CastTarget *written = (CastTarget *)0;
  CastTarget *checked = static_cast<CastTarget *>(0);
  CastTarget *read = reinterpret_cast<CastTarget *>(0);
  return written == checked && checked == read ? 0 : 1;
}
