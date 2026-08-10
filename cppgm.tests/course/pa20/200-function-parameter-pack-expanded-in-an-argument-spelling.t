// N3485 focus: 14.5.3 [temp.variadic] p4 and 8.3.5 [dcl.fct] p10 - a pack
// expansion written inside a template-argument-list arrives as a *spelling*,
// and the pack its pattern names may be either kind of settled pack: a run an
// argument list bound at a type place, or the places one expansion of a
// function parameter pack declared.  `sizeof(args)...` names the second, and a
// reading that only knew the first walked the elements of a type that holds
// none.
template<unsigned long... Ns>
struct widths {
  static const int count = sizeof...(Ns);
};

template<int... Ns>
struct sum;

template<>
struct sum<> {
  static const int value = 0;
};

template<class... Ts>
int spelled(Ts... args)
{
  return widths<sizeof(args)...>::count + sizeof...(Ts);
}

template<class... Ts>
int one_place(Ts... args)
{
  return widths<sizeof(args)...>::count;
}

int main()
{
  if (spelled(1) != 2) { return 1; }
  if (spelled(1, 'a') != 4) { return 2; }
  if (spelled() != 0) { return 3; }
  if (one_place('a') != 1) { return 4; }
  if (sum<>::value != 0) { return 5; }
  return 0;
}
