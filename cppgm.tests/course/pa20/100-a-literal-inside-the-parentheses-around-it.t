// VALIDATION: compile-pass
// 5.1.1p6: a parenthesized expression is the expression it holds, so the
// string literal 5.19p2 reads an element out of is the same one however many
// parentheses stand around it - in a tree and inside an argument list alike.

template<int N>
struct place
{
  static const int value = N;
};

static_assert(("abc")[1] == 'b', "");
static_assert((("abc"))[1] == 'b', "");
static_assert(place<("abc")[1]>::value == 'b', "");
static_assert(place<(("abc"))[1]>::value == 'b', "");
static_assert(place<("abc")[1] + 1>::value == 'c', "");
static_assert(place<"abc"[1]>::value == 'b', "");

// The parentheses hold a cast rather than a primary here, which is the other
// reading of the same words and is settled by what they name.
static_assert(place<(int)'b'>::value == 'b', "");
static_assert(place<(char)("abc")[1]>::value == 'b', "");

template<class T, int N>
struct held
{
  static const int value = N + sizeof(T);
};

int main()
{
  int room[("abcd")[2] - 'a'];
  return (sizeof(room) / sizeof(int) == 2 &&
          held<int, ("abc")[0] - 'a'>::value == 4)
    ? 0 : 1;
}
