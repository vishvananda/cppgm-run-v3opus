// VALIDATION: compile-fail
// N3485 focus: 11.2 [class.access.base], 11.2p4
// The member is public in the class that declared it and the base-specifier
// that reaches it is private, so no context outside the derived class names it.

struct base
{
  typedef int type;
};

struct derived : private base
{
};

typedef derived::type rejected;

int main()
{
  return 0;
}
