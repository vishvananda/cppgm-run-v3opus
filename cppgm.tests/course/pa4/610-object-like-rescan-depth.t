// An object-like name stays hidden inside the replacement list of the
// function-like macro its own expansion reached, and is free again once a
// further rescan has left that invocation behind.
#define G(x) 8 OBJ
#define OBJ 9 G
OBJ(1)

#undef G
#undef OBJ
#define G(x) H
#define H(x) OBJ
#define OBJ 9 G
OBJ(1)(2)

#undef G
#undef H
#undef OBJ
#define OBJ 9 G
#define G(x) 8 OBJ2
#define OBJ2 7 OBJ
OBJ(1)
