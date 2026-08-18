// VALIDATION: compile-pass
// 14.8.2p8 at 14.5.5.1p1's match: substituting the deduced arguments back into
// a partial specialization's pattern is part of asking whether the pattern
// matches, so a member only some arguments' class declares makes the pattern
// one *those* lists match rather than a program to refuse.  3.4.3.1p1 has no
// region to look the name up in where the prefix is settled to something that
// is not a class, so an enumeration and a fundamental type are refusals of the
// same kind and not "an argument list has yet to say".

template<class...>
struct make_void
{
  typedef void type;
};

template<class... T>
using void_t = typename make_void<T...>::type;

struct yes
{
  static const bool value = true;
};

struct no
{
  static const bool value = false;
};

template<class T, class = void>
struct has_member : no
{
};

template<class T>
struct has_member<T, void_t<typename T::member> > : yes
{
};

struct with
{
  typedef int member;
};

struct without
{
};

union with_union
{
  int held;
  typedef int member;
};

enum plain_enum
{
  one
};

enum class scoped_enum
{
  two
};

static_assert(has_member<with>::value, "");
static_assert(has_member<with_union>::value, "");
static_assert(has_member<const with>::value, "");
static_assert(!has_member<without>::value, "");
static_assert(!has_member<plain_enum>::value, "");
static_assert(!has_member<scoped_enum>::value, "");
static_assert(!has_member<int>::value, "");
static_assert(!has_member<int *>::value, "");
static_assert(!has_member<int &>::value, "");
static_assert(!has_member<int[3]>::value, "");
static_assert(!has_member<void>::value, "");

int main()
{
  return has_member<with>::value && !has_member<plain_enum>::value ? 0 : 1;
}
