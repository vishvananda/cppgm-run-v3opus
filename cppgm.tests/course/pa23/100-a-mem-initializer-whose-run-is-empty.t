// 12.6.2p7 with 14.5.3p4: a mem-initializer written `v(pattern...)` over a run
// of no elements is the `()` 8.5p16 value-initializes for, and not a call of
// the member's default constructor over storage nothing zeroed.
struct plain { int held; };
struct parenthesized
{
  plain value;
  template<class... A> parenthesized(A&&... a) : value(static_cast<A&&>(a)...) {}
};
struct braced
{
  plain value;
  template<class... A> braced(A&&... a) : value{static_cast<A&&>(a)...} {}
};
struct held_base { plain value; held_base() : value() {} };
struct of_a_base : held_base
{
  template<class... A> of_a_base(A&&... a) : held_base(static_cast<A&&>(a)...) {}
};
int main()
{
  parenthesized first;
  braced second;
  of_a_base third;
  return first.value.held + second.value.held + third.value.held;
}
