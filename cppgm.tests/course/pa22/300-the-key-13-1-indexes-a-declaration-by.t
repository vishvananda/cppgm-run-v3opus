// N3485 focus: 13.1 [basic.start.main]/[over.load] p1's index of the declarations
// a region holds of one name, read with 14.5.6.1 [temp.over.link] p5 - and
// 3.4.2 [basic.lookup.argdep] p3's set at a callee the program wrote as a
// template-id.
//
// Two declarations written under heads of their own wrote their
// parameter-type-lists in places those heads declared, so the list alone says
// neither that they are one declaration nor that they are two: `template<int N>
// int e(int)` and `template<class T> int e(int)` wrote one list and declare two
// templates.  p5's canonical form is what 13.1's index has to be keyed by, at a
// namespace, in a class, and over a class's constructors alike.
//
// 14.1p4 leaves what an argument *is* a fact of the declaration it is bound to,
// so the written argument list is read once per declaration and a spelling that
// fits none of one declaration's places ends that candidate rather than the
// naming.  3.4.2p3's set is the ordinary lookup's declarations and the
// argument-dependent search's together, and the list is read against the second
// lot the same way.
struct Key {};

namespace space
{
  struct tag {};
  template<class T> int reach(T) { return 3; }
}

typedef space::tag tag;

template<class T> int reach(T, int) { return 4; }

template<class T> int e(int x) { return x + 2; }
template<int N> int e(int x) { return x + 1; }

struct holder
{
  template<int N> int m(int x) { return x + 1; }
  template<class T> int m(int x) { return x + 2; }
};

struct built
{
  int k;
  template<int N> built(int v) { k = v + 1; }
  template<class T> built(int v) { k = v + 2; }
  built() { k = 0; }
};

int main()
{
  // The two heads declare two templates, and the written list picks one of
  // them at each of the three tiers a declaration is indexed at.
  if (e<3>(0) != 1 || e<Key>(0) != 2)
  {
    return 1;
  }
  holder h;
  if (h.m<3>(0) != 1 || h.m<Key>(0) != 2)
  {
    return 1;
  }
  built b;
  if (b.k != 0)
  {
    return 1;
  }
  // 3.4.2p2 reaches the declaration the argument's own namespace makes, which
  // is the only one this list and this argument fit.
  tag t;
  return reach<tag>(t) == 3 ? 0 : 1;
}
