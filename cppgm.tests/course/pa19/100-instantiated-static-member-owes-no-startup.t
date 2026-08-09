// N3485 focus: 14.7.1 [temp.inst] and 3.6.2 [basic.start.init] - the
// definition of a static data member an instantiation made is the template's
// rather than this unit's, and the initialization it carries is one the use
// that names the member asks for.  So a unit whose one namespace-scope object
// is such a member of trivial type owes the program no startup body at all,
// while a definition the program itself wrote owes one however little it does.

struct tag
{
  int marker;
};

template<class T>
struct held
{
  static T value;
};

template<class T>
T held<T>::value;

tag * reach()
{
  return &held<tag>::value;
}

int main()
{
  return reach()->marker;
}
