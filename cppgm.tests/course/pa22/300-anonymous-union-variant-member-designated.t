// N3485 focus: 12.6.2p2 [class.base.init] a mem-initializer-id may designate a
// variant member of an anonymous union, and what it initializes is the member
// of the object 9.5p1 declared - so the initialization names that object and
// then the member standing in it.
struct Value
{
  int x;

  Value(int v)
      : x(v)
  {
  }
};

struct Base
{
  int lead;

  union
  {
    int early;
    long wide;
  };

  Base()
      : lead(1), early(2)
  {
  }
};

template<class T>
struct Holder
{
  struct
  {
    int head;

    union
    {
      T value;
      long other;
    };
  };

  union
  {
    int cells[3];
    double spread;
  };

  Holder()
      : head(3), value(4), cells{5, 6, 7}
  {
  }
};

struct Derived : Base
{
  union
  {
    Value held;
    char narrow;
  };

  Derived()
      : held(8)
  {
  }

  int reached() const
  {
    return lead + early + held.x;
  }
};

int main()
{
  Derived derived;
  Holder<int> holder;
  return derived.reached() + holder.head + holder.value + holder.cells[0] +
      holder.cells[1] + holder.cells[2] - 36;
}
