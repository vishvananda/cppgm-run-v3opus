// VALIDATION: compile-pass
// N3485 focus: 14.6.4.1 [temp.point], 14.7.1 [temp.inst], 14.5.6.1 [temp.over.link]
//
// 14.5.6.1p5 makes the definition below a definition of the template declared
// at the top, so the specializations the middle of this unit asked for are
// specializations of a template that has a definition by the end of it.
// 14.6.4.1p1 gives each of them a point of instantiation there, so the body is
// the one the definition wrote however far above it the name that asked stands
// - a call, a target type that chose among the declarations, and a member of a
// class template named the same way.

template<class T>
int twice(T);

template<class T>
int twice(T *);

template<class T>
struct holder
{
  T v;

  int doubled();
};

// Each of these names a specialization of a template that is still only
// declared here.
int by_call()
{
  return twice(1);
}

int by_target()
{
  int (*p)(int *) = twice;

  int n = 4;

  return p(&n);
}

int by_member()
{
  holder<int> h;

  h.v = 8;

  return h.doubled();
}

template<class T>
int twice(T t)
{
  return t + t;
}

template<class T>
int twice(T *t)
{
  return *t + *t;
}

template<class T>
int holder<T>::doubled()
{
  return v + v;
}

int main()
{
  return by_call() + by_target() + by_member();
}
