// VALIDATION: compile-pass
// N3485 focus: 12.1 [class.ctor], 12.6.2 [class.base.init], 14.5.2 [temp.mem]

struct root
{
  int n;

  template<class U>
  root(U start)
    : n((int)start * 3)
  {
  }
};

struct only_base : root
{
  only_base()
    : root(2)
  {
  }
};

struct both_ways : root
{
  both_ways()
    : root('\x01')
  {
  }
};

int main()
{
  only_base under;
  both_ways twice;
  root whole('\x02');
  return under.n == 6 && twice.n == 3 && whole.n == 6 ? 0 : 1;
}
