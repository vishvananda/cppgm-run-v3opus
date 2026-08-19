// 12.9p8: the base subobject is initialized from this constructor's own
// parameters, and each of them reaches the place the base declared it from -
// so an rvalue reference the base wrote takes one at all, which a name of a
// parameter is not.

struct counted
{
  int v;

  explicit counted(int x)
    : v(x)
  {
  }
};

struct taken
{
  int value;

  explicit taken(counted&& c)
    : value(c.v)
  {
  }
};

struct held
{
  int value;

  template <class U>
  explicit held(U&& u)
    : value(static_cast<U&&>(u).v)
  {
  }
};

struct from_taken : taken
{
  using taken::taken;
};

struct from_held : held
{
  using held::held;
};

int main()
{
  counted one(1);
  from_taken a(static_cast<counted&&>(one));
  counted two(2);
  from_held b(static_cast<counted&&>(two));
  return a.value == 1 && b.value == 2 ? 0 : 1;
}
