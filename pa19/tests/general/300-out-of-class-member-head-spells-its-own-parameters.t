// VALIDATION: compile-pass
// N3485 focus: 14.5.1.3 [temp.mem.func], 14.1 [temp.param]
//
// 14.1p2 leaves each declaration of one template free to spell its parameters
// as it likes, and what two heads share is the *places* the argument list is in
// the order of.  So a member definition written outside its class is read
// against the names its own head wrote, standing for the places its own
// template-id put them in - whatever the class-head called those places, and
// whatever the definition written before it called them.

template<class First, class Second>
struct pair_of
{
  First one;
  Second two;
  void store();
  void load();
};

template<class Second, class First>
void pair_of<Second, First>::store()
{
  Second held = 300;
  First tag = 2;
  one = held;
  two = tag;
}

template<class Alpha, class Beta>
void pair_of<Alpha, Beta>::load()
{
  Alpha held = 7;
  Beta tag = 8;
  one = held;
  two = tag;
}

int main()
{
  pair_of<int, char> made;
  made.store();
  if (made.one != 300 || made.two != 2)
  {
    return 1;
  }
  made.load();
  return made.one == 7 && made.two == 8 ? 0 : 1;
}
