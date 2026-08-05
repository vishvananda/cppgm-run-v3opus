// 16.2/4: macro replacement leaves a `<...>` operand spelled as the
// preprocessing-tokens it was written with, which an implementation combines
// back into a header-name its own way.
#define HEADER <course/pa5/combined-header-name.h>
#include HEADER
#define SPACED < course/pa5/combined-header-name.h >
#include SPACED
#define LT <
#define GT >
#include LT course/pa5/combined-header-name.h GT
#define NOTHING
#include NOTHING <course/pa5/combined-header-name.h>
