// VALIDATION: run-pass
// 5.2.3p1 writes `T(x)` and 5.2.3p3 `T{...}` with any type-specifier, and
// 7.1.6.3p1's `typename` is one - so a constant expression written as a
// template argument may open with the keyword.  The split hands the keyword
// and the qualified name on as two words, and they are one specifier.

template<bool B>
struct bool_constant
{
  static const bool value = B;
};

struct by_value {};
struct by_reference {};

struct value_result { typedef by_value kind; };
struct reference_result { typedef by_reference kind; };

template<class F>
constexpr bool copies(by_value) { return true; }

template<class F>
constexpr bool copies(by_reference) { return false; }

template<class Result, class F>
struct copies_its_argument
  : bool_constant<copies<F>(typename Result::kind{})>
{
};

int main()
{
  static_assert(copies_its_argument<value_result, int>::value,
                "a braced typename-specifier is an operand");
  static_assert(!copies_its_argument<reference_result, int>::value,
                "and picks the other declaration for the other tag");
  return copies_its_argument<value_result, int>::value ? 0 : 1;
}
