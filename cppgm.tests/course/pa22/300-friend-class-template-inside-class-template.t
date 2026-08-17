// VALIDATION: compile-pass
// N3485 focus: 14.5.4 [temp.friend], 14.6 [temp.res]
// 14.6p8 reads a class template's own definition where it stands, and the one
// record that reading makes is 14.5.4p1's: a `friend class W;` under a head
// names a class template of the enclosing namespace, so the declaration the
// program writes for `W` below is a second declaration of that template and
// not a redeclaration of some class this reading invented.

template<class T> class holder
{
  T kept;
public:
  explicit holder(T v)
    : kept(v)
  {
  }
  template<class U> friend class peeker;
};

template<class U> class peeker
{
public:
  static U read(holder<U>& h)
  {
    return h.kept;
  }
};

int main()
{
  holder<int> a(5);
  holder<char> b('\a');
  return peeker<int>::read(a) == 5 && peeker<char>::read(b) == '\a' ? 0 : 1;
}
