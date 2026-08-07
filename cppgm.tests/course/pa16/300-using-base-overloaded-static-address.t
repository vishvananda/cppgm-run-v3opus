// 13.4 and 7.3.3p1: the target type of an initialization chooses one declaration
// of an overloaded name, and where it chose the one a using-declaration made,
// the address it takes is the base's function - the definition this unit writes
// and the name the object file gives it.
struct address_base {
  static int selected(int given) { return given; }
  static int selected(double given) { return (int)given + 1; }
};

struct address_derived : private address_base {
 public:
  using address_base::selected;
};

int main() {
  int (*chosen)(int) = address_derived::selected;
  return chosen(4) - 4;
}
