// N3485 focus: 12.6.2p8 [class.base.init] an anonymous union member and the
// variant members in it take no initialization from a constructor that
// designates none, however much a member's own class would ask for.
struct Value
{
  int x;

  Value()
      : x(7)
  {
  }
};

template<class T>
struct Node
{
  union
  {
    T value;
    long other;
  };

  Node()
  {
  }
};

struct Plain
{
  union
  {
    Value value;
  };

  int count;

  Plain()
      : count(3)
  {
  }
};

int main()
{
  Node<Value> node;
  Plain plain;
  (void)node;
  return plain.count - 3;
}
