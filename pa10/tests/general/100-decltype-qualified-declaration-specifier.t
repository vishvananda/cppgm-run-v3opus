// HHC-125
struct holder
{
  typedef int entry;
  entry held;
};

int main()
{
  holder source;
  source.held = 3;
  decltype(source)::entry copied = source.held;
  return copied == 3 ? 0 : 1;
}
