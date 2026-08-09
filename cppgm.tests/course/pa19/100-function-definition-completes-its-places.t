// 8.3.5p6 read the other way: the parameter and return types of a function
// *definition* shall be complete, so the walk that builds the objects a body
// names is where 14.7.1p1 is asked for a specialization only a declaration
// had named.
template<class T> struct holder { int base; };

struct box { int q; };
typedef holder<box> named;

int consume(holder<box> x) { return x.base; }
holder<box> produce() { holder<box> h; h.base = 39; return h; }

int main() { return consume(produce()) == 39 ? 0 : 1; }
