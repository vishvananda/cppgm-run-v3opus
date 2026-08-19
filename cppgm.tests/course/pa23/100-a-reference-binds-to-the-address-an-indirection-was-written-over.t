// VALIDATION: compile-pass
// 5.3.1p1 and 8.5.3p5: the storage `*p` names begins where `p` points, so the
// address of that lvalue is the value `p` held - and an operand that is already
// an address needs nothing written to make one of it.  A constant is such an
// operand as much as a temporary is: an `addr` written over one would name the
// entry that constant numbers rather than the storage it addresses.

struct held { int first; int second; };

int & scalar()
{
  return *static_cast<int *>(0);
}

int * addressed()
{
  return &*static_cast<int *>(0);
}

int & member()
{
  return static_cast<held *>(0)->second;
}

int * member_address()
{
  return &static_cast<held *>(0)->second;
}

int (&arrayed())[3]
{
  return *static_cast<int (*)[3]>(0);
}

int main()
{
  return 0;
}
