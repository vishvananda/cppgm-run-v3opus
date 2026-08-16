// 5.19p2 with 13.3 and 14.8.2: a call written in a constant expression names
// the declaration overload resolution chooses, and it chooses it the way it
// does anywhere else.  A callee written as a template-id names 14.8.1's
// specializations; an unqualified one also names what 3.4.2's associated
// namespaces declare; a set two declarations of the same arity stand in is
// ranked by 13.3.3's conversions and 14.5.6.2's partial ordering rather than
// left ambiguous; 9.3.2p1's cv-qualification of the object is what a member
// candidate's object parameter has to accept; 13.5.4p1's parentheses written
// on an object of class type call its `operator()`; and 14.5.3p4's function
// parameter pack binds a run of the arguments the call wrote.

template <class T>
constexpr T twice(T x)
{
  return x + x;
}

// 13.3.3.2 with 8.5.3p5: a constant is a value, so `T&` binds nothing here
// and `T const&` binds everything - and where both would, 14.5.6.2 orders the
// templates and the more specialized one wins.
template <unsigned, class T>
int rank(T &)
{
  return 0;
}

template <unsigned, class T>
constexpr int rank(T const &)
{
  return 1;
}

template <unsigned I, class T>
constexpr int through(T const value)
{
  return rank<I>(value);
}

namespace outer
{
struct tagged
{
  int held;
};

// 3.4.2p2: found by the type of the argument and by nothing else.
constexpr int reached(tagged what)
{
  return what.held;
}
}

namespace inner
{
template <int N>
constexpr int numbered()
{
  return N;
}
}

constexpr int through_directive()
{
  using namespace inner;
  return numbered<7>();
}

struct counter
{
  int held;
  constexpr counter(int a) : held(a) {}
  // 13.3.1p3: the object is an argument, and a constant object is const - so
  // this is the candidate a call on one reaches and `grow` is not.
  constexpr int read() const { return held; }
  int read() { return held + 1000; }
  int grow() { return ++held; }
};

struct doubler
{
  int by;
  constexpr int operator()(int x) const { return x * by; }
};

template <class F>
constexpr int applied(F f, int x)
{
  return f(x);
}

constexpr int total()
{
  return 0;
}

template <class First, class... Rest>
constexpr int total(First first, Rest... rest)
{
  return static_cast<int>(first) + total(rest...);
}

constexpr counter one(41);
constexpr outer::tagged marked = {5};
constexpr doubler twofold = {2};

static_assert(twice<int>(3) == 6, "a template-id names a specialization");
static_assert(twice(4) == 8, "and deduction names one from the arguments");
static_assert(through<0>(0) == 1, "13.3 ranks a set arity cannot tell apart");
static_assert(reached(marked) == 5, "3.4.2 reaches the argument's namespace");
static_assert(through_directive() == 7, "a using-directive in a folded body");
static_assert(one.read() == 41, "a const object reaches the const candidate");
static_assert(applied(twofold, 21) == 42, "an object of class type is called");
static_assert(total(1, 2, 3, 4) == 10, "a pack binds a run of the arguments");
static_assert(total() == 0, "and the run it stands for may hold nothing");

int main()
{
  return one.read() + twice<int>(3) - 47;
}
