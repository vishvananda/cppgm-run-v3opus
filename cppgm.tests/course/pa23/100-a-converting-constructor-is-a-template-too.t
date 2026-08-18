// VALIDATION: compile-pass
// 13.3.1.4p1 with 14.8.3p1: the candidate set a copy-initialization of class
// type is chosen from holds the converting constructor *templates* too, each
// through the specialization the source argument deduces for it.  12.3.1p2's
// `explicit` keeps one out of that set and leaves it reachable by a
// direct-initialization, and 13.3.3p1's last tie-break is what makes a
// constructor the class declared outright beat a specialization that converts
// the argument exactly as well.

template<class T>
struct boxed
{
  long held;

  boxed(long value)
    : held(value)
  {
  }

  boxed(const boxed<int> & other)
    : held(other.held + 100)
  {
  }

  template<class U>
  boxed(const boxed<U> & other)
    : held(other.held)
  {
  }
};

template<class T>
struct guarded
{
  long held;

  guarded(long value)
    : held(value)
  {
  }

  template<class U>
  explicit guarded(const guarded<U> & other)
    : held(other.held)
  {
  }
};

long from_boxed(const boxed<long> & value)
{
  return value.held;
}

long from_guarded(const guarded<long> & value)
{
  return value.held;
}

int main()
{
  // 13.3.3p1: `boxed(const boxed<int> &)` and the specialization of the
  // template for `U = int` convert the argument equally well, and the one the
  // class declared outright is the one chosen.
  const long declared = from_boxed(boxed<int>(8));
  // The template is the only way in for every other source specialization.
  const long deduced = from_boxed(boxed<char>(9));
  // 12.3.1p2: an `explicit` constructor template is no converting constructor,
  // and a direct-initialization still reaches it.
  const long direct = from_guarded(guarded<long>(guarded<int>(7)));
  return declared == 108 && deduced == 9 && direct == 7 ? 0 : 1;
}
