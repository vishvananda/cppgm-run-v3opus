// VALIDATION: compile-pass
// 10p1's `class-or-decltype`: a base-specifier may be written as a
// decltype-specifier, which names the class the type of an expression is.  No
// spelling answers what that type is - which declaration a call in it reaches
// is 13.3's question and not the text's - so the operand is a tree the reading
// comes back to, and a dependent one is a base only the arguments settle.

struct first_base
{
  static const int first = 1;
};

struct second_base
{
  static const int second = 2;
};

first_base make_first();

second_base make_second();

struct plain : decltype(make_first())
{
};

struct qualified : public decltype(make_second())
{
};

class hidden : private decltype(make_first())
{
public:
  static const int reached = first_base::first;
};

struct both : decltype(make_first()), public decltype(make_second())
{
};

template<class T>
T made();

template<class T>
struct chosen : decltype(made<T>())
{
};

struct outer
{
  struct nested : decltype(make_first())
  {
  };
};

int main()
{
  return plain::first + qualified::second + hidden::reached + both::first +
                     both::second + chosen<second_base>::second +
                     outer::nested::first ==
                 10
             ? 0
             : 1;
}
