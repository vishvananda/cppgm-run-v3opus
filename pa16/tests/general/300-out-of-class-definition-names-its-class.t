// VALIDATION: compile-pass
struct table
{
  typedef int entry;
  entry held;
  entry take(entry given);
  entry give();
};

table::entry table::take(entry given)
{
  return given + held;
}

auto table::give() -> entry
{
  return held;
}

int main()
{
  table t;
  t.held = 3;
  return t.take(4) == 7 && t.give() == 3 ? 0 : 1;
}
