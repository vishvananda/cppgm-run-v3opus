// VALIDATION: compile-pass
// 14.7.1p1 and 8.5.1p2: the aggregate whose array member the clauses reach is
// a specialization, so the bound its member was declared with is the argument
// the list settled - and the constructor the class is given from its members is
// declared once for that specialization, whatever names it.  Two
// specializations of the one template are two classes and two such
// constructors.

template<class T, int N>
struct holder
{
  T elements[N];
};

struct item
{
  item(int) {}
};

void take_scalars(holder<int, 3>);
void take_items(holder<item, 2>);

int main()
{
  take_scalars(holder<int, 3>{4, 5, 6});
  take_items(holder<item, 2>{item(0), item(1)});
  return 0;
}
