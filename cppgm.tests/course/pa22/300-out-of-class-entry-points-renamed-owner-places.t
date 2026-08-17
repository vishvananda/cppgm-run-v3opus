// N3485 focus: 14.5.1.3p1 [temp.mem.func] with 14.1p2 [temp.param] - a member
// of a class template defined outside its class stands under a head that may
// spell the class's places with names of its own, and 14.7.1p1 reads the body
// long after the reading that declared it.  So the region binding those names
// has to stand again where the body is read, at every kind of member such a
// definition can be written for: 12.1's constructor, 12.4's destructor,
// 12.3.2p1's conversion function, an operator, a static data member, and a
// member of a class nested inside the template - none of which the class's own
// region binds a single one of those names for.  A class nested two deep is
// left out: `pa22/cppgm++-ref` accepts it and emits only a `declare function`
// its own LowIR never defines, which `lowir2cy86` refuses to link.

int sink;

template<class T>
struct outer
{
  struct inner
  {
    int v;
    inner();
    int width();
    static int held;
  };

  int v;
  outer();
  ~outer();
  operator int();
  int plus(int);
};

template<class A>
outer<A>::inner::inner()
  : v((int)sizeof(A))
{
}

template<class B>
int outer<B>::inner::width()
{
  return v + (int)sizeof(B);
}

template<class C>
int outer<C>::inner::held = (int)sizeof(C) * 10;

template<class E>
outer<E>::outer()
  : v((int)sizeof(E))
{
}

template<class F>
outer<F>::~outer()
{
  sink += (int)sizeof(F);
}

template<class G>
outer<G>::operator int()
{
  return (int)sizeof(G) * 1000;
}

template<class H>
int outer<H>::plus(int k)
{
  return k + (int)sizeof(H);
}

struct seven
{
  char pad[7];
};

int main()
{
  outer<char>::inner one;
  if (one.v != 1) {
    return 1;
  }
  if (one.width() != 2) {
    return 2;
  }
  if (outer<char>::inner::held != 10) {
    return 3;
  }
  outer<seven>::inner other;
  if (other.width() != 14) {
    return 4;
  }
  if (outer<seven>::inner::held != 70) {
    return 5;
  }
  {
    outer<char> held;
    if (held.v != 1) {
      return 6;
    }
    int made = held;
    if (made != 1000) {
      return 7;
    }
    if (held.plus(2) != 3) {
      return 8;
    }
  }
  if (sink != 1) {
    return 9;
  }
  return 0;
}
