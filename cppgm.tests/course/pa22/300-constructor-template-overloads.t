// VALIDATION: compile-pass
// N3485 focus: 14.5.2 [temp.mem], 13.3.1.3 [over.match.ctor]
// 13.3.1.3 chooses among the constructors a class declares, and a constructor
// template is a candidate through the specialization the arguments deduce -
// so the chain the class holds carries both kinds and each is told apart by
// the tag its own declaration wrote.

struct tagged
{
  struct wide { };
  struct narrow { };

  int v;

  tagged()
    : v(0)
  {
  }

  template<class U>
  tagged(wide*, U u)
    : v(int(u) + 100)
  {
  }

  template<class U>
  tagged(narrow*, U u)
    : v(int(u) + 200)
  {
  }
};

int main()
{
  tagged a;
  tagged b((tagged::wide*)0, 1);
  tagged c((tagged::narrow*)0, 1);
  return a.v == 0 && b.v == 101 && c.v == 201 ? 0 : 1;
}
