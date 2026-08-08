// 9.8p1 and the ABI's <unnamed-type-name>: a class a function body declares
// under no name at all is named after that function and its place among the
// types that function left unnamed - `Ut_`, `Ut0_`, ... - because a spelling
// the translation counts for itself is one another unit gives to a class of its
// own.  The shapes below are the ones that numbering has to tell apart: two
// unnamed classes in one function, one in another function, one a member
// function declares, and one a nested block declares after the function's body
// already declared another - the number is the function's and not the block's.
struct base
{
  virtual int value() const { return 0; }
  virtual ~base() {}
};

int two_in_one()
{
  struct : base { int value() const { return 1; } } first;
  struct : base { int value() const { return 2; } } second;
  base & one = first;
  base & other = second;
  return one.value() + other.value();
}

int one_more()
{
  struct : base { int value() const { return 4; } } only;
  base & polymorphic = only;
  return polymorphic.value();
}

struct holder
{
  int member() const
  {
    struct : base { int value() const { return 8; } } object;
    base & polymorphic = object;
    return polymorphic.value();
  }
};

int across_a_block()
{
  struct : base { int value() const { return 16; } } outer;
  base & first = outer;
  int total = first.value();
  {
    struct : base { int value() const { return 32; } } inner;
    base & second = inner;
    total += second.value();
  }
  return total;
}

int main()
{
  return two_in_one() == 3 && one_more() == 4 && holder().member() == 8 &&
         across_a_block() == 48
    ? 0
    : 1;
}
