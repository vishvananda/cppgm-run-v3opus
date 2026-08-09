// N3485 focus: 5.1.1 [expr.prim.general] `this` stands over the declarator of a
// non-static member function alone, so the trailing-return-type of a static one
// has no object to name however the class around it is written.

struct holder
{
  int slot;
  static auto reach() -> decltype(this->slot);
};

int main()
{
  return 0;
}
