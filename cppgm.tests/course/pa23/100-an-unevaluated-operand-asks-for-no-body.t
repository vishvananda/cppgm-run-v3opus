// VALIDATION: compile-pass
// 14.7.1p1 with 3.2p2: a function template specialization is instantiated
// where it is named in a context that requires the definition to exist, and
// 5p8's unevaluated operand is not one - `decltype`, `sizeof` and `noexcept`
// say what the call would be worth and run nothing.  So `declval<T>()` written
// in any of the three leaves the body it would have to read alone, which is
// what the idiom is for: no object of `T` is ever made.
//
// 3.2p2 stops at the operand, though.  A class the operand completes is
// instantiated there, and the constant expressions its own members write are
// evaluated - the point of instantiation is a namespace-scope one and not the
// operand that asked for the type.

template<class T>
T declval() noexcept
{
  enum { unread = sizeof(typename T::missing) };
  return declval<T>();
}

template<class T>
struct measured
{
  static const int width = (int)sizeof(T);
};

template<class T>
struct probe
{
  enum { size = (int)sizeof(declval<T>()) };
  enum { named = (int)sizeof(decltype(declval<T>())) };
  enum { quiet = noexcept(declval<T>()) ? 1 : 0 };
  enum { held = measured<T>::width };
};

int main()
{
  return probe<int>::size - probe<int>::named + probe<int>::held -
         measured<int>::width - probe<int>::quiet + 1;
}
