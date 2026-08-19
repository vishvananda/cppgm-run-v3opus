// VALIDATION: run-pass
// 7.2p1 and 7.2p3: an enum-specifier that wrote its braces defines the
// enumeration however many enumerators stood between them, and one that wrote
// none is 7.2p3's elaborated-type-specifier naming an enumeration that already
// exists.  What tells the two apart is the `}`, which is the same fact the
// declaration with no declarator reads - so `enum E {} e;` defines an
// enumeration and declares an object of it exactly as `enum E { x } e;` does,
// and `enum E e;` written below still names the one already defined.

enum plain {} first;
enum class scoped {} second;
enum sized : short {} third;
enum {} unnamed;

enum plain later;

struct outer
{
  enum member {} held;
};

template<class T>
struct wrapper
{
  enum nested {} kept;
};

typedef enum {} anonymous;

enum spelled {} * addressed = 0;

int measure(plain)
{
  return 1;
}

int main()
{
  outer o = outer();
  wrapper<int> w = wrapper<int>();
  anonymous a = anonymous();
  return measure(first) == 1 && sizeof(third) == 2 && sizeof(sized) == 2 &&
      static_cast<int>(second) == 0 && static_cast<int>(unnamed) == 0 &&
      static_cast<int>(later) == 0 && static_cast<int>(o.held) == 0 &&
      static_cast<int>(w.kept) == 0 && static_cast<int>(a) == 0 &&
      addressed == 0
    ? 0 : 1;
}
