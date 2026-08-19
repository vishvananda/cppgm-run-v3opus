// 12.9p1 twice over: a class that inherits a constructor template declares one
// of its own, which the class below it inherits in turn - and a parameter pack
// the base wrote stands for as many places as the run one call bound it to.

struct base
{
  int value;

  template <class... Us>
  base(int a, Us... us)
    : value(a + int(sizeof...(us)))
  {
  }
};

struct middle : base
{
  using base::base;
};

struct bottom : middle
{
  using middle::middle;
};

int main()
{
  bottom one(5);
  bottom three(5, 1, 2);
  return one.value == 5 && three.value == 7 ? 0 : 1;
}
