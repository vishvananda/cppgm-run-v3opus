// VALIDATION: compile-pass
struct counter
{
  mutable int seen;
  int value;

  int look() const
  {
    seen = seen + 1;
    return value;
  }
};

int main()
{
  const counter c = {0, 9};
  int first = c.look();
  int second = c.look();
  return first == 9 && second == 9 && c.seen == 2 ? 0 : 1;
}
