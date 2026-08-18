// VALIDATION: compile-pass
// 7.1.3p2 with 14.8.2p8: a template-id over an alias template *is* the type its
// type-id named, so a naming of one holds no argument once it has been read.
// That is right until an argument is one no argument list has settled: it is
// built where 14.7.1p1's substitution puts a list behind it, and building it is
// what discards a candidate.  `discard<typename T::pointer>` names `void`
// however it is written, and collapsing it where it stands leaves every `T`
// agreeing with the detector's partial specialization - the trait then answers
// `true` for a class that declares no such member.
//
// 14.5.3p5 over the same reading: `enable_if_t<bool(Bn::ok)>...` names its pack
// through a value argument read again rather than rebuilt, so the pack appears
// nowhere in what the argument was interned as.  An expansion of it is one
// reading per element all the same.

template<class A, class B>
struct is_same
{
  static const bool value = false;
};

template<class A>
struct is_same<A, A>
{
  static const bool value = true;
};

template<bool B, class T = void>
struct enable_if
{
};

template<class T>
struct enable_if<true, T>
{
  typedef T type;
};

template<bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<class...>
using discard = void;

template<class T>
using pointer_of = typename T::pointer;

// 14.5.5.1p1 with 14.8.2p8: the pattern's second argument is a naming that
// throws `pointer_of<A>` away, and a class with no `pointer` is a list this
// pattern does not match rather than a program to refuse.
template<class Fallback, class Void, class A>
struct detected
{
  typedef Fallback type;
  static const bool found = false;
};

template<class Fallback, class A>
struct detected<Fallback, discard<pointer_of<A> >, A>
{
  typedef pointer_of<A> type;
  static const bool found = true;
};

template<class Fallback, class A>
using detected_or_t = typename detected<Fallback, void, A>::type;

struct with_pointer
{
  typedef int * pointer;
};

struct without_pointer
{
};

static_assert(detected<char, void, with_pointer>::found, "");
static_assert(!detected<char, void, without_pointer>::found, "");
static_assert(is_same<detected_or_t<char, with_pointer>, int *>::value, "");
static_assert(is_same<detected_or_t<char, without_pointer>, char>::value, "");

// 14.5.7p1 with 14.5.3p4: the same clause where the arguments thrown away are a
// run.  `first_of` names its first argument and nothing else, so the expansion
// beside it is built for its refusals alone.
template<class T, class...>
using first_of = T;

struct true_tag
{
  static const bool value = true;
};

struct false_tag
{
  static const bool value = false;
};

template<class... Bn>
auto all_of(int) -> first_of<true_tag, enable_if_t<bool(Bn::ok)>...>;

template<class... Bn>
auto all_of(...) -> false_tag;

struct yes
{
  static const bool ok = true;
};

struct no
{
  static const bool ok = false;
};

static_assert(decltype(all_of<yes>(0))::value, "");
static_assert(decltype(all_of<yes, yes>(0))::value, "");
static_assert(!decltype(all_of<no>(0))::value, "");
static_assert(!decltype(all_of<yes, no>(0))::value, "");

// 14.5.3p4 where the pattern of an expansion is not a place: the run a list
// gives `Ts` is written into `held` as one entry per element, each `Ts &`.
template<class... Ts>
struct held
{
  static const int length = sizeof...(Ts);
};

template<class... Ts>
using held_by_reference = held<Ts &...>;

template<class... Ts>
struct carrier
{
  typedef held_by_reference<Ts...> type;
};

static_assert(carrier<int, char>::type::length == 2, "");
static_assert(is_same<carrier<int>::type, held<int &> >::value, "");

int main()
{
  return 0;
}
