// VALIDATION: compile-pass
// N3485 focus: 8.5.2 [dcl.init.string], 8.5.4 [dcl.init.list], 12.6.2 [class.base.init]
//
// 8.5.2p1 initializes an array of character type with the code units of a
// string literal, and 8.5.4p1 lets the literal be written inside braces: `{"ab"}`
// and `"ab"` are the same initialization of the same array.  So the braces the
// program wrote are the list the elements stand under, and the reading is one
// whichever of the four places the list stands in - a declaration, a clause of
// an enclosing list, 12.6.2p2's mem-initializer and 12.6.2p8's
// brace-or-equal-initializer.
//
// A list whose elements stand under a node of its own and a list whose elements
// stand under a node inside it are two shapes of one construct, and a reader
// that knows the first finds nothing in the second: what is written for the
// second element is then what an element no clause reached holds, and the
// literal has quietly lost every unit but its first.

struct held_in_braces
{
  char spelled[4];
  held_in_braces() : spelled{"ab"} {}
};

struct held_by_equals
{
  char spelled[4] = "cd";
};

char written_plainly[3] = "ef";
char written_in_braces[3] = {"gh"};

struct held_as_a_clause
{
  char spelled[3];
  int counted;
};

held_as_a_clause clause_written = {{"ij"}, 5};

int main()
{
  held_in_braces first;
  held_by_equals second;
  return first.spelled[1] + second.spelled[1] + written_plainly[1] +
         written_in_braces[1] + clause_written.spelled[1] +
         clause_written.counted;
}
