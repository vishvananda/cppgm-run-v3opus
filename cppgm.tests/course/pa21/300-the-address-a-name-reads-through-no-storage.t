// VALIDATION: compile-pass
// N3485 focus: 3.2 [basic.def.odr], 5.19 [expr.const], 9.4.2 [class.static.data]
//
// 9.4.2p3 lets a class initialize a static data member it declares `constexpr`,
// and 9.4.2p2 leaves the object that member names to a definition written
// outside the class.  3.2p2 says what a program that writes no such definition
// may still do with the name: an lvalue-to-rvalue conversion applied to it is
// no odr-use, because the value is one the translation already has.
//
// For an arithmetic member that value is a number.  For a member of pointer
// type it is 5.19p2's address constant, which is no number at all - it is
// *which object* the pointer designates, and the object file names that object
// by a symbol.  So what such a name is worth where it is read is the
// brace-or-equal-initializer the class wrote, read again where the name stands:
// the literal's own object for `"x"`, the declaration's symbol for `&counter`,
// the function's for `&step`, one element in for `&numbers[2]`, and 4.10p1's
// null pointer value for `nullptr`.  Nothing names the member's own storage, so
// the unit lays none out and declares none.
//
// 5.3.1p3 is the other half of the same sentence: `&` written on the name is a
// pointer to the *object* the member declares, which is an odr-use of it - so
// there the name is the storage and the unit declares it.  The two readings are
// one question asked at the name, and every door that reaches a name asks it:
// the qualified `holder::text`, the unqualified one written inside a member
// function, and the one written after an object expression.

int counter = 7;
int numbers[4] = {1, 2, 3, 4};

int step(int value)
{
  return value + 1;
}

struct holder
{
  static constexpr const char* text = "xy";
  static constexpr const char* second = "xy" + 1;
  static constexpr int* counted = &counter;
  static constexpr int* third = &numbers[2];
  static constexpr int (*acted)(int) = &step;
  static constexpr const char* nothing = nullptr;

  const char* own_text() const
  {
    // 3.2p2 at a name written with no nested-name-specifier: the same reading,
    // reached through 9.4p2's member of the class rather than through a
    // qualified name.
    return text;
  }

  const char* const* own_place() const
  {
    // 5.3.1p3 at the same name: the object the member declares, which is what
    // 9.4.2p2's definition lays out and what this address names.
    return &text;
  }
};

template<class T>
struct traits
{
  static constexpr const char* name = "z";
};

int read_char(const char* at)
{
  return *at;
}

int read_int(const int* at)
{
  return *at;
}

int read_place(const char* const* at)
{
  return **at;
}

// 3.6.2p2: an initializer written outside every body is the image the object
// starts as, so the same reading gives this object the literal's address and
// the program runs nothing before `main`.
const char* held = holder::text;

int main()
{
  holder one;
  int total = 0;
  total += read_char(holder::text);
  total += read_char(holder::second);
  total += read_char(traits<int>::name);
  total += read_char(traits<char>::name);
  total += read_int(holder::counted);
  total += read_int(holder::third);
  total += holder::acted(4);
  total += holder::nothing == nullptr ? 1 : 0;
  // 5.2.5p1 with 9.4p2: the object expression says where to look and the name
  // denotes the member of the class, which is the same constant it is anywhere.
  total += read_char(one.text);
  total += read_char(one.own_text());
  total += read_place(one.own_place());
  total += read_place(&holder::text);
  total += read_char(held);
  return total == 0 ? 0 : 1;
}
