// 12.9p1: a using-declaration that names a base's constructors names its
// constructor templates too, and what it declares here is a template - the same
// places over the same parameter list, deduced at this class and built from the
// base's own declaration over the very same argument list.

template <class T>
struct holder
{
  int value;

  template <class U>
  explicit holder(U u)
    : value(int(u) + int(sizeof(T)))
  {
  }
};

template <class T>
struct kept : holder<T>
{
  using holder<T>::holder;
};

struct four
{
  operator int() const { return 4; }
};

int main()
{
  kept<int> a(3);
  kept<char> b((four()));
  return a.value == 7 && b.value == 5 ? 0 : 1;
}
