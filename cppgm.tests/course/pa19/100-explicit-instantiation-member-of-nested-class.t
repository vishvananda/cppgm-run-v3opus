// N3485 focus: 14.7.2 [temp.explicit] p1 - what makes the declaration name a
// specialization is that an instantiation declared it, and a class the pattern
// nests inside its body is made by that same instantiation, so a member of the
// nested class is as much the specialization's as one written beside it.

template<class T>
struct tester
{
  struct probe
  {
    int test() { return 7; }
  };
};

template int tester<int>::probe::test();

int main()
{
  return 0;
}
