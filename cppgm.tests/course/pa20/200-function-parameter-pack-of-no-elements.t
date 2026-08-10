// N3485 focus: 14.5.3 [temp.variadic] p4 and 8.3.5 [dcl.fct] p10 - a function
// parameter pack whose run holds no elements declares no place at all, and it
// is still a declaration: `sizeof...` over it is zero and an expansion of it
// comes to no arguments.  8.3.5p10 names the *first* place after the pack, so a
// reading that only declared places left the pack's own name naming nothing as
// soon as the run was empty.
int none()
{
  return 5;
}

int one(int a)
{
  return a;
}

template<class... Ts>
int forwarded(Ts... args)
{
  return sizeof...(args);
}

template<class... Ts>
int calls_none(Ts... args)
{
  return none() + sizeof...(args) + sizeof...(Ts);
}

template<class... Ts>
int calls_one(int first, Ts... args)
{
  return one(first) + forwarded(args...) + sizeof...(args);
}

int main()
{
  if (calls_none() != 5) { return 1; }
  if (calls_none(1, 2) != 9) { return 2; }
  if (calls_one(3) != 3) { return 3; }
  if (calls_one(3, 4, 5) != 7) { return 4; }
  if (forwarded() != 0) { return 5; }
  return 0;
}
