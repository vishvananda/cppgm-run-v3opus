// VALIDATION: compile-pass
// N3485 focus: 14.5.4 [temp.friend], 11.3 [class.friend], 3.4.1 [basic.lookup.unqual]
// 11.3p10: a friend declaration whose declarator-id is qualified names a
// function the region already declared.  3.4.1p8 moves the head such a
// declaration stands under inside the region that name reaches, so which class
// grants the declaration has to be taken before the declarator is read.

namespace store
{

template<class T> class crate;

namespace detail
{
template<class T> int weigh(crate<T> c);
}

template<class T> class crate
{
  int mass;
public:
  explicit crate(int m)
    : mass(m)
  {
  }
  template<class U> friend int detail::weigh(crate<U>);
};

namespace detail
{
template<class U> int weigh(crate<U> c)
{
  return c.mass;
}
}

}

int main()
{
  return store::detail::weigh(store::crate<int>(7)) == 7 ? 0 : 1;
}
