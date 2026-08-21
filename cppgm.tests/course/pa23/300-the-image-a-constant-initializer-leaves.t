// VALIDATION: compile-pass
// N3485 focus: 3.6.2 [basic.start.init], 12.1 [class.ctor], 12.6.2 [class.base.init]
//
// 3.6.2p2's second bullet: an object with static storage duration initialized by
// a constructor call is *constant-initialized* where that constructor is a
// constexpr one and every full-expression of the mem-initializers and of
// 12.6.2p8's brace-or-equal-initializers is a constant expression.  Nothing in
// that sentence is about `const`.  5.19p3's is: it says what a *name* of the
// object is worth, and a declaration that wrote no `const` names no constant
// however well the initialization folds - so the two clauses are two facts of
// one fold, and only the first one reaches the image.
//
// 12.1p5 gives an implicitly-defined default constructor the same standing as a
// written `constexpr` one, so `holds_it a;` over a class whose members carry
// brace-or-equal-initializers holds them before the program runs.  The
// declaration is asked once per form 8.5 fills the object through, once per
// place 3.7.1 gives static storage duration, and once per kind of subobject
// 9.2p13 laid out; 3.2p2 is the question left over, and the definition the walk
// went through to work the image out is one this unit then holds.

enum shade
{
  dim = 1,
  bright = 2
};

int anchor = 1;

struct written_out
{
  int held;

  constexpr written_out() : held(2)
  {
  }
};

struct one_of_each
{
  char narrow = 1;
  int plain = 2;
  long long wide = 3;
  double scaled = 0.5;
  bool flag = true;
  shade tone = bright;
  int * points = &anchor;
};

struct beneath
{
  int under = 1;
};

struct above : beneath
{
  written_out inner;
  int over = 2;
};

// 8.5p6, 8.5p16, 8.5.4p3 and 8.5p14: the four ways one declaration fills an
// object of class type, each of them 3.6.2p2's constructor call.
one_of_each by_default;
one_of_each from_a_prvalue = one_of_each();
one_of_each from_a_list{};
one_of_each from_an_equals_list = {};

// 10p1 and 9.2p13: the subobjects a walk down the class reaches.
above through_the_layout;

// 9.4.2p2: the definition of a static data member stands outside its class, and
// 3.7.1p1 gives it the same storage duration a namespace-scope object has.
struct owner
{
  static above member_definition;
};

above owner::member_definition;

// 3.7.1p3: an object a block declared `static` has it too.
int reached_through_a_block()
{
  static one_of_each inside_a_block;
  return inside_a_block.plain;
}

int main()
{
  return by_default.plain == 2 && from_a_prvalue.wide == 3 &&
         from_a_list.tone == bright && from_an_equals_list.narrow == 1 &&
         from_an_equals_list.points == &anchor &&
         through_the_layout.under == 1 && through_the_layout.inner.held == 2 &&
         through_the_layout.over == 2 &&
         owner::member_definition.inner.held == 2 &&
         reached_through_a_block() == 2
    ? 0
    : 1;
}
