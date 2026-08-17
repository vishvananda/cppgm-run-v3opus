// 8.3.5p7 makes the ref-qualifier written after a parameter-clause part of the
// function type, so `int(char) const` and `int(char) const &` are two types,
// two specializations of a template named with them, and two entities the
// object file has to name apart.  A specialization's own spelling writes the
// cv-qualifier-seq and the ref-qualifier after the parameter-clause, and the
// ABI's `<function-type>` writes `R` or `O` inside its `E`.

template<class T>
struct holder
{
  static int which() { return 0; }
};

template<>
struct holder<int(char)>
{
  static int which() { return 1; }
};

template<>
struct holder<int(char) const>
{
  static int which() { return 2; }
};

template<>
struct holder<int(char) const &>
{
  static int which() { return 3; }
};

template<>
struct holder<int(char) volatile &&>
{
  static int which() { return 4; }
};

template<class T>
struct sorted
{
  static const int value = 0;
};

template<class R, class... A>
struct sorted<R(A...) &>
{
  static const int value = 10;
};

template<class R, class... A>
struct sorted<R(A...) &&>
{
  static const int value = 20;
};

static_assert(sorted<long(int) &>::value == 10, "an lvalue ref-qualifier");
static_assert(sorted<long(int) &&>::value == 20, "an rvalue ref-qualifier");
static_assert(sorted<long(int)>::value == 0, "and neither pattern takes none");

int main()
{
  const int all = holder<int(char)>::which() +
                  holder<int(char) const>::which() * 10 +
                  holder<int(char) const &>::which() * 100 +
                  holder<int(char) volatile &&>::which() * 1000;
  return all == 4321 ? 0 : 1;
}
