// 14.3.2p5's note: although 0 is a valid template-argument for a place of
// integral type, it is not one for a place of pointer type - only an argument
// of type `std::nullptr_t` reaches such a place through 4.10p1.
static int number = 0;

template<int *P>
struct at
{
	static int read() { return *P; }
};

int main()
{
	return at<0>::read();
}
