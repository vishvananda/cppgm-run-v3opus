// VALIDATION: compile-fail
// N3485 focus: 12.3.1 [class.conv.ctor], 13.3.1.4 [over.match.copy],
// 14.5.2 [temp.mem]

struct only_direct
{
  int n;

  template<class U>
  explicit only_direct(U start)
    : n((int)start)
  {
  }
};

int main()
{
  only_direct copied = 3;
  return copied.n;
}
