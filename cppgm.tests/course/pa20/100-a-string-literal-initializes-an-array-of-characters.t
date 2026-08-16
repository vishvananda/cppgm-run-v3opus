// N3485 focus: 8.5.2 [dcl.init.string] p1 with 8.5 [dcl.init] p14 and 8.3.4
// [dcl.array] p3 - an array of character type written with a string literal
// holds the code units of the literal, the terminating one among them.  The
// braces 8.5.1 would otherwise count the clauses of may be written or left out,
// an array of unknown bound takes its length from the literal, and the elements
// past the literal hold what any other unreached element holds.

char narrow[4] = "abc";
char braced[3] = { "de" };
char deduced[] = "fghi";
char deduced_braced[] = { "jk" };
const char16_t wide[] = u"lm";
const char32_t widest[] = U"n";
unsigned char unsigned_units[4] = "opq";

struct holds
{
  char text[4];
  int after;
};

holds aggregate = { "rst", 7 };
holds aggregate_braced = { { "uv" }, 8 };

int main()
{
  char local[5] = "wxyz";
  char padded[6] = "abc";
  char local_deduced[] = "AB";
  // 8.5.2p1: the elements are the code units, and the one past them is the
  // terminating null the literal itself holds.
  if (narrow[0] != 'a' || narrow[2] != 'c' || narrow[3] != 0) { return 1; }
  // 8.5.1p7 and 8.5.2p1: an element past the literal holds zero.
  if (padded[3] != 0 || padded[4] != 0 || padded[5] != 0) { return 2; }
  if (braced[0] != 'd' || braced[1] != 'e' || braced[2] != 0) { return 3; }
  // 8.3.4p3: the bound is the code units the literal holds.
  if (sizeof(deduced) != 5) { return 4; }
  if (deduced[3] != 'i' || deduced[4] != 0) { return 5; }
  if (sizeof(deduced_braced) != 3) { return 6; }
  if (deduced_braced[1] != 'k') { return 7; }
  // 8.5.2p1: each of 2.14.5's prefixed literals initializes the array of its
  // own element type, and the bound counts code units and not bytes.
  if (sizeof(wide) != 3 * sizeof(char16_t)) { return 8; }
  if (wide[0] != u'l' || wide[2] != 0) { return 9; }
  if (sizeof(widest) != 2 * sizeof(char32_t)) { return 10; }
  if (widest[0] != U'n' || widest[1] != 0) { return 11; }
  // 8.5.2p1: an ordinary literal initializes an array of any narrow character
  // type, `unsigned char` among them.
  if (unsigned_units[2] != 'q' || unsigned_units[3] != 0) { return 12; }
  // 8.5.1p2: the literal is one clause of the enclosing list, whether or not
  // its own braces were written.
  if (aggregate.text[0] != 'r' || aggregate.text[3] != 0) { return 13; }
  if (aggregate.after != 7) { return 14; }
  if (aggregate_braced.text[1] != 'v' || aggregate_braced.after != 8)
  {
    return 15;
  }
  if (local[3] != 'z' || local[4] != 0) { return 16; }
  if (sizeof(local_deduced) != 3 || local_deduced[0] != 'A') { return 17; }
  return 0;
}
