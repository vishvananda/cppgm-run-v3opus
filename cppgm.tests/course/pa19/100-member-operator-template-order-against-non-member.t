// N3485 focus: 14.5.6.2p2 [temp.func.order] and 13.5p6 [over.oper] where only
// one of two function templates is a non-static member, that one is considered
// to have a first parameter of "reference to cv A" - which is the place the
// non-member wrote its own first operand in, so 13.3.1.2's candidate set orders
// a member operator template against the non-member beside it as it would order
// two non-members.

struct wrapped
{
  int slot;

  template<class T>
  int operator+(T step) const
  {
    return slot + static_cast<int>(step);
  }
};

template<class T>
int operator+(const wrapped &held, T *step)
{
  return held.slot + static_cast<int>(*step);
}

int main()
{
  wrapped one;
  one.slot = 20;
  int two = 2;
  return (one + 2) + (one + &two) - 44;
}
