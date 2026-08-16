// VALIDATION: compile-fail
// 14.5.3p5: the pattern of a pack expansion shall name a parameter pack *this*
// expansion is over.  `Rest` is expanded by the inner `...`, so the outer one
// here is written over nothing at all.

template<class...>
struct list
{
};

template<class T>
struct wrap
{
};

template<class... Args>
struct outer
{
  template<class... Rest>
  struct inner
  {
    typedef list<wrap<list<Rest...> >...> made;
  };
};

int main()
{
  outer<char>::inner<int, long>::made made;
  (void)made;
  return 0;
}
