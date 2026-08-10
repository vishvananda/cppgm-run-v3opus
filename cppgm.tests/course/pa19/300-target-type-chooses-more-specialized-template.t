// VALIDATION: compile-pass
// N3485 focus: 13.4 [over.over], 14.8.2.2 [temp.deduct.funcptr], 14.5.6.2 [temp.func.order]
//
// A target type chooses one declaration of an overloaded name, and where the
// name is a set of function templates it chooses through the specialization
// each of them deduces.  More than one can deduce a specialization of exactly
// the target type - `pick(T)` and `pick(T *)` both make `int (int *)` - so the
// choice is 14.5.6.2's ordering of the two templates, exactly as it is when a
// call leaves two specializations whose conversions tie.  Every context whose
// target type asks the question asks the same one: an initializer, an
// assignment, a parameter, and a target written over a class template.

template<class T>
struct box
{
  T v;
};

template<class T>
int pick(T)
{
  return 1;
}

template<class T>
int pick(T *)
{
  return 2;
}

template<class T>
int pick(box<T>)
{
  return 3;
}

// 13.4p1: a declaration the program wrote stands ahead of every specialization
// a deduction makes, however many of them have the target's type.
int pick(char *)
{
  return 4;
}

int through_parameter(int (*f)(int *))
{
  int n = 0;

  return f(&n);
}

int main()
{
  int (*by_initializer)(int *) = pick;

  int (*by_assignment)(int *);

  by_assignment = pick;

  int (*over_class)(box<int>) = pick;

  int (*written)(char *) = pick;

  box<int> b;

  b.v = 0;

  int n = 0;

  return by_initializer(&n) + by_assignment(&n) * 10 +
         over_class(b) * 100 + written(0) * 1000 +
         through_parameter(pick) * 10000;
}
