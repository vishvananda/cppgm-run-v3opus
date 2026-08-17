// VALIDATION: compile-pass
// N3485 focus: 12.3.2 [class.conv.fct], 14.5.2 [temp.mem],
// 14.8.2.3 [temp.deduct.conv]

struct held
{
  int n;

  held(int start)
    : n(start)
  {
  }

  template<class U>
  operator U() const;
};

template<class U>
held::operator U() const
{
  return (U)(n + 1);
}

struct base_of
{
  int n;

  base_of()
    : n(4)
  {
  }

  template<class U>
  operator U() const
  {
    return (U)n;
  }
};

struct derived_from : base_of
{
};

int main()
{
  held five(5);
  int raised = five;
  derived_from below;
  long inherited = below;
  return raised == 6 && inherited == 4 ? 0 : 1;
}
