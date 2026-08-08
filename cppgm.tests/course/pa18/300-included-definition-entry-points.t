// 2.2p1 and the ABI: which of a constructor's entry points the object file
// owes depends on where its definition was written.  An inline one read from an
// included file is one every unit including that file also holds, so only the
// entries this unit's code named are owed; one written in this unit's own
// source, or one written outside its class without `inline`, is this unit's
// contribution and owes both.
#include "300-included-definition-entry-points.h"

// This unit's own source writes it, so a complete object of it may be created
// wherever the definition is seen even though only a base subobject runs it
// here.
struct own_base
{
  own_base() {}
  virtual int mine() const { return 4; }
};

struct own_derived : own_base
{
  own_derived() : held(40) {}
  virtual int mine() const { return held; }
  int held;
};

written_base::written_base() {}

shared_derived::shared_derived() : held(10) {}

int shared_derived::value() const
{
  return held;
}

written_derived::written_derived() : held(20) {}

int written_derived::here() const
{
  return held;
}

int main()
{
  shared_derived first;
  written_derived second;
  own_derived third;
  shared_base & one = first;
  written_base & two = second;
  own_base & three = third;
  return one.value() == 10 && two.here() == 20 && three.mine() == 40 ? 0 : 1;
}
