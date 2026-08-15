// VALIDATION: compile-pass
// 2.14.3p1: an ordinary character-literal has type `char` whatever its one
// c-char is, and 2.14.5p5 encodes a c-char that is no numeric escape in the
// execution character set - which the course defines as UTF-8, so a c-char
// above the ASCII range is more than one code unit of the run a
// multicharacter literal packs.

// One c-char is a `char`, and one no ordinary code unit holds is that code
// unit rather than an `int` holding the code point PA2's dump prints.
static_assert(sizeof('\xff') == 1, "");
static_assert('\xff' == -1, "");
static_assert('\377' == -1, "");
static_assert(sizeof('a') == 1 && 'a' == 97, "");

// A run of them packs code units and not code points, so a c-char written as
// a source character or a universal-character-name is the two the execution
// encoding gives it, while a numeric escape is the one code unit it names.
static_assert('ab' == 0x6162, "");
static_assert('aé' == 0x61c3a9, "");
static_assert('abĀ' == 0x6162c480, "");
static_assert('\x41\xe9' == 0x41e9, "");
static_assert('\x41\xff' == 0x41ff, "");
static_assert(sizeof('aé') == sizeof(int), "");

// 2.14.5p12: a string literal holds the same code units, so the element
// 5.19p2 reads out of one is a `char` too.
static_assert("\xff"[0] == -1, "");
static_assert("é"[0] == -61 && "é"[1] == -87, "");
static_assert("é"[2] == 0, "");

template<int N>
struct place
{
  static const int value = N;
};

// The same reading answers where an argument list writes the literal as text.
static_assert(place<'\xff'>::value == -1, "");
static_assert(place<'aé'>::value == 0x61c3a9, "");
static_assert(place<"\xff"[0]>::value == -1, "");

// 4.7p2 widens the value the literal already has, so a `char` holding a high
// bit reaches a wider object as the negative value it is.
long long widened = '\xff';
int narrowed = '\377';

int main()
{
  long long held = '\xff';
  return (held == -1 && widened == -1 && narrowed == -1 &&
          '\x41\xe9' == 16873)
    ? 0 : 1;
}
