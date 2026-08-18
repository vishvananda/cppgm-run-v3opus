// N3485 focus: 8.5.1p15 [dcl.init.aggr] an aggregate holding an anonymous union
// is initialized by the first member of that union alone, so the constructor an
// array of such an aggregate is built by carries that member and not the object
// 9.5p1 declared.
struct Inner
{
  int lead;

  union
  {
    int early;
    long wide;
  };
};

struct Outer
{
  struct
  {
    int head;

    union
    {
      int deep;
      double spread;
    };
  };

  union
  {
    int tail;
    char narrow;
  };
};

union Alone
{
  int only;
  long wide;
};

int main()
{
  Inner inner[2] = {{1, 2}, {3, 4}};
  Outer outer[2] = {{5, 6, 7}, {8, 9, 10}};
  Alone alone[2] = {{11}, {12}};
  return inner[0].lead + inner[0].early + inner[1].lead + inner[1].early +
      outer[0].head + outer[0].deep + outer[0].tail + outer[1].head +
      outer[1].deep + outer[1].tail + alone[0].only + alone[1].only - 78;
}
