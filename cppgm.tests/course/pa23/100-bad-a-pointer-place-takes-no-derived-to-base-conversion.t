// 14.3.2p5's note: 4.10p3's derived-to-base conversion is not one of the
// conversions a template argument reaches a place of pointer type through.
// The address of `object` is no `right *`, and reading `b` through it as one
// would read the wrong storage rather than merely accept the program.
struct left { int a; };
struct right { int b; };
struct both : left, right {};

static both object;

template<right *P>
struct at
{
	static int read() { return P->b; }
};

int main()
{
	object.b = 3;
	return at<&object>::read() - 3;
}
