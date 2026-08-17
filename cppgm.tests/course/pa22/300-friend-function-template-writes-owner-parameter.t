// VALIDATION: compile-pass
// N3485 focus: 14.5.4 [temp.friend], 14.7.1 [temp.inst], 3.4.1 [basic.lookup.unqual]
// 11.3p6 declares a friend function template into the namespace around the
// class, and 14.7.1p1 reads its definition again when a call deduces the
// specialization - so what that reading stands in has to be the region the
// definition was *written* in and not the one the declaration was made in.
// Only the written one binds the enclosing class template's own argument,
// which the declarator and the body below both name.

template<class T>
struct box
{
  T v;

  box(T x)
    : v(x)
  {
  }

  template<class U>
  friend T mixed(box<T> b, U u)
  {
    return b.v + (T)u;
  }

  template<class U>
  friend U trailing(U u, box<T> b)
  {
    return u + (U)b.v;
  }
};

int main()
{
  return mixed(box<int>(1), 2) + (int)mixed(box<char>('\1'), 1) +
                 trailing(2, box<int>(1)) ==
             9
           ? 0
           : 1;
}
