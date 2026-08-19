// VALIDATION: compile-pass
// 14.7.1p1 and 3.2p2 are two sentences about one naming and they answer
// differently.  5p8 leaves the operand of `decltype` and of `noexcept`
// unevaluated, so nothing named there is odr-used and this unit owes no
// definition of `width<int>` - which is why no `@width` function stands in the
// output below.  But a *template argument* written inside that operand is a
// constant expression all the same, and reading what it comes to is a context
// that requires the definition of the function to exist.  So the demand the
// naming does not make is made by the fold that needs the body.

template<int N>
struct box
{
  char pad[N];
};

template<class T>
constexpr int width()
{
  return sizeof(T) + 1;
}

template<int N>
int taking()
{
  return N;
}

int main()
{
  typedef decltype(box<width<int>()>()) inside_decltype;
  const bool inside_noexcept = noexcept(taking<width<char>()>());
  return (sizeof(inside_decltype) == 5 && inside_noexcept) ? 0 : 1;
}
