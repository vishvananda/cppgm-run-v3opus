// N3485 focus: 14.7.2 [temp.explicit] the declaration an explicit instantiation
// stands on may name a member of a class template specialization, which is no
// template of its own: the class its prefix names is what made it.

template<class T>
struct tester
{
  static int test();
};

template<class T>
int tester<T>::test()
{
  return 7;
}

template int tester<int>::test();

int main()
{
  return 0;
}
