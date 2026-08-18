// VALIDATION: compile-pass
// 14.1p3: a non-type template-parameter is a parameter-declaration, whose
// declarator may be abstract - so `template<M *>` declares a place of pointer
// type that no name reaches, and 14.1p9's default may still fill it.

struct marker;

marker *held = 0;

template<class T = int, marker * = (marker *)0>
struct holder
{
  typedef T type;
};

template<int *>
struct ignored
{
  static int read() { return 11; }
};

int counter = 0;

int main()
{
  holder<>::type value = ignored<&counter>::read();
  return value == 11 && held == 0 ? 0 : 1;
}
