// VALIDATION: compile-pass
// N3485 focus: 14.5.4 [temp.friend], 14.6 [temp.res], 11.3 [class.friend]
// 14.6p8 reads a class template's own definition where it stands and 14.7.1p1
// reads it again for each specialization, and 11.3p6 declares the friend into
// one namespace either way - so the first of those readings declares the
// function template and the instantiation is what defines it.  Counting both
// as definitions would make one instantiation of the class a redefinition of
// the template the class body wrote.

template<class T>
struct box
{
  T v;

  box(T x)
    : v(x)
  {
  }

  template<class U>
  friend U unwrap(box<U> b)
  {
    return b.v;
  }
};

template<class T>
struct holder
{
private:
  T kept;

public:
  holder(T x)
    : kept(x)
  {
  }

  template<class U>
  friend U peek(holder<U> h);
};

template<class U>
U peek(holder<U> h)
{
  return h.kept;
}

int main()
{
  return (int)unwrap(box<int>(4)) + (int)unwrap(box<int>(5)) +
                 (int)peek(holder<char>('\1')) ==
             10
           ? 0
           : 1;
}
