// VALIDATION: compile-pass
// N3485 focus: 2.14.5 [lex.string], 12.8 [class.copy], 3.7.1 [basic.stc.static]
//
// 2.14.5p8 makes a string literal an array object with static storage
// duration, which the program holds because the literal was written and not
// because anything evaluates the expression it stands in.  So where 12.8p15's
// copy of a class that holds nothing leaves an initializer unread, the arrays
// its literals name still stand in the program - one per distinct literal,
// however many of the initializers were left out.

struct empty {};

empty operator+(const char *, const empty &) { return empty(); }

const char *keep(const char *text) { return text; }

int main() {
  empty source;
  empty first = "one" + source;
  empty second = "two" + source;
  const char *named = keep("one");
  (void)first;
  (void)second;
  return named[0] == 'o' ? 0 : 1;
}
