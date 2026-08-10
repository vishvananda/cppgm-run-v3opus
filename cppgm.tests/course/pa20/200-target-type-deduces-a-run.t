// N3485 focus: 14.8.2.2 [temp.deduct.funcaddr] p1 with 14.5.3 [temp.variadic]
// p4 - the target type a function template's address is deduced from is one A
// written over a whole function type, so the parameter list inside it is
// matched the way a template-argument-list is: a trailing `Ts...` is one entry
// standing for every parameter the entries before it did not take.  So
// `int (*)(int, char)` deduces the run <int, char> and `int (*)()` deduces a
// run of none, rather than the two lists being refused for their lengths.
template<class... Ts> int count(Ts... args) { return (int)sizeof...(args); }

template<class T, class... Ts> int lead(T first, Ts... rest)
{
  return first + (int)sizeof...(rest);
}

int main()
{
  int (*two)(int, char) = count;
  if (two(1, 'a') != 2) { return 1; }
  int (*none)() = count;
  if (none() != 0) { return 2; }
  int (*three)(int, char, long) = lead;
  if (three(4, 'a', 5L) != 6) { return 3; }
  return 0;
}
