// VALIDATION: compile-pass
// 5.19p2: the lvalue-to-rvalue conversion applied to a subobject of a string
// literal, which is the one object a constant expression reads out of storage.

static_assert("ab"[0] == 97, "");
static_assert("ab"[1] == 98, "");
static_assert("ab"[2] == 0, "");
static_assert(L"ab"[1] == 98, "");
static_assert(u"ab"[1] == 98, "");
static_assert(U"ab"[1] == 98, "");
static_assert(u8"ab"[1] == 98, "");
static_assert("ab" "cd"[3] == 100, "");
static_assert("a\tb"[1] == 9, "");
static_assert(("ab")[1] == 98, "");
static_assert("abc"[1 + 1] == 99, "");
static_assert(sizeof(L"a"[0]) == 4 && sizeof("a"[0]) == 1, "");

enum letter { z = "xyz"[2] };

template<int N>
struct place
{
  static const int value = N;
};

// 14.2 writes an argument list inside a name, so the same element is read out
// of a spelling - encoding-prefix, subscript and all.
typedef place<"ab"[1]> narrow;
typedef place<L"ab"[1]> wide;
typedef place<u8"xy"[0]> utf8;
typedef place<L'a'> prefixed_character;

int main()
{
  int bound["abc"[0] - 94];
  return narrow::value == 98 && wide::value == 98 && utf8::value == 120 &&
         prefixed_character::value == 97 && z == 122 &&
         sizeof(bound) / sizeof(int) == 3
    ? 0 : 1;
}
