// 7.1.6.2p1 and 14.2: a decltype-specifier written inside a
// template-argument-list reaches the semantic layer as text, and 5.1.1p8's
// id-expression is only one of the expressions it admits - a call through
// 13.5.4's operator, a delete-expression and 5.2.3p1's explicit type
// conversion each say nothing a lookup of their spelling could answer.  So the
// specifier is answered by the tree the parse read for its operand rather than
// by a lookup of the text between its parentheses.

template<class T>
struct box
{
  typedef T type;
  static const bool value = true;
};

struct source
{
  int held;
};

struct factory
{
  source operator()() const
  {
    source made;
    made.held = 3;
    return made;
  }
};

template<class T>
T&& declval();

source* pointer();
int counter();

// 5.3.5p1: a delete-expression has type void, which is a type no lookup of
// `delete declval<int*>()` could have reached.
static_assert(box<decltype(delete declval<int*>())>::value, "");
static_assert(box<decltype(delete[] declval<int*>())>::value, "");
// 5.3.1p1: an indirection through a call is an lvalue and the specifier names
// a reference, so this is a different specialization from the one below it.
static_assert(box<decltype(*pointer())>::value, "");
static_assert(box<decltype(pointer())>::value, "");
static_assert(box<decltype(counter() + 1)>::value, "");

template<class ValueFactory>
int run()
{
  ValueFactory make;
  // 13.5.4: the call is through the object's own operator, which is a
  // declaration only the argument list of `run` names.
  box<decltype(make())>::type made = make();
  // 5.2.3p1: an explicit type conversion written in functional notation.
  box<decltype(source())>::type zero = made;
  return zero.held;
}

int main()
{
  return run<factory>() == 3 ? 0 : 1;
}
