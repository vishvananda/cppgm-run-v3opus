// 14.2 and 14.6.1p1: a template-name says which class only once a
// template-argument-list is written after it, so a plain one is a
// type-specifier exactly where the injected-class-name stands.  Written at a
// statement where no class of that name encloses it, `close_impl(which);` is
// 6.8p1's expression-statement and the member 3.4.1 finds is what it calls.
struct mode {
};

template<class T>
struct close_impl {
};

struct linked_streambuf {
  int value;

  linked_streambuf() : value(0) {}

  void close(int which)
  {
    value = 1;
    close_impl(which);
  }

  void close_impl(int given)
  {
    value = given;
  }
};

int main()
{
  linked_streambuf held;
  held.close(7);
  return held.value == 7 ? 0 : 1;
}
