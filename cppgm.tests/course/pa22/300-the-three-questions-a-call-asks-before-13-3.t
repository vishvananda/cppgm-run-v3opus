// N3485 focus: 3.4.2 [basic.lookup.argdep] p3 with 5.2.3 [expr.type.conv] p1
// and 13.3.2 [over.match.viable] p2 - three questions one call asks before 13.3
// has a set to choose from.
//
// p3 empties the argument-dependent set where the ordinary lookup found a class
// member, a block-scope function declaration *that is not a using-declaration*,
// or something that is no function at all - so a using-declaration written in a
// block leaves the associated namespaces searched, and the declaration it named
// is one candidate among what they add rather than the answer.
//
// 5.2.3: a callee written as a parenthesized postfix-expression is not a
// type-id, so `(dispatch(ex))(f, w)` calls what the parentheses made rather
// than converting to a type - 8.1p1 leaves no declarator-id inside a type-id's
// own parentheses, which is what tells the two apart.
//
// 13.3.2p2: a constructor whose parameter-declaration-clause is `...` alone is
// viable for one argument, so 13.3.3.1.2p1 reaches the class through it - and
// 13.3.3.1p4 leaves the class's copy constructor no way in, because there the
// first parameter considers no user-defined conversion sequence of its own.

namespace lib
{
enum class errc { value };

namespace shadowing
{
// Reached only through the using-declaration below, and viable for no call of
// one argument - so what the call runs is what 3.4.2 found.
int make_code();
}

int make_code(errc) { return 7; }

struct reader
{
  int seen;
  reader() : seen(0) {}

  int read(errc e)
  {
    using shadowing::make_code;
    return make_code(e);
  }
};
}

struct work {};

template<class T>
struct dispatch
{
  int seen;
  explicit dispatch(const T& v) : seen(v) {}
  template<class W> int operator()(W, int extra) const { return seen + extra; }
};

typedef char (&one_byte)[1];
typedef char (&two_bytes)[2];

struct anything
{
  anything(...);
};

one_byte arity(anything);
two_bytes arity(anything, anything);

struct only_ellipsis
{
  only_ellipsis(...) : n(5) {}
  only_ellipsis(const only_ellipsis& o) : n(o.n) {}
  int n;
};

int through(only_ellipsis o) { return o.n; }

int main()
{
  lib::reader r;
  if (r.read(lib::errc::value) != 7)
  {
    return 1;
  }

  int ex = 4;
  if ((dispatch<int>(ex))(work(), 3) != 7)
  {
    return 1;
  }

  // The ellipsis reaches the constructor with no written parameter, and the
  // copy constructor beside it is no second way in for an argument of another
  // type.
  if (through(1) != 5)
  {
    return 1;
  }

  return (sizeof(arity(work())) == 1 && sizeof(arity(work(), work())) == 2)
    ? 0 : 1;
}
