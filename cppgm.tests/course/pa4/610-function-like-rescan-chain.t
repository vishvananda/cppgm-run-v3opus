// A function-like name in a replacement list is hidden only from what the
// head and the closing paren agree on, so a mutual pair keeps expanding for
// as long as the file supplies parentheses.
#define f(x) 1 g
#define g(x) 2 f
f(a)(b)(c)(d)

// An argument keeps the nesting of the invocation it is substituted into,
// which is what stops g here.
#define p(x) 1 x
#define q(x) 2 x
q(p)(q)(3)

// But an argument the nesting does not name is invoked as usual, even when
// the head it is substituted under came from a helper macro.
#define ID(x) x
#define A(m) m(MK)
#define MK() A
MK()(ID)()
