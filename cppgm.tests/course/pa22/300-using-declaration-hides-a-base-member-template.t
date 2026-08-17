// N3485 focus: 7.3.3 [namespace.udecl] p14 with 14.5.6.1 [temp.over.link] p5 -
// a member function template a using-declaration brought in from a base is
// hidden by one this class declared "with the same name, parameter-type-list,
// cv-qualification and ref-qualifier", rather than conflicting with it.
//
// The two parameter-type-lists are written in places two different heads
// declared, so `const K&` in the base and `const K&` in the derived class are
// two types and nothing about the types alone says the lists are one.  p5's
// equivalence of the heads is what says it: each head's places stand for their
// positions, and the lists are compared over those stand-ins.
//
// The program may write the using-declaration above or below the declaration
// that hides it, and 9.2p2's complete class is what both orders are settled at.
// A head that declares a different number of places is no such declaration, so
// what the base declared is still reached through it.
struct Key {};

struct base
{
  template<class K> int pick(const K&) const { return 1; }
  template<class K> int also(const K&) const { return 1; }
  template<class K> int kept(const K&) const { return 1; }
};

struct derived_below : base
{
  template<class K> int pick(const K&) const { return 2; }
  using base::pick;
};

struct derived_above : base
{
  using base::also;
  template<class K> int also(const K&) const { return 2; }
};

struct derived_kept : base
{
  using base::kept;
  template<class K, class L> int kept(const K&) const { return 2; }
};

int main()
{
  Key k;
  const derived_below below;
  const derived_above above;
  const derived_kept kept;

  // The derived class's own declaration is what the call runs, whichever order
  // the class body wrote the two in.
  if (below.pick(k) != 2 || above.also(k) != 2)
  {
    return 1;
  }

  // The heads declare one place and two, so neither declaration hides the
  // other and both are reached.
  return (kept.kept(k) == 1 && kept.kept<Key, int>(k) == 2) ? 0 : 1;
}
