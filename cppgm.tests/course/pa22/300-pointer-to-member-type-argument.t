// 14.2 writes a template-argument-list inside a name, so a type-id reaches the
// semantic layer as text - and 8.3.3p1's `nested-name-specifier *` is a
// ptr-operator that reading has to know, or `int C::*` reads as a
// type-specifier-seq spelling `int C::` and names no type at all.  The class
// before the `*` is looked up the way any other name in a type-id is, so a
// place a head declared answers there as much as a class the program wrote.

struct point
{
  int x;
  int y;
  int at(char) const;
};

struct outer
{
  struct inner
  {
    long v;
  };
};

template<class T>
struct shape
{
  static const int value = 0;
};

template<class M, class K>
struct shape<M K::*>
{
  static const int value = 1;
};

template<class R, class K, class... A>
struct shape<R (K::*)(A...) const>
{
  static const int value = 2;
};

template<class T>
struct owner
{
  static const int value = 0;
};

template<class M>
struct owner<M point::*>
{
  static const int value = 5;
};

template<class M>
struct owner<M outer::inner::*>
{
  static const int value = 6;
};

static_assert(shape<int point::*>::value == 1, "a data member pointer");
static_assert(shape<long outer::inner::*>::value == 1, "a nested owner");
static_assert(shape<int (point::*)(char) const>::value == 2,
              "8.3.5p1's qualifiers after a member function's clause");
static_assert(shape<int *>::value == 0, "a plain pointer is neither");
static_assert(owner<int point::*>::value == 5, "the class the members are of");
static_assert(owner<long outer::inner::*>::value == 6, "and the other one");

int main()
{
  return shape<int point::*>::value + shape<int (point::*)(char) const>::value +
                 owner<long outer::inner::*>::value ==
             9
           ? 0
           : 1;
}
