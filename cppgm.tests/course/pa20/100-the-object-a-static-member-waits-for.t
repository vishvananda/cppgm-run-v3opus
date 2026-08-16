// VALIDATION: compile-pass
// 3.2p3 with 14.7.1p1: the definition written outside a class template lays out
// storage where the program reaches it, and 9.2p1's non-static data member is
// no reach of its own - it is one subobject of every object of its class, which
// is where the walk that asks reaches it.  So a class that only *declares* a
// member of a specialization lays out nothing, and an object of that class lays
// out every static data member the tree of it holds - including one whose own
// definition had not been read where the class was completed.

template<class T>
struct counter
{
  static int held;
};

template<class T>
int counter<T>::held = 7;

struct declares_one
{
  counter<char> member;
};

// No object of `declares_one` is laid out and nothing names the member, so this
// unit lays out no storage for `counter<char>::held`.

template<class T>
struct later
{
  static int kept;
};

struct holds_one
{
  later<int> member;
};

struct holds_it_below : holds_one
{
  counter<short> member;
};

// The definitions are written after the classes that declare a member of the
// specialization, and the object below is what reaches both of them.
template<class T>
int later<T>::kept = 5;

holds_it_below reached_object;

int main()
{
  return 0;
}
