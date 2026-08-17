// VALIDATION: compile-pass
// N3485 focus: 14.2 [temp.names], 13.4 [over.over], 5.19 [expr.const]
//
// 14.2 leaves a template-id naming the specializations its argument list makes
// rather than anything an ordinary lookup finds, and 5.19's readings look a
// name up at three doors of their own - the value of an id-expression, the
// object one designates, and the one a template-argument spelling holds.  All
// three ask 14.2 before ordinary lookup, exactly as an expression's own reading
// does, and 14.7.1p1's demand is made where a specialization is chosen.

typedef int (*fn_type)();

template<class T>
int free_function()
{
	return 1;
}

int free_function(int)
{
	return 100;
}

template<class T, class U>
int free_function()
{
	return 2;
}

struct plain
{
	template<class T>
	static int member()
	{
		return 4;
	}
};

template<class T>
struct holder
{
	template<class U>
	static int nested()
	{
		return 8;
	}

	static int own()
	{
		return 16;
	}
};

// 5.3.1p3 over a name 14.2 wrote: the address is of the specialization the
// written list makes, and no declaration of the whole spelling is in scope.
constexpr fn_type addressed = &free_function<char>;
// 4.3p1 with the same name: the id-expression decays where the place is a
// pointer, which is the value reading of it rather than the designated one.
constexpr fn_type decayed = free_function<char>;
// 14.2p4's keyword written where 5.1p1 allows it.
constexpr fn_type keyword = &plain::template member<char>;
constexpr fn_type unkeyed = plain::member<char>;
// 14.5.2p1: a member template of a class template, named through the
// specialization an argument list made of the enclosing class.
constexpr fn_type nested = &holder<long>::template nested<char>;
constexpr fn_type own = holder<long>::own;

template<class T>
struct table
{
	// 14.7.1p1: the naming stands in a member's own initializer, which is read
	// once where the pattern stands and again for each specialization - and only
	// the second of those asks this unit for a body.
	static constexpr fn_type entries[] = {
		&holder<T>::template nested<char>,
		free_function<T>
	};
};

template<class T>
constexpr fn_type table<T>::entries[];

int main()
{
	const int reached = addressed() + decayed() + keyword() + unkeyed() +
		nested() + own();
	const int through = table<long>::entries[0]() + table<long>::entries[1]();
	return reached == 1 + 1 + 4 + 4 + 8 + 16 && through == 8 + 1 ? 0 : 1;
}
