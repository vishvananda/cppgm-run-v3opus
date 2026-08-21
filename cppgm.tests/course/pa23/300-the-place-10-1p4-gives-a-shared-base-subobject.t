struct A { int a; };
struct B { int b; int twice() { return b + b; } };
struct D : A, virtual B { int d; };
int main()
{
	D v[2];
	v[1].a = 1;
	v[1].b = 2;
	v[1].d = 4;
	A *pa = &v[1];
	B *pb = &v[1];
	return v[1].twice() + pa->a + pb->b + v[1].d;
}
