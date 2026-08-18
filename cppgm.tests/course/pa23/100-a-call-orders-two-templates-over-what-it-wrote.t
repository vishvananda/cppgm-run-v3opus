// VALIDATION: compile-pass
// 14.8.2.4p3's first bullet: the types a partial ordering is done over in the
// context of a function call are the parameter types the call *has arguments
// for*, and p3's footnote says a default argument is no argument here.  So two
// declarations whose parameter lists are of different lengths are still
// ordered, over the places the call filled - and 14.8.2.4p12 keeps a trailing
// run a place of its own however few arguments it took, which is the whole of
// what leaves a fixed list ahead of one that ends in a pack.

struct graph
{
};

struct generator
{
};

// The pair a call of six arguments orders over six places: one declaration
// wrote two defaulted `bool`s there and the other deduces both, and only the
// first's `bool` refuses to be deduced from the other's stand-in.
template<class Graph, class Random>
int build(Graph &, int, int, Random &, bool = true, bool = false)
{
	return 1;
}

template<class Graph, class Random, class Vertices, class Edges>
int build(Graph &, int, int, Random &, Vertices, Edges, bool = false)
{
	return 2;
}

// 14.8.2.4p8's first sentence, reached only because the run is not cut off by
// the three arguments the call wrote: an A a function parameter pack was
// transformed from, against a P that is not one, is a deduction that fails.
template<class Context, class Item>
int visit(Context &, Item, int)
{
	return 4;
}

template<class Context, class Item, class... Rest>
int visit(Context &, Item, int, Rest...)
{
	return 8;
}

// 14.8.2.4p5, p7 and p9 over one place: with the references and the top-level
// qualifiers off, the two wrote one type, so what orders them is what those
// took - an lvalue reference is more specialized than what is not one.
template<class T>
int bind(const T &)
{
	return 16;
}

template<class T>
int bind(T &&, int = 0, int = 0)
{
	return 32;
}

graph shared;

const graph &frozen()
{
	return shared;
}

int main()
{
	graph g;
	generator r;
	return build(g, 1, 2, r, true, true) + visit(g, 1, 2) + bind(frozen()) +
					bind(g) ==
				1 + 4 + 16 + 32
		? 0
		: 1;
}
