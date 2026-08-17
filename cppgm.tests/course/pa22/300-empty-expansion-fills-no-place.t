// N3485 focus: 14.5.3p4 [temp.variadic] with 14.3p1 [temp.arg] - an expansion
// written in a template-argument-list stands for its run and is counted only
// once that run is settled.  A template that declared no pack of its own is
// where the two counts part company: `one<int, As...>` gives `one` one argument
// where `As` is empty and two where it is not, and the reading of the pattern
// that wrote it can answer neither, because there the run stands for itself.
// So the list is left as written until an argument list settles it, and both
// clauses - 14.3p1's too many and its too few - are asked of what it settled
// into.  14.1p4's own question travels with it: what an entry standing at no
// place at all writes is the pack it names, a type or a value.

template<class X>
struct one
{
  X slot;
};

template<class X, class Y, class Z>
struct three
{
  int slot;
};

template<int N>
struct num
{
  static const int n = N;
};

template<class... As>
struct fills
{
  typedef one<int, As...> exact;
};

template<class... As>
struct spans
{
  typedef three<int, As...> exact;
};

template<int... Ns>
struct valued
{
  typedef num<3, Ns...> exact;
};

template<class... As>
struct at_a_value
{
  typedef num<2, As...> exact;
};

template<class... As>
int held()
{
  one<int, As...> made;
  made.slot = 4;
  return made.slot;
}

int main()
{
  fills<>::exact none;
  none.slot = 1;
  if (none.slot != 1) {
    return 1;
  }
  spans<char, long>::exact both;
  both.slot = 2;
  if (both.slot != 2) {
    return 2;
  }
  if (valued<>::exact::n != 3) {
    return 3;
  }
  if (at_a_value<>::exact::n != 2) {
    return 4;
  }
  if (held<>() != 4) {
    return 5;
  }
  return 0;
}
