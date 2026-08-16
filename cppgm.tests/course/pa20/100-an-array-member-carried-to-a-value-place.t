// VALIDATION: compile-pass
// 8.5.1p2 and 8.3.5p5: a braced-init-list written where an object of the
// aggregate is one of its own builds it through the constructor the class is
// given from its members - and 8.3.5p5 leaves an array member no by-value
// parameter at all, so that place carries the array itself: the clauses build
// the elements in storage of the caller's and the callee copies them into the
// member.  8.5.1p11's braces around them may be written or left out.

struct carried
{
  int leading;
  int elements[3];
  double trailing;
};

struct braced
{
  int elements[2];
};

void take_carried(carried);
void take_braced(braced);

int main()
{
  take_carried(carried{1, 2, 3, 4, 5.5});
  take_braced(braced{{6, 7}});
  return 0;
}
