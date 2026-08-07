typedef double D;
typedef char * P;

int drop(D & d, P & p)
{
  d.~D();
  p.~P();
  (*&d).~D();
  return 0;
}

int main()
{
  D d = 1.5;
  P p = 0;
  return drop(d, p);
}
