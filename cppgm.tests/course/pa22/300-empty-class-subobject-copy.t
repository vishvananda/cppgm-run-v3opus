// VALIDATION: compile-pass
// N3485 focus: 12.8 [class.copy], 8.5.1 [dcl.init.aggr], 12.6.2 [class.base.init]
//
// The same copy 12.8p15 defines, asked of the subobjects an initialization
// reaches rather than of an object a declaration named.  A member an aggregate
// clause reached, a member a mem-initializer named and 12.6.2p5's base
// subobject each take that copy, and for a class with no non-static data
// member and no base subobject of its own it carries no byte - so nothing is
// written for it and the object it would have read is never made.  The
// subobject is still named: 8.5.1p1, 12.6.2p2 and 10.2 each reach it before
// the copy.  8.5.1p1's element of an *array* is not one of them - what fills an
// element is the value the ABI handed back going into storage the array holds -
// and neither is a member whose class holds a byte, so both are written here
// beside the ones that are not.

struct empty {};
struct held { int one; };

empty operator+(int, const empty &) { return empty(); }
held operator+(int, const held &) { return held(); }

struct aggregate_member { empty member; };
struct held_member { held member; };

struct named_member {
  empty member;
  explicit named_member(const empty &source) : member(1 + source) {}
};

struct derived : empty {
  explicit derived(const empty &source) : empty(1 + source) {}
};

int main() {
  empty source;
  held held_source;

  aggregate_member clause = {1 + source};
  held_member held_clause = {1 + held_source};
  empty elements[2] = {1 + source, 1 + source};
  named_member initialized(source);
  derived based(source);

  (void)clause;
  (void)held_clause;
  (void)elements;
  (void)initialized;
  (void)based;
  return 0;
}
