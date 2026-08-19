// VALIDATION: run-pass
// 7.2p1 and 7.2p2: the enumerator-list of an enum-specifier is optional, so
// `enum E {}` defines an enumeration that declares no enumerator - and what
// tells a definition from an opaque-enum-declaration is the braces the program
// wrote and not what stood between them.  An unscoped opaque declaration fixes
// no underlying type and is still a program 7.2p2 refuses; this is not one.

enum plain {};
enum class scoped {};
enum sized : short {};

struct outer
{
  enum member {};
};

template<class T>
struct held
{
  enum nested {};
};

int measure(plain)
{
  return 1;
}

int main()
{
  plain a = plain();
  scoped b = scoped();
  sized c = sized();
  outer::member d = outer::member();
  held<int>::nested e = held<int>::nested();
  (void)b;
  (void)c;
  (void)d;
  (void)e;
  return measure(a) == 1 && sizeof(sized) == 2 ? 0 : 1;
}
