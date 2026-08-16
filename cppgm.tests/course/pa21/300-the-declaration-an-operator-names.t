// 13.3.1.2p1 with 5.19p2: an operator written on an operand of class or
// enumeration type is a call of an operator function, and a constant expression
// chooses that declaration the way every other reading of the operator does.
// The set is 13.3.1.2p3's - the member operator functions the left operand's
// class declares, the non-member ones an unqualified lookup and 3.4.2 reach -
// and the ranking is 13.3's, so a member operator, one 3.4.2 alone reaches, an
// operator template and a second operand that reaches its place by a converting
// constructor are each named here exactly as they are in a body the program
// runs.

struct duration
{
  int rep;

  constexpr explicit duration(int value) : rep(value) {}
  constexpr int count() const { return rep; }

  // 13.5.1p1: a unary operator declared as a member, which a constant
  // expression reaches before 5.3.1's built-in reading of the operand.
  constexpr duration operator-() const { return duration(-rep); }
  // 13.5.5p1: a subscript is a member operator function and no reading of an
  // array at all.
  constexpr int operator[](int index) const { return rep + index; }
};

constexpr duration source(3);
constexpr duration negated = -source;

static_assert(negated.count() == -3, "a member operator function is named");
static_assert(source[4] == 7, "13.5.5p1's subscript is that call too");

namespace units
{
struct counted
{
  int held;

  constexpr counted(int value) : held(value) {}
};

// 3.4.2p2: declared beside its own operand's class and nowhere the operator is
// written, so the argument-dependent lookup is the whole of what reaches it -
// and 5.2.2p4 initializes the second place from `2` by the converting
// constructor 13.3 chose when it ranked this call.
constexpr counted operator+(counted const &a, counted const &b)
{
  return counted(a.held + b.held);
}
}

static_assert((units::counted(1) + 2).held == 3, "3.4.2 reaches the operator");

template <class T>
struct quantity
{
  T value;

  constexpr explicit quantity(T v) : value(v) {}
};

// 14.8.2: a template is a candidate through the specialization the operands
// deduce, here as at a call written with parentheses.
template <class T>
constexpr bool operator<(quantity<T> left, quantity<T> right)
{
  return left.value < right.value;
}

static_assert(quantity<int>(1) < quantity<int>(2), "an operator template");

struct flag
{
  int on;

  constexpr flag(int value) : on(value) {}
};

// 13.5p4 with 5.14p2: an overloaded `&&` and `||` are ordinary calls, so both
// operands are evaluated and the result is what the declaration returns rather
// than a `bool`.
constexpr flag operator||(bool left, flag right) { return left ? flag(1) : right; }
constexpr flag operator&&(flag left, bool right) { return left.on ? flag(right) : flag(0); }

static_assert((false || flag(9)).on == 9, "a left operand of built-in type");
static_assert((flag(1) && true).on == 1, "and a left operand of class type");

struct factory
{
  // 13.5.3p1: an assignment written on an operand no evaluation declared is a
  // call and not 5.17p1's write-back at all.
  constexpr int operator=(int value) const { return value + 1; }
};

static_assert((factory() = 6) == 7, "13.5.3p1's assignment is a call");

struct scale
{
  constexpr duration operator()(duration const &input) const
  {
    return duration(input.count() * 2);
  }
};

// 13.5.4p1: the parentheses are written on a temporary, which is no name for a
// lookup to answer - so what they call is an `operator()` of its class.
static_assert(scale()(duration(4)).count() == 8, "a call on a temporary");

struct token
{
};

struct property
{
  int value;

  constexpr property() : value(0) {}
  constexpr property(token) : value(1) {}
};

// 6.6.3p2: the returned value initializes an object of the *declared* return
// type, which for a class is the constructor 13.3.1.4 chose.
constexpr property required() { return token(); }

static_assert(required().value == 1, "6.6.3p2 initializes the return type");

// 8.5.1p1 is a fact of the class template's *definition*, so a fold that
// reaches a specialization asks 14.7.1p1 for one before reading it - and
// 3.9p10's array member is then a subobject like any other.
template <class T>
struct pair
{
  T first;
  T second;
};

template <class T>
struct holder
{
  pair<T> items[2];
};

constexpr holder<int> held = {{{1, 2}, {3, 4}}};

static_assert(held.items[0].second == 2, "a nested clause down the object");
static_assert(held.items[1].first == 3, "and the element after it");

int main()
{
  return negated.count() + 3;
}
