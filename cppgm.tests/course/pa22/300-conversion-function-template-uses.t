// VALIDATION: compile-pass
// N3485 focus: 12.3.2 [class.conv.fct], 13.3.1.5 [over.match.conv],
// 14.5.2 [temp.mem], 14.8.2.3 [temp.deduct.conv]

struct counted
{
  int n;

  counted(int start)
    : n(start)
  {
  }

  template<class U>
  operator U() const
  {
    return (U)n;
  }
};

struct pointed
{
  int n;

  pointed()
    : n(0)
  {
  }

  template<class U>
  operator U*() const
  {
    return 0;
  }
};

int widened(long v)
{
  return (int)v + 1;
}

int main()
{
  counted seven(7);
  int as_int = seven;
  long as_long = seven;
  char as_char = seven;
  pointed none;
  int* nothing = none;
  return as_int == 7 && as_long == 7 && as_char == 7 &&
         nothing == 0 && widened(seven) == 8 ? 0 : 1;
}
