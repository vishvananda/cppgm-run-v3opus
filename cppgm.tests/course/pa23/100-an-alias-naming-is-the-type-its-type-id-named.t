// VALIDATION: compile-pass
// 14.5.7p1: a template-id naming a specialization of an alias template *is* the
// associated type its type-id named, so a declaration writing the naming and a
// declaration writing that type out longhand are two declarations of one
// template - a redeclaration, a definition written for it, an out-of-class
// member and a member template alike.
//
// 14.8.2p8 is the other half and is what bounds it: an argument the type-id
// threw away is still built where 14.7.1p1 puts a list behind the naming, and
// building it is what drops a candidate.  So the naming stands for itself only
// where an argument is one a substitution *builds* - `typename T::pointer` is
// looked up, `S<T>` is instantiated, `T &` is derived - and a place a list binds
// is looked up and can refuse nothing, which leaves `discard<T>` and
// `discard<Ts...>` the `void` they name wherever they are written.

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

template<class T>
using discard_one = void;

template<class T>
using through = discard_one<T>;

template<class... Ts>
using discard_run = void;

template<class T>
struct box
{
};

// 14.5.7p1 at 14.5.6.1p5: the declaration writes the naming and the definition
// writes what its type-id named.
template<class T>
void take(discard_one<T> *p);

template<class T>
void take(void *p)
{
}

// The same over a return type, and through an alias naming another alias.
template<class T>
through<T> give(T x);

template<class T>
void give(T x)
{
}

// The same where the argument thrown away is a run.
template<class... Ts>
void run(discard_run<Ts...> *p);

template<class... Ts>
void run(void *p)
{
}

// 9.3p2: a member of a class template whose declaration wrote the naming and
// whose definition, written outside the class, wrote the type.
template<class T>
struct holder
{
  discard_one<T> m(T x);
  int n(discard_one<T> *p);
};

template<class T>
void holder<T>::m(T x)
{
}

template<class T>
int holder<T>::n(void *p)
{
  return 4;
}

// 14.5.6.1p5 over a member template of a class that is not one.
struct outer
{
  template<class T>
  discard_one<T> mm(T x);
};

template<class T>
void outer::mm(T x)
{
}

static_assert(is_same<discard_one<int>, void>::value, "");
static_assert(is_same<box<through<int> >, box<void> >::value, "");

// 14.8.2p8: what the naming keeps an entry for, which is an argument the
// substitution reads again - the detector still answers the two classes apart.
template<class T>
using pointer_of = typename T::pointer;

template<class...>
using discard = void;

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

struct with_pointer
{
  typedef int * pointer;
};

struct without_pointer
{
};

static_assert(detected<char, void, with_pointer>::found, "");
static_assert(!detected<char, void, without_pointer>::found, "");
static_assert(is_same<detected<char, void, with_pointer>::type, int *>::value,
              "");

// 14.8.2p8 at a place written bare: `discard<T>` is the `void` its type-id
// named for every `T` there is, so the candidate stands wherever the list can
// be bound at all - `void` included, which no reading of `T` can refuse.
template<class T>
char place_probe(discard<T> *p);

template<class T>
long place_probe(...);

int main()
{
  int i = 0;
  take<int>(&i);
  give(1);
  run<int, char>(&i);
  outer o;
  o.mm(1);
  holder<int> h;
  h.m(1);
  if (sizeof(place_probe<int>(0)) != 1 || sizeof(place_probe<void>(0)) != 1)
  {
    return 2;
  }
  return h.n(0) - 4;
}
