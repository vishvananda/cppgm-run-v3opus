struct B { int i; };
struct D : virtual B { int j; };
template<class, class> constexpr int probe(...) { return 1; }
template<class T, class U, class = decltype(static_cast<U *>((T *)0))>
constexpr int probe(int) { return 2; }
int main()
{
	D d;
	d.i = 3;
	d.j = 4;
	B &b = d;
	return probe<B, D>(0) + b.i + d.j;
}
