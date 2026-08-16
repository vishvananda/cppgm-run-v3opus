// VALIDATION: compile-pass
// 3.4.1p8: a name used in the definition of a static data member outside its
// class, after the member's declarator-id, is looked up in the class - so the
// initializer reaches what the class declares under the unqualified spelling,
// exactly as the rest of the declarator already does.

struct plain
{
  typedef int value_type;
  enum { tag = 3 };
  static const long size;
  static const long tagged;
  static const long from_member;
  static const int base = 2;
};

const long plain::size = sizeof(value_type *);
const long plain::tagged = tag;
const long plain::from_member = base + 1;

// A name of the enclosing region is still found where the class declares none
// of its own, and the class's own shadows it where it declares both.
typedef char element_type;
const long around = 5;

struct shadowing
{
  typedef int element_type;
  static const long inner;
  static const long outer;
  static const long enclosing;
};

const long shadowing::inner = sizeof(element_type);
const long shadowing::outer = sizeof(::element_type);
const long shadowing::enclosing = around;

int main()
{
  return (int)(plain::size - 8) + (int)(plain::tagged - 3) +
         (int)(plain::from_member - 3) + (int)(shadowing::inner - 4) +
         (int)(shadowing::outer - 1) + (int)(shadowing::enclosing - 5);
}
