// 12.9p3 leaves a base constructor out of what is inherited where a constructor
// this class *declared itself* already takes its parameters.  A constructor
// another using-declaration inherited is no such declaration: 12.9p1 declares
// one here for each candidate of each base, so two bases whose constructor
// templates agree on a parameter-type-list leave two declarations of one list
// and 13.3 no best one where a use is written.

struct left
{
  int value;

  left()
    : value(0)
  {
  }

  template <class U>
  left(U u)
    : value(int(u))
  {
  }
};

struct right
{
  int other;

  right()
    : other(0)
  {
  }

  template <class U>
  right(U u)
    : other(int(u) + 1)
  {
  }
};

struct both : left, right
{
  using left::left;
  using right::right;
};

int main()
{
  both one(1);
  return one.value == 1 ? 0 : 1;
}
