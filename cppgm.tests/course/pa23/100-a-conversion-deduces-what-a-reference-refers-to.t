// VALIDATION: compile-pass
// 14.8.2.3p2 and p4: the one P of a conversion deduction is the type the
// conversion-type-id handed back and the one A is the type required as the
// result, and a reference on *either* side is the type it refers to before the
// pair is read.  p3 and p4's first sentence then leave neither side carrying a
// top-level qualification, and p5's first bullet is what lets the deduced A be
// the less qualified type a reference refers to.
//
// So `operator U()` deduces `U` from `const holder &` exactly as it does from
// `holder`.  That is what makes the conversion 13.3.1.4 gathers for a copy
// constructor and the one it gathers for a move constructor *one* function -
// and 13.3.3.2p3 orders two user-defined sequences only where they hold one,
// so before this the pair was two and the copy-initialization named neither.
//
// The reference *binding* half of p4's second sentence is left out here: the
// reference binary deduces `const holder &` where `g++` and this build deduce
// `holder`, so no fixture can pin it either way.

struct holder
{
	int held;
};

holder origin = {20};

int anchor = 9;

struct number
{
	int held;

	template<class T>
	operator T() const
	{
		return static_cast<T>(held);
	}
};

struct maker
{
	template<class T>
	operator T() const
	{
		return origin;
	}
};

// 14.8.2.3p2 read at the other side: a P that is a reference is the type it
// refers to, so the deduction is over `F` and the declaration still hands back
// a reference the initialization binds without a temporary.
struct lender
{
	template<class F>
	operator const F &() const
	{
		return anchor;
	}
};

int by_value(holder taken)
{
	return taken.held;
}

int main()
{
	number counted = {5};
	// A is no reference at all, which is the reading that already worked.
	long widened = counted;
	int narrowed = counted;
	maker made;
	// 13.3.1.4: the copy constructor's `const holder &` and the move
	// constructor's `holder &&` are one conversion function now, so the two
	// candidates are ordered by their second standard conversion sequences.
	holder copied = made;
	lender lent;
	const int &borrowed = lent;
	return widened + narrowed + copied.held + by_value(made) + borrowed ==
				5 + 5 + 20 + 20 + 9
		? 0
		: 1;
}
