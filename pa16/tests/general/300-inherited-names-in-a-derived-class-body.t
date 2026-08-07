// VALIDATION: compile-pass
struct base
{
  typedef int count;
  enum tag { chosen = 4 };
  int held;
  int reached() const { return held; }
};

struct middle : base
{
};

struct derived : middle
{
  count total;
  int use() const { return reached() + chosen + total; }
};

int main()
{
  derived d;
  d.held = 1;
  d.total = 2;
  return d.use() == 7 ? 0 : 1;
}
