// N3485 focus: 10.3 [class.virtual] p10 - the table of a class the instantiation
// of a class template made names that class's virtual members, and 3.2p3 has no
// expression to point at for the use.  A class nested in the pattern is made by
// the same instantiation, so its own table asks for its own members.

template<class T>
struct holder
{
  struct node
  {
    virtual int tag() { return 41; }
  };

  node part;
};

int main()
{
  holder<int> made;
  return 0;
}
