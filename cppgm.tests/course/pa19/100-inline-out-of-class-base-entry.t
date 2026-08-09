// 12.1 and the ABI: a constructor stands under an entry point for a complete
// object and one for a base class subobject.  9.3p2 says which of them the
// object file owes: a definition the program wrote outside its class is this
// unit's contribution whichever entry a call here happened to name, so both
// stand even where only a base subobject ran one.
struct Base {
  int value;

  Base(int v);
};

inline Base::Base(int v) : value(v) {}

struct Derived : Base {
  Derived() : Base(4) {}
};

int main()
{
  Derived d;
  return d.value - 4;
}
