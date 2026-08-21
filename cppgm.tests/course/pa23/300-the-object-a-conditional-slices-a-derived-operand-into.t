// 5.16p3's second bullet: where one operand's class derives from the other's,
// the operand "is changed to a prvalue of the base class copy-initialized from
// it" - which is one initialization of the result object and not a step written
// above one.  So the operand may be a prvalue as much as an lvalue, and what
// binds the base class subobject is the parameter of the constructor 13.3
// chose, at whatever offset that base stands at.
struct base
{
	int held;
	base(int n) { held = n; }
	base(base const & other) { held = other.held + 100; }
};

struct derived : base
{
	derived(int n) : base(n) { }
};

struct alpha
{
	int one;
	alpha(int n) { one = n; }
	alpha(alpha const & other) { one = other.one + 10; }
};

struct pair_of : alpha, base
{
	pair_of(int n) : alpha(n), base(n + 1) { }
};

base named(1);
const base kept(2);

int total(base const & one) { return one.held; }

int main()
{
	int sum = 0;
	// a derived prvalue against a base lvalue, written in either order
	sum = sum + total(1 ? derived(3) : named);
	sum = sum + total(0 ? named : derived(4));
	// a derived prvalue against a *const* base lvalue
	sum = sum + total(1 ? derived(5) : kept);
	// a derived prvalue against a base prvalue
	sum = sum + total(0 ? base(6) : derived(7));
	// the lvalue shape the same bullet already reached
	derived stands(8);
	sum = sum + total(1 ? stands : named);
	// a base that is not the first one, so the subobject stands at an offset
	sum = sum + total(1 ? pair_of(9) : named);
	return sum & 255;
}
