// N3485 focus: 14.5.3 [temp.variadic] p4 and 5.3.3 [expr.sizeof] p5 - one
// reading of a pattern binds each pack it names to one element of the run, and
// the pattern may still name that pack *as a pack*: a nested expansion is
// written over the whole run, and `sizeof...` yields how long the run is rather
// than anything about the element standing in it.  So the element the reading
// binds stands for the pack only where the pack is named as one argument.
int add(int a, int b)
{
  return a + b;
}

template<class... Ts>
int width(Ts... args)
{
  return sizeof...(args);
}

template<class... Ts>
int nested(Ts... args)
{
  return width(width(args...) + args...);
}

template<class... Ts>
int held(Ts... args)
{
  return add(width(args...), sizeof...(args));
}

int main()
{
  if (nested(1, 2) != 2) { return 1; }
  if (nested(1, 2, 3) != 3) { return 2; }
  if (held(1, 2, 3, 4) != 8) { return 3; }
  if (nested() != 0) { return 4; }
  return 0;
}
