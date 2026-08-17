// 14.2 writes a template-argument-list inside a name, so `R(A...) const &`
// reaches the semantic layer as text.  8.3.5p1's parameter clause is read there
// too: the `...` after a parameter whose type holds a pack expands that
// parameter rather than opening 8.3.5p3's ellipsis, the cv-qualifier-seq and
// the ref-qualifier after the clause are part of the function type, and
// 8.3.5p5 makes a parameter of function type a pointer to one.

template<class T>
struct shape
{
  static const int value = 0;
};

template<class R, class... A>
struct shape<R(A...)>
{
  static const int value = 1 + sizeof...(A);
};

template<class R, class... A>
struct shape<R(A...) const>
{
  static const int value = 20;
};

template<class R, class... A>
struct shape<R(A...) &>
{
  static const int value = 30;
};

template<class R, class... A>
struct shape<R(A...) const &>
{
  static const int value = 40;
};

template<class T>
struct nested
{
  static const int value = 0;
};

template<class R, class A>
struct nested<R (*)(A)>
{
  static const int value = 7;
};

template<class T>
struct call
{
  static const int value = 0;
};

template<class Fun, class A0>
struct call<Fun(A0)>
{
  static const int value = nested<A0>::value;
};

struct tag {};

static_assert(shape<int()>::value == 1, "an empty run is an empty list");
static_assert(shape<int(char, long)>::value == 3, "a run of two");
static_assert(shape<int(char) const>::value == 20, "a cv-qualified pattern");
static_assert(shape<int(char) &>::value == 30, "a ref-qualified pattern");
static_assert(shape<int(char) const &>::value == 40, "both of them");
static_assert(call<tag(int(char))>::value == 7,
              "8.3.5p5 makes a function parameter a pointer");

int main()
{
  return shape<int(char, long)>::value == 3 ? 0 : 1;
}
