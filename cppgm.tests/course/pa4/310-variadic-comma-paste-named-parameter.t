// The comma before `## __VA_ARGS__` disappears only when the variadic
// argument is empty; otherwise the argument is macro replaced as usual.
#define E 9
#define CALL(a,...) target(a, ##__VA_ARGS__)
CALL(1)
CALL(1,E)
CALL(1,2,3)
