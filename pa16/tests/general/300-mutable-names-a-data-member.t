// VALIDATION: compile-fail
struct holder
{
  mutable int reached() { return 1; }
};

int main()
{
  holder source;
  return source.reached() - 1;
}
