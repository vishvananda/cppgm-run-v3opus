// VALIDATION: compile-pass
// 14.7.1p1: the implicit instantiation of a class template specialization
// causes the implicit instantiation of the declarations, and not of the
// definitions, of its member classes.  So `outer<int>` declares `holder` and
// `holder` declares `unused`, and neither body is read until something requires
// that class to be complete - which the object below does for `holder` and
// nothing does for `unused`, whose `typename T::missing` no argument list of
// `outer` could ever settle.

template<class T>
struct outer
{
  struct holder
  {
    struct unused
    {
      enum { width = sizeof(typename T::missing) };
    };

    struct measured
    {
      T value;
    };

    explicit holder(T v)
      : held(v)
    {
    }

    T held;
  };
};

typedef outer<int>::holder holder;

int main()
{
  holder one(7);
  holder::measured two;
  two.value = one.held - 7;
  return two.value;
}
