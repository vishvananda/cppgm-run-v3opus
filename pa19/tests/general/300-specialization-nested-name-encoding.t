// VALIDATION: compile-pass
// N3485 focus: 14.2 [temp.names], 9.1 [class.name], 14.5.1.3 [temp.mem.func]
//
// Every name here is one the object file has to write through a
// specialization: a class the specialization declares, a class defined
// outside the one that declared it, a template-argument-list that spells a
// qualified name, and a static data member reached through both.

namespace n {

struct a
{
  int k;
};

template<class T>
struct use
{
  int size()
  {
    return sizeof(T);
  }
};

}

template<class T>
struct outer
{
  struct inner;

  struct tag
  {
    int n;
  };

  static tag value;

  int call(tag t)
  {
    return t.n;
  }
};

template<class T>
struct outer<T>::inner
{
  typedef int reference;
  reference deref();
};

template<class T>
int outer<T>::inner::deref()
{
  return 7;
}

template<class T>
typename outer<T>::tag outer<T>::value = { 0 };

namespace api {

template<class T>
struct text
{
  int v;

  text() : v(1)
  {
  }
};

typedef text<char> string;

enum kind
{
  ok
};

template<class A, class B>
struct pair
{
  A first;
  B second;

  pair(A a, B b) : first(a), second(b)
  {
  }
};

}

int main()
{
  outer<int>::tag t;
  t.n = 2;
  outer<int>::value = t;
  outer<int> o;
  outer<int>::inner q;
  n::use<n::a> u;
  api::pair<const api::string, api::kind> p(api::string(), api::ok);
  return o.call(t) - 2 + q.deref() - 7 + u.size() - 4 + p.first.v - 1 +
      outer<int>::value.n - 2;
}
