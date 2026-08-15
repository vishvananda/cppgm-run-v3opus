// VALIDATION: compile-pass
// 2.14.3p1: a character-literal holding more than one c-char has type `int`
// and a value the implementation defines - here the last four of them packed
// one code unit each, the first of them most significant.

static_assert('ab' == 0x6162, "");
static_assert('abc' == 0x616263, "");
static_assert('abcd' == 0x61626364, "");
static_assert('abcde' == 0x62636465, "");
static_assert('\x41\x42' == 0x4142, "");
static_assert('\101\102' == 0x4142, "");
static_assert('a\n' == 0x610a, "");
static_assert(sizeof('ab') == sizeof(int), "");

// 2.14.3p1 leaves the one c-char literal alone: an ordinary one in the ASCII
// range is still a `char`.
static_assert(sizeof('a') == 1 && 'a' == 97, "");

enum packed { two = 'ab' };

template<int N>
struct place
{
  static const int value = N;
};

int main()
{
  int bound['\1\2' | 0];
  int written = 0x6162;
  switch (written)
  {
  case 'ab':
    return place<'ab'>::value == two && sizeof(bound) / sizeof(int) == 258
      ? 0 : 1;
  default:
    return 1;
  }
}
