// VALIDATION: compile-pass
struct holder
{
  enum step { first = 1 };

  static int total;
  int held;
};

int holder::total = 5;

int main()
{
  holder source;
  source.held = 0;
  int* reached = &decltype(source)::total;
  *reached = *reached + decltype(source)::first;
  return decltype(source)::total == 6 ? 0 : 1;
}
