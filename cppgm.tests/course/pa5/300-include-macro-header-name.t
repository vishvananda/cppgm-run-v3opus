// The #include operand after macro replacement is a string-literal rather
// than a header-name, which is the other operand form of 16.2.
#define QUOTE(x) #x
#define HEADER QUOTE(include-macro-header.h)
#include HEADER
#define INDIRECT "include-macro-header.h"
#include INDIRECT
