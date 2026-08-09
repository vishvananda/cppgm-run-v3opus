// N3485 focus: 14.7.2 [temp.explicit] p1 is a demand with no use behind it, so
// the body 14.7.1p1's instantiation put aside for the use that names the member
// is one this declaration has to ask for - a member defined in its class has no
// other declaration in the unit to be reached through.

template<class T>
struct tester
{
  int test() { return 7; }
};

template int tester<int>::test();

int main()
{
  return 0;
}
