// N3485 focus: 5.2.3 [expr.type.conv] p1, p2 and p3 - `T(x)` is the cast
// `(T)x`, `T()` is the value-initialization 8.5 [dcl.init] p7 writes, and
// `T{x}` is written where `T(x)` is.  Each folds to an integral constant
// wherever `(T)x` and `static_cast<T>(x)` do: an array bound, an enumerator, a
// constexpr object and a template argument.
enum kind { first = int(3), second = short(4) };

constexpr short narrowed = short(42);

int sized[int(3)];

template<int N>
struct box {
  static const int value = N;
};

int main()
{
  static_assert(int() == 0, "");
  static_assert(int{5} == 5, "");
  static_assert(narrowed == 42, "");
  static_assert(kind(first) == first, "");
  if (sizeof(sized) != 3 * sizeof(int)) { return 1; }
  if (first != 3 || second != 4) { return 2; }
  if (box<int(3)>::value != 3) { return 3; }
  if (box<unsigned(7)>::value != 7) { return 4; }
  return 0;
}
