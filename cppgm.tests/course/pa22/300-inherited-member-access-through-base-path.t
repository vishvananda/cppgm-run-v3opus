// VALIDATION: compile-pass
// N3485 focus: 11.2 [class.access.base], 11.2p4
// A member the naming class did not declare is reached through a base of it,
// and each base-specifier on that path grants what its own access-specifier
// says: a member of the derived class, a friend of it, and a class derived
// from one that named the base protected all reach it.

struct base
{
  typedef int type;
  int held;
};

struct owner : private base
{
  owner() { held = 1; }
  type from_member() const { return held; }
  friend int through_friend(const owner&);
};

int through_friend(const owner& o)
{
  owner::type v = o.held;
  return v;
}

struct guarded : protected base
{
  guarded() { held = 2; }
};

struct lower : guarded
{
  type from_derived() const { return held; }
};

struct opened : private base
{
public:
  using base::type;
  opened() { held = 4; }
};

typedef opened::type published;

int main()
{
  owner one;
  lower two;
  published three = 4;
  return one.from_member() + through_friend(one) + two.from_derived() +
         three - 8;
}
