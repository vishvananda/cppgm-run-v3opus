// VALIDATION: compile-pass
// N3485 focus: 12.8 [class.copy], 9 [class], 5.2.2 [expr.call]
//
// 12.8p15's copy of a class with no non-static data member and no base
// subobject carries no byte, and 9p6 gives such an object a size only so that
// two of them stand apart.  Where 12.8p31 elides a prvalue of that class into
// an object standing in storage of its own, what is left for the
// initialization to be is that copy - so nothing is written for it and the
// value it would have copied is never read.  The one initializer that is not
// read that way is the one 5.2.2p1 wrote as a *call*: the destination is then
// 6.6.3p2's returned object and the bytes the ABI hands back are a store into
// it however little they say.  13.3.1.2p2's rewrite of an operator into a call
// spells the same call and is not one, which is what this pins at both scopes
// and at the four operator forms.

struct empty {};

empty operator+(int, const empty &) { return empty(); }
empty operator-(const empty &) { return empty(); }

struct holder {
  empty operator[](int) const { return empty(); }
  empty operator()(int) const { return empty(); }
  empty made() const { return empty(); }
};

empty namespace_source;
empty namespace_binary = 1 + namespace_source;
empty namespace_call = operator+(1, namespace_source);

int main() {
  empty source;
  holder held;

  empty binary = 1 + source;
  empty unary = -source;
  empty subscript = held[0];
  empty parenthesized = (held.made());

  empty named_operator = operator+(1, source);
  empty member_call = held.made();
  empty call_operator = held(0);

  (void)binary;
  (void)unary;
  (void)subscript;
  (void)parenthesized;
  (void)named_operator;
  (void)member_call;
  (void)call_operator;
  (void)namespace_binary;
  (void)namespace_call;
  return 0;
}
