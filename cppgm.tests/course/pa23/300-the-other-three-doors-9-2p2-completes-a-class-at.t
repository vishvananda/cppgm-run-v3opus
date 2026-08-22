// 9.2p2 names four complete-class contexts, and a function body is only one of
// them: a default argument, a brace-or-equal-initializer and an
// exception-specification are read where the class is complete too.  So each is
// put aside at the member specification and read at the `}` -
// `ptr->pop<long>(v)` written above `template<class U> bool pop(U &)` is 14.2's
// template-id at all four doors and 5.9's two comparisons where the declarator
// stands.
//
// The exception-specification carries the fact twice over: the parse reads its
// operand at the `}`, and 15.4p1's own condition is *folded* there as well,
// because a fold made at the declarator answers about a class that has none of
// the members below it yet - which for a condition naming one is no answer at
// all and leaves the function throwing.
struct holder
{
	static holder * ptr;
	static long v;

	static int made(int width = (int)sizeof(ptr->pop<long>(v)))
	{
		return width;
	}

	int held = (int)sizeof(ptr->pop<long>(v)) + (int)sizeof(inner) + span;

	static int settled() noexcept(sizeof(ptr->pop<long>(v)) == 1)
	{
		return 0;
	}

	template<class U> bool pop(U & u) { return u != 0; }
	struct inner { int i; };
	static const int span = 3;
};

holder * holder::ptr = 0;
long holder::v = 7;

int main()
{
	holder object;
	return holder::made() - 1 + object.held - 8 + holder::settled() +
	       (noexcept(holder::settled()) ? 0 : 1);
}
