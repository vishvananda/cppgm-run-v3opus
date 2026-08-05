// The spelling `##` joins has already been through phases 1 and 2, so the `\`
// it ends with is a token of its own rather than a line splice, and `x\` is
// not one preprocessing-token.
#define cat(a,b) a ## b
cat(x,\)
