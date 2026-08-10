// VALIDATION: compile-pass
// N3485 focus: 14.5.6.1 [temp.over.link], 14.1 [temp.param], 1.3.11 [defns.signature]
//
// 14.5.6.1p5: two function templates declare the same template when their
// heads declare the same parameters and their types agree once each head's
// parameters stand for the other's.  The types themselves differ, because each
// head declared parameters of its own, so the question is asked of a signature
// each declaration has on its own - its type with each parameter standing for
// the place its head declared it in - and not by reading every declaration of
// the name against every other.
//
// The name here heads a chain of templates that are pairwise distinct except
// where a redeclaration means to name one of them: the definitions are of the
// declarations above them, and nothing here declares a second `join` with the
// same parameter types.

template<class T>
struct box
{
  T v;
};

template<class A>
int join(A);

template<class A>
int join(A *);

template<class A, class B>
int join(A, B);

template<class A>
int join(box<A>);

template<class A>
int join(A, int);

// Each of these defines the declaration written above it, whose head spelled
// its parameters with other names.
template<class X>
int join(X x)
{
  return x + 1;
}

template<class Y>
int join(Y *y)
{
  return *y + 2;
}

template<class P, class Q>
int join(P p, Q q)
{
  return p + q + 4;
}

template<class Z>
int join(box<Z> b)
{
  return b.v + 8;
}

template<class W>
int join(W w, int n)
{
  return w + n + 16;
}

int main()
{
  int n = 0;

  box<int> b;

  b.v = 0;

  return join(n) + join(&n) * 3 + join(n, 0L) * 5 + join(b) * 7 +
         join(n, 0) * 11;
}
