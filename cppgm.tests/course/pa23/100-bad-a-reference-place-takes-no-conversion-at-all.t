// 14.3.2p5: for a place of reference type no conversions apply.  The type
// referred to may be more cv-qualified than the otherwise identical type of the
// object the argument designates, and `long` is not `int` however it is
// qualified.
static long wide = 0;

template<const int &R>
struct at
{
	static int read() { return R; }
};

int main()
{
	return at<wide>::read();
}
