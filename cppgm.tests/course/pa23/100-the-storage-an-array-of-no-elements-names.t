// 8.5.1p4 with 1.8p5: an array of unknown bound whose initializer-list came to
// no clause at all has a bound of zero, and the complete object it declares
// still occupies storage of its own.
template<class... A> int walk(A... a)
{
  int held[] = { a... };
  char narrow[] = { a... };
  return (void *)held == (void *)narrow ? 1 : 0;
}
int main()
{
  return walk();
}
