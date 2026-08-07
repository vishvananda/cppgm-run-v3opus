// VALIDATION: compile-pass
struct holder
{
  typedef int entry;
  entry held;
};

int main()
{
  holder source;
  source.held = 6;
  decltype(source)::entry copied = source.held;
  return static_cast<decltype(source)::entry>(copied) == 6 ? 0 : 1;
}
