// 5.19p2: an address constant expression evaluates to the address of an object
// with *static storage duration*.  12.2p1 gives a prvalue argument bound to a
// reference a temporary to bind to, and that temporary's lifetime ends with the
// evaluation that made it - so a body handing its address back hands back
// storage the program never has, and 7.1.5p9's constant initializer is one this
// declaration does not have.
//
// The refusal belongs to the declaration that keeps the pointer and not to the
// body: `&value` inside the call is a perfectly good address of a perfectly
// good object while that object exists.

constexpr const int *address_of(const int &value)
{
  return &value;
}

// 12.2p1: the argument is a prvalue, so what the place binds to is a temporary.
constexpr const int *escaped = address_of(5);

int main()
{
  return 0;
}
