// VALIDATION: run-pass
// 7.1.3p2 makes a template-id over an alias template be the type its type-id
// named, and the type-id is read once per argument list.  14.7.1p1's demand is
// still made of the class that type names at every naming of the alias, so a
// member of a class template written through one is a complete class where the
// enclosing specialization is laid out.

template<class... T>
struct payload
{
  int held;
  payload() : held(5) {}
};

template<class W>
using payload_of = payload<W>;

template<class... T>
using payload_pack = payload<T...>;

template<class S>
struct holder
{
  payload_of<int> direct;
  typedef payload_pack<> named;
};

template<class S>
struct derived : payload_of<int>
{
};

int main()
{
  holder<char> one;
  holder<char>::named two;
  derived<char> three;
  return one.direct.held + two.held + three.held == 15 ? 0 : 1;
}
