// N3485 focus: 5.19 [expr.const] p3 with 8.5 [dcl.init] p16 and 8.5.4
// [dcl.init.list] p3 - a const object of integral type initialized by a
// constant expression is one, and `T k(x)` and `T k{x}` initialize `k` with
// the very expression `T k = x` does.  So all three spellings may be written
// where an array bound, a static_assert condition or a template argument is.
enum tag { tag_value = 1 };

const int parenthesized(3);
const int braced{4};
constexpr tag direct(tag_value);

int by_paren[parenthesized];
int by_brace[braced];

template<int N>
struct box {
  static const int value = N;
};

int main()
{
  static_assert(parenthesized == 3, "");
  static_assert(braced == 4, "");
  static_assert(direct == tag_value, "");
  if (sizeof(by_paren) != 3 * sizeof(int)) { return 1; }
  if (sizeof(by_brace) != 4 * sizeof(int)) { return 2; }
  if (box<parenthesized>::value != 3) { return 3; }
  if (box<braced>::value != 4) { return 4; }
  return 0;
}
