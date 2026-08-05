// The presumed location of a token is the one in force where it was written,
// so a `#line` cannot renumber the argument list it stands in.
#define WHERE(x) x __LINE__
WHERE(1
#line 100
)
