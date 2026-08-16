// VALIDATION: compile-pass
// N3485 focus: 3.9 [basic.types], 7.1.5 [dcl.constexpr], 8.5 [dcl.init]
//
// 3.9p10's third bullet asks a class for one constexpr constructor that is not
// a copy or move constructor, and for a class that declares none the
// constructor 12.1p5 defines is the one it reads.  What 12.1p5 asks of that
// constructor is 7.1.5p4's whole list; what 3.9p10 asks of it is only that
// every base and every member of class type be built by a constexpr one - so a
// class 8.5.1p1 leaves no aggregate, because it has a base or because 11p1
// hides a member, is a literal type all the same, and `constexpr` on an object
// of one, or on a function returning one, is a declaration the program was
// allowed to write.
//
// 8.5p6 is the sentence that refuses instead: a declaration of such a class
// that writes no initializer is initialized by nothing at all, which is why
// every object here writes one.
//
// Beside them, 7.1.5p3 and 7.1.5p4 asked at the two doors that are neither an
// ordinary function definition nor an object: a constructor's parameter types
// and a conversion function's return type are literal here, so both
// declarations stand.

struct empty_base
{
};

struct derived_object : empty_base
{
  int value;
};

struct hidden_member
{
private:
  int value;
};

struct counted_base
{
  int count;
};

struct derived_counter : counted_base
{
  int extra;
};

struct literal_pair
{
  int first;
  int second;

  constexpr literal_pair(int a, int b) : first(a), second(b)
  {
  }

  constexpr operator int() const
  {
    return first + second;
  }
};

constexpr derived_object built = derived_object();
constexpr hidden_member hidden = hidden_member();
constexpr derived_counter counted = {};
constexpr literal_pair pair_value(3, 4);

static_assert(pair_value == 7, "");
static_assert(pair_value.second == 4, "");

constexpr derived_object make()
{
  return derived_object();
}

int main()
{
  derived_object local = make();
  return local.value + pair_value.first - 3;
}
