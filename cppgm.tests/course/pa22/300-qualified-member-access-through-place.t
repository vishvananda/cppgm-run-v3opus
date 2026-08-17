// VALIDATION: compile-pass
// N3485 focus: 5.2.5 [expr.ref], 3.4.3 [basic.lookup.qual]
// The nested-name-specifier of a qualified-id written after `->` names a class
// through whatever name reached it: 14.1p2's place an argument list bound to a
// class type, and 7.1.3's typedef-name.

struct counted
{
  static int twice(int v) { return v + v; }

  int once(int v) const { return v; }
};

template<class B>
struct through_place : B
{
  typedef B parent;

  int by_place(int v) const { return this->B::twice(v); }
  int by_typedef(int v) const { return this->parent::once(v); }
};

int main()
{
  through_place<counted> one;
  return one.by_place(2) + one.by_typedef(3) - 7;
}
