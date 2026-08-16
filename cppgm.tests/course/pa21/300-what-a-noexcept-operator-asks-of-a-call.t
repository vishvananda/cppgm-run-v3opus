// 5.3.7p3: the `noexcept` operator is false where its unevaluated operand
// would hold a potentially-evaluated call to a function without a non-throwing
// exception-specification.  The operand is not read as syntax: which
// declaration each call reaches is 13.3's choice, and 15.4 is asked of that
// declaration - a constructor 8.5 chose, the copy of a by-value argument, the
// specialization a template-id names, and 15.4p14's implicit specification of a
// defaulted special member alike.  15.4p1's own condition is a constant
// expression and not the one spelling `true`.

void plain();
void quiet() noexcept;
void computed() noexcept(sizeof(int) == 4 && !false);
void written() throw();

struct member
{
  member(int = 0) {}
};

struct owner
{
  member value;
  owner() = default;
};

struct empty
{
  empty() = default;
};

struct copied
{
  copied() noexcept {}
  copied(const copied &) {}
};

void by_value(copied) noexcept;
copied an_object;

template <class T>
T &&declared() noexcept;

struct callable
{
  constexpr int operator()(int, int) const noexcept { return 0; }
};

template <bool B>
struct tag
{
  static const int chosen = B ? 1 : 2;
};

static_assert(noexcept(quiet()), "an explicit specification");
static_assert(!noexcept(plain()), "no specification allows every exception");
static_assert(noexcept(computed()), "15.4p1's condition is folded");
static_assert(noexcept(written()), "an empty type-id-list says the same");
static_assert(!noexcept(owner()), "a defaulted constructor takes its member's");
static_assert(noexcept(empty()), "and says nothing where no member throws");
static_assert(!noexcept(by_value(an_object)), "the copy of an argument counts");
static_assert(noexcept(declared<const callable &>()(1, 2)),
              "a specialization keeps the template's specification");
static_assert(tag<noexcept(quiet())>::chosen == 1,
              "the operator written as a template argument");
static_assert(tag<noexcept(plain())>::chosen == 2, "and its other answer");
static_assert(noexcept(noexcept(plain())), "the operator evaluates nothing");

int main()
{
  return noexcept(owner()) ? 1 : 0;
}
