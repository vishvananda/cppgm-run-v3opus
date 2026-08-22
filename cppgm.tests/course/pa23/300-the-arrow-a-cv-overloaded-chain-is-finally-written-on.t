// N3485 focus: 13.5.6 [over.ref] the operand a cv-overloaded chain hands the arrow
struct value {
  int held;
  int twice() const { return held * 2; }
};

value one = {7};

// The non-const `operator->` hands back a `const` of this same class, and the
// const one hands back the pointer - so 13.5.6p1's process is two steps and it
// ends.  The class is stepped through twice and the *operand* is not: `alt`
// stood at the first step and `const alt` stands at the second.
struct alt {
  value *at;
  const alt operator->() { return *this; }
  value *operator->() const { return at; }
};

// A chain of three distinct classes, which ends because the last hands back a
// pointer, beside one written on an operand that is already const.
struct inner {
  value *at;
  value *operator->() const { return at; }
};

struct outer {
  inner step;
  inner operator->() const { return step; }
};

int main() {
  alt from = {&one};
  if (from->held != 7) { return 1; }
  if (from->twice() != 14) { return 2; }

  const alt settled = {&one};
  if (settled->held != 7) { return 3; }

  int *reached = &from->held;
  if (*reached != 7) { return 4; }
  from->held = 9;
  if (one.held != 9) { return 5; }

  inner near = {&one};
  outer far = {near};
  if (far->held != 9) { return 6; }
  return 0;
}
